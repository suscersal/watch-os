#include "perf_esp.h"
#include "sdkconfig.h"

#if CONFIG_WATCH_PERF_LOG
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "perf";

#define SCREEN_PX     (410 * 502)
#define FLUSH_COST_US 300.0       // та же постоянная на один сброс, что в эмуляторе (EMU_US_PER_FLUSH)

static int64_t  s_t0;             // начало прохода отрисовки
static uint32_t s_px, s_flushes;  // пикселей и сбросов областей в текущем проходе
// Накопленное за период печати:
static uint32_t s_passes, s_big;
static double   s_big_k_sum;      // сумма оценок мкс/пиксель по крупным проходам
static double   s_big_ms_sum;
static uint32_t s_small_n;
static double   s_small_ms_sum;
static int64_t  s_last_print;

static void render_start_cb(lv_event_t *e) { (void)e; s_t0 = esp_timer_get_time(); s_px = 0; s_flushes = 0; }

static void flush_start_cb(lv_event_t *e)
{
    const lv_area_t *a = lv_event_get_param(e);
    if (!a) return;
    s_px += (uint32_t)(lv_area_get_width(a) * lv_area_get_height(a));
    s_flushes++;
}

// Проход закончен: все области отрисованы и переданы на экран.
static void refr_ready_cb(lv_event_t *e)
{
    (void)e;
    if (s_px == 0) return;
    int64_t now = esp_timer_get_time();
    double us = (double)(now - s_t0);
    s_passes++;
    if (s_px >= SCREEN_PX / 4) {                     // крупный проход: по нему честно считается стоимость пикселя
        s_big++;
        s_big_ms_sum += us / 1000.0;
        s_big_k_sum += (us - FLUSH_COST_US * s_flushes) / (double)s_px;
    } else {
        s_small_n++;
        s_small_ms_sum += us / 1000.0;
    }
    if (now - s_last_print >= 5000000) {
        s_last_print = now;
        if (s_big) ESP_LOGI(TAG, "проходов %u; крупных (>=1/4 экрана) %u: среднее %.0f мс, WATCH_EMU_US_PER_PX=%.3f; мелких %u: среднее %.1f мс",
                            (unsigned)s_passes, (unsigned)s_big, s_big_ms_sum / s_big, s_big_k_sum / s_big,
                            (unsigned)s_small_n, s_small_n ? s_small_ms_sum / s_small_n : 0.0);
        else       ESP_LOGI(TAG, "проходов %u, крупных нет (откройте приложение или пролистайте сетку); мелких %u: среднее %.1f мс",
                            (unsigned)s_passes, (unsigned)s_small_n, s_small_n ? s_small_ms_sum / s_small_n : 0.0);
        s_passes = s_big = s_small_n = 0;
        s_big_k_sum = s_big_ms_sum = s_small_ms_sum = 0.0;
    }
}

void perf_esp_start(lv_display_t *disp)
{
    lv_display_add_event_cb(disp, render_start_cb, LV_EVENT_RENDER_START, NULL);
    lv_display_add_event_cb(disp, flush_start_cb, LV_EVENT_FLUSH_START, NULL);
    lv_display_add_event_cb(disp, refr_ready_cb, LV_EVENT_REFR_READY, NULL);
    ESP_LOGI(TAG, "замер отрисовки включён: смотрите строки с WATCH_EMU_US_PER_PX");
}
#else
void perf_esp_start(lv_display_t *disp) { (void)disp; }
#endif
