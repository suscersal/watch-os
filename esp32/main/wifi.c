// wifi.c - Wi-Fi в режиме станции: подключение при старте к сохранённой сети и управление из экрана Settings
// (watch/wifi_ctl.h): поиск сетей, подключение с паролем, «забыть сеть».
// Сеть хранится в settings.json на LittleFS (src/watch/settings.c), а не в NVS Wi-Fi: esp_wifi_set_storage(RAM).
// Если там пусто, берутся CONFIG_WATCH_WIFI_SSID / PASSWORD из menuconfig (запасной путь для первой прошивки).
#include "wifi.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "watch/settings.h"
#include "watch/wifi_ctl.h"

static const char *TAG = "wifi";

#define BIT_GOT_IP BIT0
#define USER_TRIES 3          // столько неудач подряд для сети, которую выбрал пользователь, и мы сдаёмся

static EventGroupHandle_t s_ev;
static esp_timer_handle_t s_retry_timer;
static bool               s_started;
static int                s_retry;

// Состояние запроса пользователя. Пишется из задачи событий Wi-Fi, читается из задачи LVGL: поля простые, гонка безвредна.
static volatile wifi_state_t s_state = WIFI_ST_IDLE;
static volatile bool         s_user_req;
static volatile int          s_user_fail;
static volatile bool         s_want_conn;     // есть сеть, к которой нужно держать подключение (повторять попытки)
static char                  s_ssid[33], s_pass[65];
static char                  s_cfg_ssid[33];  // сеть, записанная в конфигурацию станции сейчас

// Результаты поиска: собираются в обработчике события, читаются экраном.
static wifi_net_t        s_nets[WIFI_NETS_MAX];
static volatile int      s_nnets;
static volatile bool     s_scanning;

static void retry_cb(void *arg) { (void)arg; esp_wifi_connect(); }

static void apply_config(const char *ssid, const char *pass)
{
    wifi_config_t wc = { 0 };
    snprintf(s_cfg_ssid, sizeof s_cfg_ssid, "%s", ssid);
    snprintf((char *)wc.sta.ssid, sizeof wc.sta.ssid, "%s", ssid);
    snprintf((char *)wc.sta.password, sizeof wc.sta.password, "%s", pass);
    wc.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;   // порог: WPA2 и всё, что надёжнее
    wc.sta.pmf_cfg.capable = true;
    wc.sta.pmf_cfg.required = false;
    wc.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
#ifdef CONFIG_WATCH_POWER_SAVE
    wc.sta.listen_interval = 10;                       // просыпаться на маяк раз в ~1 с: уведомления приходят с задержкой до секунды
#endif
    esp_wifi_set_config(WIFI_IF_STA, &wc);
}

// Сохранённая сеть (или из menuconfig); false, если ни той ни другой нет.
static bool load_saved(char *ssid, size_t sn, char *pass, size_t pn)
{
    if (settings_wifi_get(ssid, sn, pass, pn)) return true;
    if (CONFIG_WATCH_WIFI_SSID[0]) {
        snprintf(ssid, sn, "%s", CONFIG_WATCH_WIFI_SSID);
        snprintf(pass, pn, "%s", CONFIG_WATCH_WIFI_PASSWORD);
        return true;
    }
    return false;
}

static void schedule_retry(void)
{
    // Пауза растёт: 1, 2, 4, 8, 16, 30 с. Без неё недоступная сеть быстро сажает батарею.
    int shift = s_retry < 5 ? s_retry : 5;
    uint64_t delay_us = 1000000ULL << shift;
    if (delay_us > 30000000ULL) delay_us = 30000000ULL;
    s_retry++;
    esp_timer_stop(s_retry_timer);            // ошибку (таймер не запущен) игнорируем
    esp_timer_start_once(s_retry_timer, delay_us);
    ESP_LOGW(TAG, "disconnected, retry in %u s", (unsigned)(delay_us / 1000000ULL));
}

