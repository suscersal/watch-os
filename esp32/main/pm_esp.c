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

void pm_esp_screen_off(bool off)
{
#if CONFIG_PM_ENABLE && CONFIG_WATCH_POWER_SAVE && CONFIG_WATCH_SCREEN_OFF_SLEEP
    static bool cur;
    if (off == cur) return;
    cur = off;
    int max = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
    if (off && max > 160) max = 160;
#if CONFIG_WATCH_LIGHT_SLEEP
    bool sleep = true;
#else
    bool sleep = false;
#endif
#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
    if (off) sleep = true;
#endif
    esp_pm_config_t c = { .max_freq_mhz = max, .min_freq_mhz = CONFIG_WATCH_CPU_MIN_MHZ, .light_sleep_enable = sleep };
    esp_err_t e = esp_pm_configure(&c);
    ESP_LOGI("pm", "экран %s: DFS %d..%d МГц, light sleep %s: %s", off ? "выкл" : "вкл", c.min_freq_mhz, c.max_freq_mhz,
             sleep ? "on" : "off", esp_err_to_name(e));
    // Касание опрашивается таймером LVGL: при выключенном экране реже, чтобы процессор мог спать.
    for (lv_indev_t *i = lv_indev_get_next(NULL); i; i = lv_indev_get_next(i)) {
        lv_timer_t *t = lv_indev_get_read_timer(i);
        if (t) lv_timer_set_period(t, off ? 100 : 33);
    }
#else
    (void)off;
#endif
}
