// pm_esp.c - динамическая частота процессора. Пока никто не держит блокировку (идёт только ожидание), частота падает
// до WATCH_CPU_MIN_MHZ; драйверы (QSPI экрана, I2S, Wi-Fi) сами поднимают её на время работы. Лёгкий сон (light sleep)
// по умолчанию выключен: на этой плате он не проверялся и может ломать касание и звук (CONFIG_WATCH_LIGHT_SLEEP).
#include "pm_esp.h"
#include "esp_log.h"
#include "sdkconfig.h"
#if CONFIG_PM_ENABLE && CONFIG_WATCH_POWER_SAVE
#include "esp_pm.h"
#endif

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
