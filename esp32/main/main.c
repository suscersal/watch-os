// main.c - точка входа прошивки: память, файловая система, Wi-Fi, экран и сенсор (BSP Waveshare), лаунчер.
#include <stdbool.h>
#include <stdio.h>
#include "esp_err.h"          // должен идти до заголовков BSP: они используют esp_err_t, но сами его не подключают
#include "esp_littlefs.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "wifi.h"

void watch_launcher_create(void);   // src/launcher.c

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
    wifi_start();                       // подключение идёт в фоне, экран ждать не должен

    // BSP по умолчанию даёт задаче LVGL ~6 КБ стека. Сетевые запросы по HTTPS и разбор JSON
    // выполняются прямо в ней, поэтому стек увеличен (CONFIG_WATCH_LVGL_STACK).
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_DRAW_BUFF_SIZE,
        .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
        },
    };
    cfg.lvgl_port_cfg.task_stack = CONFIG_WATCH_LVGL_STACK;

    lv_display_t *disp = bsp_display_start_with_config(&cfg);
    if (!disp) {
        ESP_LOGE(TAG, "display init failed");
        return;
    }
    // После инициализации BSP яркость равна 0: без этого вызова экран остаётся чёрным.
    bsp_display_brightness_set(CONFIG_WATCH_BRIGHTNESS);

    if (!fs_ok) ESP_LOGW(TAG, "running without storage: no apps, PIN will not be saved");

    if (bsp_display_lock(0)) {
        watch_launcher_create();
        bsp_display_unlock();
    }
}
