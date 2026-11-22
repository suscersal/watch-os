#include "clock.h"
#include "lvgl_inc.h"
#include "plat.h"
#include "settings.h"
#include <stdio.h>

static bool     g_set;
static int64_t  g_base_epoch;      // UTC в момент g_base_tick
static uint32_t g_base_tick;
static int      g_tz_min;

// lv_tick_get() - 32 бита в миллисекундах, переполняется за 49 суток. Раз в сутки (и при каждом чтении) переносим
// целые секунды в g_base_epoch, чтобы разность тиков оставалась маленькой.
static int64_t now_utc(void)
{
    uint32_t el = lv_tick_elaps(g_base_tick);
    uint32_t secs = el / 1000;
    if (secs >= 3600) {
        g_base_epoch += secs;
        g_base_tick += secs * 1000u;
        el -= secs * 1000u;
        secs = 0;
    }
    return g_base_epoch + (int64_t)(el / 1000);
}

void clock_init(void)
{
    g_tz_min = settings_tz_min();
    int64_t e;
    if (plat_rtc_read(&e) && e >= 1500000000) {
        g_base_epoch = e;
        g_base_tick = lv_tick_get();
        g_set = true;
    }
}

void clock_set(int64_t epoch_s, int tz_offset_min)
{
    if (settings_tz_auto()) {
        g_tz_min = tz_offset_min;
        settings_set_tz(true, tz_offset_min);    // запомнить пояс телефона до следующей перезагрузки
    } else {
        g_tz_min = settings_tz_min();
    }
    g_base_epoch = epoch_s;
    g_base_tick = lv_tick_get();
    g_set = true;
    plat_rtc_write(epoch_s);
}

void clock_set_tz(bool is_auto, int tz_offset_min)
{
    settings_set_tz(is_auto, tz_offset_min);
    g_tz_min = settings_tz_min();
}

int  clock_tz_min(void)  { return g_tz_min; }
bool clock_is_set(void)  { return g_set; }

bool clock_utc(int64_t *epoch_s)
{
    if (!g_set) return false;
    *epoch_s = now_utc();
    return true;
}

bool clock_local(struct tm *out)
{
    if (!g_set) return false;
    time_t t = (time_t)(now_utc() + (int64_t)g_tz_min * 60);
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
