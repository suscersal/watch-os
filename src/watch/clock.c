#include "clock.h"
#include "fs.h"
#include "lvgl_inc.h"
#include "plat.h"
#include "settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool     g_set;
static int64_t  g_base_epoch;      // UTC в момент g_base_tick
static uint32_t g_base_tick;
static int      g_tz_min;
static bool     g_approx;          // время восстановлено из файла lasttime
static lv_timer_t *g_save_timer;

#define SAVE_EVERY_MS (15u * 60u * 1000u)      // не чаще: флеш изнашивается, а точность в минуты здесь не нужна
#define EPOCH_MIN     1500000000               // всё, что раньше (2017), считаем неверным временем

static void store_path(char *out, size_t n) { snprintf(out, n, "%s/lasttime", settings_data_root()); }

static int64_t load_saved(void)
{
    char p[96];
    store_path(p, sizeof p);
    char *t = fs_read_file(p, 32);
    if (!t) return 0;
    long long v = atoll(t);
    free(t);
    return v >= EPOCH_MIN ? (int64_t)v : 0;
}

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

static void save_time(void)
{
    if (!g_set) return;
    char p[96], b[24];
    store_path(p, sizeof p);
    snprintf(b, sizeof b, "%lld", (long long)now_utc());
    fs_write_file(p, b, strlen(b));
}

static void save_timer_cb(lv_timer_t *t) { LV_UNUSED(t); save_time(); }

void clock_init(void)
{
    g_tz_min = settings_tz_min();
    int64_t e = 0, saved = load_saved();
    bool rtc = plat_rtc_read(&e) && e >= EPOCH_MIN;
    if (rtc && saved > e + 60) rtc = false;       // RTC откатилось назад (сброс питания): время не может идти назад
    if (rtc) {
        g_base_epoch = e;
        g_base_tick = lv_tick_get();
        g_set = true;
        g_approx = false;
    } else if (saved) {
        g_base_epoch = saved;
        g_base_tick = lv_tick_get();
        g_set = true;
        g_approx = true;
    }
    if (!g_save_timer) g_save_timer = lv_timer_create(save_timer_cb, SAVE_EVERY_MS, NULL);
}

bool clock_is_approx(void) { return g_approx; }

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
    g_approx = false;
    plat_rtc_write(epoch_s);
    save_time();
}

void clock_set_utc(int64_t epoch_s)
{
    g_base_epoch = epoch_s;
    g_base_tick = lv_tick_get();
    g_set = true;
    g_approx = false;
    plat_rtc_write(epoch_s);
    save_time();
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
