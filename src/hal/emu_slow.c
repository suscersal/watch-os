#ifndef _DEFAULT_SOURCE
  #define _DEFAULT_SOURCE
#endif
#include "emu_slow.h"
#include <stdio.h>
#include <stdlib.h>
#ifdef _MSC_VER
  #include <Windows.h>
#else
  #include <unistd.h>
#endif

static double g_us_per_px;

static void flush_start_cb(lv_event_t *e)
{
    const lv_area_t *a = lv_event_get_param(e);
    if (!a) return;
    double px = (double)lv_area_get_width(a) * (double)lv_area_get_height(a);
    double us = 300.0 + px * g_us_per_px;             // 0,3 мс на запуск передачи + пикселы
    if (us > 400000.0) us = 400000.0;                  // страховка от опечатки в переменной
#ifdef _MSC_VER
    Sleep((DWORD)(us / 1000.0));
#else
    usleep((useconds_t)us);
#endif
}

void emu_slow_attach(lv_display_t *disp)
{
    const char *k = getenv("WATCH_EMU_SLOW"), *u = getenv("WATCH_EMU_US_PER_PX");
    double mul = k && *k ? atof(k) : 1.0;
    double per = u && *u ? atof(u) : EMU_US_PER_PX;
    if (mul <= 0.0 || per <= 0.0) { printf("Эмулятор: замедление выключено (скорость ПК)\n"); return; }
    g_us_per_px = per * mul;
    lv_display_add_event_cb(disp, flush_start_cb, LV_EVENT_FLUSH_START, NULL);
    printf("Эмулятор: скорость отрисовки как у платы (полный экран ~%.0f мс). WATCH_EMU_SLOW=0 - без замедления\n",
           300.0 / 1000.0 + 410.0 * 502.0 * g_us_per_px / 1000.0);
}