static void on_scan_done(void)
{
    uint16_t n = WIFI_NETS_MAX;
    wifi_ap_record_t rec[WIFI_NETS_MAX];
    int out = 0;
    if (esp_wifi_scan_get_ap_records(&n, rec) == ESP_OK) {          // записи уже отсортированы по уровню сигнала
        for (int i = 0; i < n && out < WIFI_NETS_MAX; i++) {
            if (rec[i].ssid[0] == '\0') continue;                   // скрытые сети не показываем
            bool dup = false;
            for (int k = 0; k < out; k++) if (strcmp(s_nets[k].ssid, (const char *)rec[i].ssid) == 0) dup = true;
            if (dup) continue;                                      // у одной сети бывает несколько точек
            snprintf(s_nets[out].ssid, sizeof s_nets[out].ssid, "%s", (const char *)rec[i].ssid);
            s_nets[out].rssi = rec[i].rssi;
            s_nets[out].secure = rec[i].authmode != WIFI_AUTH_OPEN;
            out++;
        }
    }
    s_nnets = out;
    s_scanning = false;
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        char ssid[33], pass[65];
        if (load_saved(ssid, sizeof ssid, pass, sizeof pass)) {
            apply_config(ssid, pass);
            snprintf(s_ssid, sizeof s_ssid, "%s", ssid);
            s_want_conn = true;
            s_state = WIFI_ST_CONNECTING;
            esp_wifi_connect();
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_SCAN_DONE) {
        on_scan_done();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *d = data;
        ESP_LOGW(TAG, "STA_DISCONNECTED reason=%d, internal free %u, largest %u",
                 d ? (int)d->reason : -1, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        xEventGroupClearBits(s_ev, BIT_GOT_IP);
        if (s_user_req) {
            if (++s_user_fail >= USER_TRIES) {                      // скорее всего неверный пароль: возвращаемся к сохранённой сети
                s_user_req = false;
                s_state = WIFI_ST_FAILED;
                char ssid[33], pass[65];
                s_want_conn = load_saved(ssid, sizeof ssid, pass, sizeof pass);
                if (s_want_conn) { apply_config(ssid, pass); s_retry = 0; schedule_retry(); }
                return;
            }
            esp_wifi_connect();
            return;
        }
        if (s_want_conn) {
            if (s_state == WIFI_ST_CONNECTED) s_state = WIFI_ST_CONNECTING;
            schedule_retry();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_retry = 0;
        xEventGroupSetBits(s_ev, BIT_GOT_IP);
        if (s_user_req) {                                           // пароль подошёл: запоминаем сеть
            s_user_req = false;
            settings_wifi_set(s_ssid, s_pass);
        }
        snprintf(s_ssid, sizeof s_ssid, "%s", s_cfg_ssid);
        s_want_conn = true;
        s_state = WIFI_ST_CONNECTED;
        ESP_LOGI(TAG, "got IP");
    }
}

bool wifi_start(void)
{
    s_ev = xEventGroupCreate();
    if (!s_ev) return false;

    const esp_timer_create_args_t targs = { .callback = retry_cb, .name = "wifi_retry" };
    if (esp_timer_create(&targs, &s_retry_timer) != ESP_OK) return false;

    if (esp_netif_init() != ESP_OK) return false;
    esp_err_t e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return false;   // цикл событий мог быть создан раньше
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t icfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&icfg) != ESP_OK) return false;
    // Страна RU: каналы 1-13 активные. По умолчанию (мир "01") 12-13 только пассивные, и на роутере
    // с каналом 13 аутентификация не проходит (STA_DISCONNECTED reason=2, AUTH_EXPIRE).
    esp_wifi_set_country_code("RU", true);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);            // не писать настройки Wi-Fi во флеш (сеть хранит settings.json)

    if (esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL) != ESP_OK) return false;
    if (esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL) != ESP_OK) return false;

    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) return false;
#ifdef CONFIG_WATCH_POWER_SAVE
    esp_wifi_set_ps(WIFI_PS_MAX_MODEM);                // радио спит между маяками (listen_interval)
#else
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);                // экономия энергии между передачами
#endif
    if (esp_wifi_start() != ESP_OK) return false;      // радио включено всегда: без него не работает поиск сетей на экране Settings
    s_started = true;
    return true;
}

bool wifi_is_connected(void)
{
    return s_ev && (xEventGroupGetBits(s_ev) & BIT_GOT_IP);
}

bool wifi_wait_connected(int timeout_ms)
{
    if (!s_ev) return false;
    EventBits_t b = xEventGroupWaitBits(s_ev, BIT_GOT_IP, pdFALSE, pdTRUE, pdMS_TO_TICKS(timeout_ms));
    return (b & BIT_GOT_IP) != 0;
}

// ---------- watch/wifi_ctl.h ----------
void wifi_ctl_scan(void)
{
    if (!s_started || s_scanning) return;
    wifi_scan_config_t sc = { 0 };
    sc.show_hidden = false;
    s_nnets = 0;
    s_scanning = true;
    if (esp_wifi_scan_start(&sc, false) != ESP_OK) s_scanning = false;   // false: не ждать, результат придёт событием SCAN_DONE
}

bool wifi_ctl_scanning(void) { return s_scanning; }

int wifi_ctl_results(wifi_net_t *out, int max)
{
    int n = s_nnets < max ? s_nnets : max;
    for (int i = 0; i < n; i++) out[i] = s_nets[i];
    return n;
}

void wifi_ctl_connect(const char *ssid, const char *pass)
{
    if (!s_started || !ssid || !ssid[0] || strlen(ssid) >= sizeof s_ssid) return;
    if (!pass) pass = "";
    if (strlen(pass) >= sizeof s_pass) return;
    esp_timer_stop(s_retry_timer);
    snprintf(s_ssid, sizeof s_ssid, "%s", ssid);
    snprintf(s_pass, sizeof s_pass, "%s", pass);
    bool was_connected = (xEventGroupGetBits(s_ev) & BIT_GOT_IP) != 0;
    s_user_fail = was_connected ? -1 : 0;     // закрытие текущего соединения само вызовет DISCONNECTED: эту «неудачу» не считаем
    s_user_req = true;
    s_want_conn = true;
    s_state = WIFI_ST_CONNECTING;
    xEventGroupClearBits(s_ev, BIT_GOT_IP);
    apply_config(ssid, pass);
    if (was_connected) esp_wifi_disconnect();   // событие DISCONNECTED запустит подключение к новой сети
    else               esp_wifi_connect();      // ошибку «уже идёт подключение» игнорируем: сеть подхватит текущая попытка
}

void wifi_ctl_forget(void)
{
    settings_wifi_clear();
    s_user_req = false;
    s_want_conn = false;
    s_state = WIFI_ST_IDLE;
    s_ssid[0] = '\0';
    if (s_started) {
        esp_timer_stop(s_retry_timer);
        esp_wifi_disconnect();
    }
}

wifi_state_t wifi_ctl_state(void) { return s_state; }
const char  *wifi_ctl_ssid(void)  { return s_ssid; }
