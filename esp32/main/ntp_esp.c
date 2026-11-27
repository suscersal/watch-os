// ntp_esp.c - сверка времени по SNTP. Как только Wi-Fi получил IP, спрашиваем NTP-сервер; ответ ставит часы (clock_set_utc)
// и пишет в RTC PCF85063, так что время переживает перезагрузку. Дальше сверка раз в CONFIG_WATCH_NTP_INTERVAL_MIN минут.
// Пояс не трогаем: SNTP знает только UTC, пояс приходит от телефона или из Settings.
#include "ntp_esp.h"
#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include "sdkconfig.h"

#ifdef CONFIG_WATCH_NTP
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bsp/esp-bsp.h"
#include "wifi.h"
#include "watch/clock.h"

static const char *TAG = "ntp";

static TaskHandle_t s_task;

// Вызывается из задачи tcpip: здесь только будим рабочую задачу. Блокировать (в т.ч. на замке LVGL) нельзя: задача LVGL
// ходит в сеть под этим замком и может ждать ответа от tcpip - получился бы взаимный затор.
static void sync_cb(struct timeval *tv)
{
    (void)tv;
    if (s_task) xTaskNotifyGive(s_task);
}

static void ntp_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        time_t now = time(NULL);
        if (now < 1500000000) continue;                     // системное время не выставлено - не верим
        if (!bsp_display_lock(15000)) continue;             // clock.c живёт под замком LVGL
        int64_t cur = 0;
        if (!clock_utc(&cur) || llabs((long long)(cur - (int64_t)now)) >= 2) {
            clock_set_utc((int64_t)now);
            ESP_LOGI(TAG, "time set from NTP: %lld", (long long)now);
        }
        bsp_display_unlock();
    }
}

// Новый IP (в т.ч. после обрыва Wi-Fi): перезапускаем SNTP, чтобы ответ пришёл сразу, а не через час.
static void restart_sntp(void)
{
    if (esp_sntp_enabled()) esp_sntp_stop();
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, CONFIG_WATCH_NTP_SERVER);
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_setservername(2, "time.cloudflare.com");
    esp_sntp_set_sync_interval((uint32_t)CONFIG_WATCH_NTP_INTERVAL_MIN * 60u * 1000u);
    esp_sntp_set_time_sync_notification_cb(sync_cb);
    esp_sntp_init();
}

static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base; (void)id; (void)data;
    restart_sntp();
}

void ntp_esp_start(void)
{
    if (!s_task && xTaskCreate(ntp_task, "ntp", 4096, NULL, 3, &s_task) != pdPASS) {
        ESP_LOGW(TAG, "task create failed");
        return;
    }
    if (esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_got_ip, NULL) != ESP_OK) {
        ESP_LOGW(TAG, "event register failed");
    }
    if (wifi_is_connected()) restart_sntp();                // IP мог прийти раньше, чем мы подписались
}
#else
void ntp_esp_start(void) {}
#endif
