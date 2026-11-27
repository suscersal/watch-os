// main.c - точка входа прошивки: память, файловая система, Wi-Fi, экран и сенсор (BSP Waveshare), лаунчер.
#include <stdbool.h>
#include <stdio.h>
#include "esp_err.h"          // должен идти до заголовков BSP: они используют esp_err_t, но сами его не подключают
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "wifi.h"
#include "audio_esp.h"
#include "buttons_esp.h"
#include "perf_esp.h"
#include "imu_esp.h"
#include "pm_esp.h"
#include "ota_esp.h"
#include "ntp_esp.h"
#include "watch/clock.h"
#include "watch/power.h"
#include "watch/settings.h"
#include "watch/watchface.h"

void watch_launcher_create(void);   // src/launcher.c

esp_err_t bsp_display_panel_power(bool on);   // добавлено в BSP (esp32_s3_touch_amoled_2_06.c)

// Экран «выключен»: яркость 0 и DISPOFF (панель не светит и потребляет меньше). Включение откладываем на ~180 мс:
// пока LVGL перерисовывает кадр порциями по 20 строк, панель остаётся тёмной, и полос от перерисовки не видно.
static bool s_panel_on = true;
static volatile int s_want_pct;
static esp_timer_handle_t s_wake_tmr;

static void wake_cb(void *arg)
{
    (void)arg;
    if (!bsp_display_lock(200)) return;
    if (!s_panel_on) { bsp_display_panel_power(true); s_panel_on = true; }
    bsp_display_brightness_set(s_want_pct);
    bsp_display_unlock();
}

static void set_brightness(int pct)
{
    if (!s_wake_tmr) {
        const esp_timer_create_args_t a = { .callback = wake_cb, .name = "wake" };
        esp_timer_create(&a, &s_wake_tmr);
    }
    s_want_pct = pct;
    if (pct <= 0) {
        if (s_wake_tmr) esp_timer_stop(s_wake_tmr);
        bsp_display_brightness_set(0);
        if (s_panel_on) { bsp_display_panel_power(false); s_panel_on = false; }
        pm_esp_screen_off(true);
        return;
    }
    if (!s_panel_on) {                          // выходим из выключенного состояния: включим после перерисовки
        pm_esp_screen_off(false);
        if (s_wake_tmr && !esp_timer_is_active(s_wake_tmr)) esp_timer_start_once(s_wake_tmr, 180 * 1000);
        return;
    }
    bsp_display_brightness_set(pct);
}

static const char *TAG = "watch";

static void init_nvs(void)
{
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        e = nvs_flash_init();
    }
    ESP_ERROR_CHECK(e);
}

// LittleFS на разделе "storage" монтируется как /flash: там apps/, packages/ и data/ (PIN-хэш).
static bool mount_storage(void)
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/flash",
        .partition_label = "storage",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };
    esp_err_t e = esp_vfs_littlefs_register(&conf);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "LittleFS mount failed: %s", esp_err_to_name(e));
        return false;
    }
    size_t total = 0, used = 0;
    if (esp_littlefs_info("storage", &total, &used) == ESP_OK)
        ESP_LOGI(TAG, "storage: %u of %u bytes used", (unsigned)used, (unsigned)total);
    return true;
}

void app_main(void)
{
    init_nvs();
    bool fs_ok = mount_storage();
    if (fs_ok) settings_init("/flash/data");   // сохранённая сеть нужна Wi-Fi уже при запуске (лаунчер прочитает файл ещё раз)
    wifi_start();                       // подключение идёт в фоне, экран ждать не должен

    // BSP по умолчанию даёт задаче LVGL ~6 КБ стека. Сетевые запросы по HTTPS и разбор JSON
    // выполняются прямо в ней, поэтому стек увеличен (CONFIG_WATCH_LVGL_STACK).
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_DRAW_BUFF_SIZE,
        .double_buffer = true,              // рисуем в один буфер, пока второй уходит на экран по DMA
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
        },
    };
    cfg.lvgl_port_cfg.task_stack = CONFIG_WATCH_LVGL_STACK;
#if CONFIG_WATCH_LCD_FULL_BUFFER
    // Два буфера на весь экран в PSRAM (DMA). Режим LVGL остаётся частичным: прокрутка (весь экран) рисуется
    // за один проход и уходит одной передачей, без полос; мелкие обновления рисуют только свою область.
    cfg.buffer_size = BSP_LCD_H_RES * BSP_LCD_V_RES;
    cfg.flags.buff_spiram = true;
#endif

    ESP_LOGW(TAG, "TRACE 1: display start");
    lv_display_t *disp = bsp_display_start_with_config(&cfg);
    ESP_LOGW(TAG, "TRACE 2: display ok");
    if (!disp) {
        ESP_LOGE(TAG, "display init failed");
        return;
    }
    if (bsp_display_lock(0)) { perf_esp_start(disp); pm_esp_attach_display(disp); bsp_display_unlock(); }   // замер отрисовки, если включено CONFIG_WATCH_PERF_LOG
    // После инициализации BSP яркость равна 0: без явной установки экран остаётся чёрным.
    // Яркостью дальше управляет watch/power (затемнение и выключение при бездействии, меню Display).
    ESP_LOGW(TAG, "TRACE 3: perf ok");
    power_set_backend(set_brightness);
    if (audio_esp_init() != ESP_OK) ESP_LOGW(TAG, "audio disabled");   // динамик и микрофон; без них приложения работают вхолостую
    ESP_LOGW(TAG, "TRACE 4: audio done");
    buttons_esp_start();                // BOOT = назад, PWR = экран вкл/выкл
    ESP_LOGW(TAG, "TRACE 5: buttons ok");
    imu_esp_start();                    // подъём руки будит экран; датчик включается только когда нужен
    ota_esp_start();                    // приём прошивки по воздуху; откат, если новая версия не стартовала
    pm_esp_start();                     // динамическая частота процессора (экономия энергии)

    ESP_LOGW(TAG, "TRACE 6: imu ok");
    ESP_LOGW(TAG, "heap internal: free %u, largest %u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    if (!fs_ok) ESP_LOGW(TAG, "running without storage: no apps, PIN will not be saved");

    if (bsp_display_lock(0)) {
        ESP_LOGW(TAG, "TRACE 7: launcher create");
        watch_launcher_create();
        ESP_LOGW(TAG, "TRACE 8: launcher ok");
        ESP_LOGW(TAG, "heap internal: free %u, largest %u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        clock_init();                                    // время из RTC PCF85063, пояс из настроек
        ESP_LOGW(TAG, "TRACE 9: watchface show");
        watchface_show(lv_screen_active());              // после включения - циферблат
        bsp_display_unlock();
    }
    ntp_esp_start();                    // после clock_init: сверка времени по сети, как только есть IP
}
