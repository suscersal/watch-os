// haptic_esp.c - вибромотор на GPIO18 (как в заводском примере Waveshare, компонент VibrationTest): выход без ШИМ,
// 1 - включено. Узор крутится на таймере esp_timer, поэтому вызвавшая задача не ждёт, а мотор не может залипнуть:
// каждый импульс короче MAX_ON_MS, а после последнего шага выход всегда ставится в 0.
#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "watch/plat.h"

#define MOTOR_GPIO GPIO_NUM_18
#define MAX_ON_MS  250
#define MAX_STEPS  16

static const char *TAG = "haptic";

static esp_timer_handle_t s_timer;
static bool               s_ready, s_failed;
static uint16_t           s_pat[MAX_STEPS];
static int                s_n, s_i;
static portMUX_TYPE       s_mux = portMUX_INITIALIZER_UNLOCKED;

// Следующий шаг узора: чётные - включено, нечётные - пауза; после последнего мотор выключается и таймер не взводится.
static void step_cb(void *arg)
{
    (void)arg;
    int level = 0;
    uint32_t ms = 0;
    portENTER_CRITICAL(&s_mux);
    if (s_i < s_n) {
        level = (s_i % 2 == 0);
        ms = s_pat[s_i];
        s_i++;
    }
    portEXIT_CRITICAL(&s_mux);
    gpio_set_level(MOTOR_GPIO, level);
    if (ms) esp_timer_start_once(s_timer, (uint64_t)ms * 1000u);
}

static bool setup(void)
{
    if (s_ready) return true;
    if (s_failed) return false;
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << MOTOR_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    const esp_timer_create_args_t ta = { .callback = step_cb, .name = "haptic" };
    if (gpio_config(&cfg) != ESP_OK || esp_timer_create(&ta, &s_timer) != ESP_OK) {
        s_failed = true;                       // не мучаем пин при каждом вызове
        ESP_LOGE(TAG, "motor GPIO%d setup failed, haptics disabled", (int)MOTOR_GPIO);
        return false;
    }
    gpio_set_level(MOTOR_GPIO, 0);
    s_ready = true;
    return true;
}

void plat_haptic(const uint16_t *pattern_ms, int n)
{
    if (!pattern_ms || n <= 0 || !setup()) return;
    if (n > MAX_STEPS) n = MAX_STEPS;
    esp_timer_stop(s_timer);                   // прежний узор прерываем (ошибку "не запущен" игнорируем)
    portENTER_CRITICAL(&s_mux);
    for (int i = 0; i < n; i++) s_pat[i] = (i % 2 == 0 && pattern_ms[i] > MAX_ON_MS) ? MAX_ON_MS : pattern_ms[i];
    s_n = n;
    s_i = 0;
    portEXIT_CRITICAL(&s_mux);
    step_cb(NULL);
}
