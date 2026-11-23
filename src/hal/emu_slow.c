#ifndef _DEFAULT_SOURCE
  #define _DEFAULT_SOURCE
#endif
#include "emu_slow.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#ifdef _MSC_VER
  #include <Windows.h>
#else
  #include <unistd.h>
#endif

static double   g_us_per_px;
static uint64_t g_t0_us;              // начало отрисовки текущей области (начало прохода или конец предыдущего сброса)

static uint64_t now_us(void)
{
#ifdef _MSC_VER
    return (uint64_t)GetTickCount64() * 1000u;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
#endif
}

static void sleep_us(double us)
{
#ifdef _MSC_VER
    Sleep((DWORD)(us / 1000.0));
#else
    usleep((useconds_t)us);
#endif
}

static void render_start_cb(lv_event_t *e)
{
    (void)e;
    g_t0_us = now_us();
}

// Плата рисует область за время, пропорциональное числу пикселей. Устройство, на котором идёт эмулятор, уже потратило на
// отрисовку своё время, поэтому ждём только недостающее: на медленном телефоне замедление само сходит на нет, а не складывается
// с его собственными тормозами. Если телефон медленнее платы, эмулятор не добавляет ничего.
static void flush_start_cb(lv_event_t *e)
{
    const lv_area_t *a = lv_event_get_param(e);
    if (!a) return;
    double px = (double)lv_area_get_width(a) * (double)lv_area_get_height(a);
    double cost = EMU_US_PER_FLUSH + px * g_us_per_px;
    if (cost > 400000.0) cost = 400000.0;              // страховка от опечатки в переменной
    double spent = (double)(now_us() - g_t0_us);
    if (cost > spent) sleep_us(cost - spent);
    g_t0_us = now_us();                                // следующая область в этом проходе отсчитывается от конца этого сброса
}

void emu_slow_attach(lv_display_t *disp)
{
    const char *k = getenv("WATCH_EMU_SLOW"), *u = getenv("WATCH_EMU_US_PER_PX");
    double mul = k && *k ? atof(k) : 1.0;
    double per = u && *u ? atof(u) : EMU_US_PER_PX;
    if (mul <= 0.0 || per <= 0.0) { printf("Эмулятор: замедление выключено (скорость ПК)\n"); return; }
    g_us_per_px = per * mul;
    lv_display_add_event_cb(disp, render_start_cb, LV_EVENT_RENDER_START, NULL);
    lv_display_add_event_cb(disp, flush_start_cb, LV_EVENT_FLUSH_START, NULL);
    printf("Эмулятор: скорость отрисовки как у платы (полный экран ~%.0f мс). WATCH_EMU_SLOW=0 - без замедления\n",
           EMU_US_PER_FLUSH / 1000.0 + 410.0 * 502.0 * g_us_per_px / 1000.0);
}
