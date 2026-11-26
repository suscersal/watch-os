// ota_esp.c - запись нового образа в неактивный OTA-слот (ota_0/ota_1, см. partitions.csv).
//
// Безопасность обновления:
//  * принимается только образ этой же прошивки: заголовок должен быть образом ESP32-S3 с тем же project_name, что у запущенной.
//    Так нельзя случайно залить полный образ watch-os-full.bin, чужой проект или мусор;
//  * после записи esp_ota_end() проверяет весь образ (контрольная сумма и SHA-256), и только потом слот становится загрузочным;
//  * включён откат (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE): новая прошивка стартует в состоянии PENDING_VERIFY. Если за 20 секунд
//    она упала или зависла и сработал сторожевой таймер, загрузчик сам вернёт прежнюю. Рабочую подтверждает таймер ниже.
// Запись идёт через буфер 4 КБ: TCP присылает куски по ~1.4 КБ, а каждая запись во флеш на время останавливает кэш.
#include "ota_esp.h"
#include <string.h>
#include "esp_app_format.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "watch/ota.h"

static const char *TAG = "ota";

#define STAGE 4096

static const esp_partition_t *s_part;
static esp_ota_handle_t       s_h;
static bool                   s_open;
static uint8_t                s_stage[STAGE];
static size_t                 s_n, s_total;

// Первые байты образа: заголовок (24), заголовок первого сегмента (8), затем esp_app_desc_t.
static int check_header(const uint8_t *b, size_t n)
{
    const size_t off = sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t);
    if (n < off + sizeof(esp_app_desc_t)) return OTA_E_FORMAT;
    esp_image_header_t h;
    esp_app_desc_t nd, cur;
    memcpy(&h, b, sizeof h);
    memcpy(&nd, b + off, sizeof nd);
    if (h.magic != ESP_IMAGE_HEADER_MAGIC || h.chip_id != ESP_CHIP_ID_ESP32S3) { ESP_LOGW(TAG, "не образ ESP32-S3"); return OTA_E_FORMAT; }
    if (nd.magic_word != ESP_APP_DESC_MAGIC_WORD) { ESP_LOGW(TAG, "нет описания приложения"); return OTA_E_FORMAT; }
    if (esp_ota_get_partition_description(esp_ota_get_running_partition(), &cur) == ESP_OK &&
        strncmp(nd.project_name, cur.project_name, sizeof cur.project_name) != 0) {
        ESP_LOGW(TAG, "чужой проект: %.32s вместо %.32s", nd.project_name, cur.project_name);
        return OTA_E_FORMAT;
    }
    ESP_LOGI(TAG, "принимаю образ %.32s %.32s", nd.project_name, nd.version);
    return OTA_OK;
}

static void hw_abort(void)
{
    if (s_open) { esp_ota_abort(s_h); s_open = false; }
    s_n = s_total = 0;
}

static bool hw_begin(uint32_t size)
{
    hw_abort();
    s_part = esp_ota_get_next_update_partition(NULL);
    if (!s_part || size > s_part->size) { ESP_LOGW(TAG, "нет слота под %u байт", (unsigned)size); return false; }
    esp_err_t e = esp_ota_begin(s_part, size, &s_h);      // стирает ровно size байт
    if (e != ESP_OK) { ESP_LOGW(TAG, "esp_ota_begin: %s", esp_err_to_name(e)); return false; }
    s_open = true;
    return true;
}

static int flush(void)
{
    if (s_n == 0) return OTA_OK;
    if (s_total == 0) { int e = check_header(s_stage, s_n); if (e != OTA_OK) return e; }   // заголовок проверяем до записи
    if (esp_ota_write(s_h, s_stage, s_n) != ESP_OK) return OTA_E_STORAGE;
    s_total += s_n;
    s_n = 0;
    return OTA_OK;
}

static int hw_write(const uint8_t *d, size_t n)
{
    if (!s_open) return OTA_E_STORAGE;
    while (n > 0) {
        size_t k = STAGE - s_n < n ? STAGE - s_n : n;
        memcpy(s_stage + s_n, d, k);
        s_n += k; d += k; n -= k;
        if (s_n == STAGE) { int e = flush(); if (e != OTA_OK) { hw_abort(); return e; } }
    }
    return OTA_OK;
}

static int hw_end(void)
{
    if (!s_open) return OTA_E_STORAGE;
    int e = flush();
    if (e != OTA_OK) { hw_abort(); return e; }
    s_open = false;
    esp_err_t r = esp_ota_end(s_h);                         // контрольная сумма и SHA-256 всего образа
    if (r != ESP_OK) { ESP_LOGW(TAG, "esp_ota_end: %s", esp_err_to_name(r)); return r == ESP_ERR_OTA_VALIDATE_FAILED ? OTA_E_FORMAT : OTA_E_STORAGE; }
    r = esp_ota_set_boot_partition(s_part);
    if (r != ESP_OK) { ESP_LOGW(TAG, "set_boot_partition: %s", esp_err_to_name(r)); return OTA_E_STORAGE; }
    ESP_LOGI(TAG, "образ принят (%u байт), следующая загрузка из %s", (unsigned)s_total, s_part->label);
    return OTA_OK;
}

static void restart_cb(void *arg) { (void)arg; esp_restart(); }

static void hw_restart_soon(void)
{
    static esp_timer_handle_t t;
    const esp_timer_create_args_t a = { .callback = restart_cb, .name = "fw_restart" };
    if (!t && esp_timer_create(&a, &t) != ESP_OK) { esp_restart(); return; }
    esp_timer_start_once(t, 2000000);                       // ответ OK успевает дойти до телефона
}

static const ota_hw_t HW = { hw_begin, hw_write, hw_end, hw_abort, hw_restart_soon };

static void valid_cb(void *arg)
{
    (void)arg;
    esp_err_t e = esp_ota_mark_app_valid_cancel_rollback();
    ESP_LOGI(TAG, "новая прошивка подтверждена: %s", esp_err_to_name(e));
}

void ota_esp_start(void)
{
    ota_set_hw(&HW);
    esp_ota_img_states_t st;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
        static esp_timer_handle_t t;
        const esp_timer_create_args_t a = { .callback = valid_cb, .name = "fw_valid" };
        if (esp_timer_create(&a, &t) == ESP_OK) esp_timer_start_once(t, 20000000);
        ESP_LOGW(TAG, "первая загрузка после обновления: подтвержу через 20 с, если не упаду");
    }
}
