#include "clock.h"
#include "lvgl_inc.h"
#include <stdio.h>

static bool     g_set;
static int64_t  g_base_epoch;
static uint32_t g_base_tick;
static int      g_tz_min;

void clock_set(int64_t epoch_s, int tz_offset_min)
{
    g_base_epoch = epoch_s;
    g_base_tick = lv_tick_get();
    g_tz_min = tz_offset_min;
    g_set = true;
}

bool clock_is_set(void) { return g_set; }

bool clock_local(struct tm *out)
{
    if (!g_set) return false;
    int64_t now = g_base_epoch + (int64_t)lv_tick_elaps(g_base_tick) / 1000 + (int64_t)g_tz_min * 60;
    time_t t = (time_t)now;
    return gmtime_r(&t, out) != NULL;   // смещение уже добавлено, поэтому gmtime
}

bool clock_format_epoch(int64_t epoch_s, char *out, unsigned n)
{
    time_t t = (time_t)(epoch_s + (g_set ? (int64_t)g_tz_min * 60 : 0));
    struct tm tmv;
    if (!gmtime_r(&t, &tmv)) return false;
    snprintf(out, n, "%04d-%02d-%02d %02d:%02d%s", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min, g_set ? "" : " UTC");
    return true;
}
