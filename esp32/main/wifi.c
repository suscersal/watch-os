#include "wifi.h"
#include <stdint.h>
#include <stdio.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "wifi";

#define BIT_GOT_IP BIT0

static EventGroupHandle_t s_ev;
static esp_timer_handle_t s_retry_timer;
static int s_retry;

static void retry_cb(void *arg)
{
    (void)arg;
    esp_wifi_connect();
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_ev, BIT_GOT_IP);
        // Пауза между попытками растёт: 1, 2, 4, 8, 16, 30 с. Без неё недоступная сеть быстро сажает батарею.
        int shift = s_retry < 5 ? s_retry : 5;
        uint64_t delay_us = 1000000ULL << shift;
        if (delay_us > 30000000ULL) delay_us = 30000000ULL;
        s_retry++;
        esp_timer_stop(s_retry_timer);            // ошибку (таймер не запущен) игнорируем
        esp_timer_start_once(s_retry_timer, delay_us);
        ESP_LOGW(TAG, "disconnected, retry in %u s", (unsigned)(delay_us / 1000000ULL));
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_retry = 0;
        xEventGroupSetBits(s_ev, BIT_GOT_IP);
        ESP_LOGI(TAG, "got IP");
    }
}

bool wifi_start(void)
{
    if (CONFIG_WATCH_WIFI_SSID[0] == '\0') {
        ESP_LOGW(TAG, "SSID not set: idf.py menuconfig -> watch-os; Wi-Fi stays off");
        return false;
    }

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
    esp_wifi_set_storage(WIFI_STORAGE_RAM);            // не писать настройки Wi-Fi во флеш

    if (esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL) != ESP_OK) return false;
    if (esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL) != ESP_OK) return false;

    wifi_config_t wc = { 0 };
    snprintf((char *)wc.sta.ssid, sizeof wc.sta.ssid, "%s", CONFIG_WATCH_WIFI_SSID);
    snprintf((char *)wc.sta.password, sizeof wc.sta.password, "%s", CONFIG_WATCH_WIFI_PASSWORD);
    wc.sta.threshold.authmode = CONFIG_WATCH_WIFI_PASSWORD[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wc.sta.pmf_cfg.capable = true;
    wc.sta.pmf_cfg.required = false;

    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) return false;
    if (esp_wifi_set_config(WIFI_IF_STA, &wc) != ESP_OK) return false;
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);                // экономия энергии между передачами
    if (esp_wifi_start() != ESP_OK) return false;
    ESP_LOGI(TAG, "connecting to \"%s\"", CONFIG_WATCH_WIFI_SSID);
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
