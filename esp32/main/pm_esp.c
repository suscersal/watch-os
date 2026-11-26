// pm_esp.c - динамическая частота процессора. Пока никто не держит блокировку (идёт только ожидание), частота падает
// до WATCH_CPU_MIN_MHZ; драйверы (QSPI экрана, I2S, Wi-Fi) сами поднимают её на время работы. Лёгкий сон (light sleep)
// по умолчанию выключен: на этой плате он не проверялся и может ломать касание и звук (CONFIG_WATCH_LIGHT_SLEEP).
#include "pm_esp.h"
#include "esp_log.h"
#include "sdkconfig.h"
#if CONFIG_PM_ENABLE && CONFIG_WATCH_POWER_SAVE
#include "esp_pm.h"

static esp_pm_lock_handle_t s_cpu_lock;
static bool s_cpu_held;

// Задача LVGL работает без блокировок питания, поэтому при DFS отрисовка шла на минимальной частоте
// (полосы на экране, задержка касания). Пока идёт проход отрисовки, держим максимум.
static void render_start_cb(lv_event_t *e)
{
    (void)e;
    if (s_cpu_lock && !s_cpu_held) { esp_pm_lock_acquire(s_cpu_lock); s_cpu_held = true; }
}

static void refr_ready_cb(lv_event_t *e)
{
    (void)e;
    if (s_cpu_lock && s_cpu_held) { s_cpu_held = false; esp_pm_lock_release(s_cpu_lock); }
}
#endif

void pm_esp_attach_display(lv_display_t *disp)
{
#if CONFIG_PM_ENABLE && CONFIG_WATCH_POWER_SAVE
    if (esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "lvgl", &s_cpu_lock) != ESP_OK) return;
    lv_display_add_event_cb(disp, render_start_cb, LV_EVENT_RENDER_START, NULL);
    lv_display_add_event_cb(disp, refr_ready_cb, LV_EVENT_REFR_READY, NULL);
#else
    (void)disp;
#endif
}

void pm_esp_start(void)
{
#if CONFIG_PM_ENABLE && CONFIG_WATCH_POWER_SAVE
    esp_pm_config_t c = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = CONFIG_WATCH_CPU_MIN_MHZ,
#if CONFIG_WATCH_LIGHT_SLEEP
        .light_sleep_enable = true,
#else
        .light_sleep_enable = false,
#endif
    };
    esp_err_t e = esp_pm_configure(&c);
    ESP_LOGI("pm", "DFS %d..%d MHz, light sleep %s: %s", c.min_freq_mhz, c.max_freq_mhz,
             c.light_sleep_enable ? "on" : "off", esp_err_to_name(e));
#else
    ESP_LOGI("pm", "энергосбережение процессора выключено в конфигурации");
#endif
}
