// about_app.c - экран «О часах». Значения обновляются раз в 2 секунды, пока экран открыт.
#include "about_app.h"
#include "plat.h"
#include "sys_ui.h"
#include "watch_fonts.h"
#include "ws_style.h"
#include <stdio.h>
#include <string.h>

enum { R_VERSION, R_BUILT, R_PLATFORM, R_SCREEN, R_LVGL, R_UPTIME, R_RAM, R_PSRAM, R_STORE, R_BATT, R_SD, R_NET, R_N };

static lv_obj_t   *g_launcher, *scr, *body;
static lv_obj_t   *g_val[R_N];
static lv_obj_t   *g_row[R_N];
static lv_timer_t *g_tick;

static void back_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_screen_load_anim(g_launcher, sys_exit_anim(), 200, 0, true);
}

static void deleted_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (g_tick) { lv_timer_delete(g_tick); g_tick = NULL; }
    memset(g_val, 0, sizeof g_val);
    memset(g_row, 0, sizeof g_row);
}

static lv_obj_t *add_row(lv_obj_t *card, int idx, const char *title)
{
    lv_obj_t *row = lv_obj_create(card);
    lv_obj_remove_style_all(row);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_t *l = lv_label_create(row);
    lv_label_set_text(l, title);
    lv_obj_set_style_text_color(l, lv_color_hex(WS_TEXT2), 0);
    lv_obj_t *v = lv_label_create(row);
    lv_label_set_long_mode(v, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_max_width(v, lv_pct(62), 0);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(v, lv_color_white(), 0);
    lv_label_set_text(v, "-");
    g_row[idx] = row;
    g_val[idx] = v;
    return v;
}

static void set_val(int idx, const char *t)
{
    if (!g_val[idx]) return;
    if (strcmp(lv_label_get_text(g_val[idx]), t) != 0) lv_label_set_text(g_val[idx], t);
}

// Размер в байтах -> «512 КБ», «3.4 МБ» или «9.4 ГБ».
static void fmt_size(char *b, size_t n, size_t bytes)
{
    if (bytes >= 1024u * 1024u * 1024u)  snprintf(b, n, "%.1f ГБ", (double)bytes / (1024.0 * 1024.0 * 1024.0));
    else if (bytes >= 1024u * 1024u)     snprintf(b, n, "%.1f МБ", (double)bytes / (1024.0 * 1024.0));
    else                                 snprintf(b, n, "%u КБ", (unsigned)(bytes / 1024u));
}

// «свободно / всего»; нет данных - строка скрывается. Подпись про порядок чисел - под карточкой.
static void set_mem(int idx, size_t free_b, size_t total_b)
{
    if (!g_row[idx]) return;
    if (total_b == 0) { lv_obj_add_flag(g_row[idx], LV_OBJ_FLAG_HIDDEN); return; }
    lv_obj_remove_flag(g_row[idx], LV_OBJ_FLAG_HIDDEN);
    char a[24], t[24], b[64];
    fmt_size(a, sizeof a, free_b);
    fmt_size(t, sizeof t, total_b);
    snprintf(b, sizeof b, "%s / %s", a, t);
    set_val(idx, b);
}

static void refresh(void)
{
    plat_sysinfo_t si;
    plat_sysinfo(&si);
    char b[96];

    set_val(R_VERSION, si.version[0] ? si.version : "неизвестно");
    set_val(R_BUILT, si.built[0] ? si.built : "неизвестно");
    set_val(R_PLATFORM, si.platform ? si.platform : "неизвестно");

    uint32_t s = lv_tick_get() / 1000;
    if (s >= 3600) snprintf(b, sizeof b, "%u ч %02u мин", (unsigned)(s / 3600), (unsigned)(s / 60 % 60));
    else           snprintf(b, sizeof b, "%u мин %02u с", (unsigned)(s / 60), (unsigned)(s % 60));
    set_val(R_UPTIME, b);

    set_mem(R_RAM, si.heap_free, si.heap_total);
    set_mem(R_PSRAM, si.psram_free, si.psram_total);
    set_mem(R_STORE, si.store_free, si.store_total);

    plat_batt_info_t bi;
    if (plat_battery_info(&bi)) {
        const char *st = bi.full ? ", заряжены" : bi.charging ? ", заряжается" : bi.vbus ? ", USB" : "";
        snprintf(b, sizeof b, "%d%%%s", bi.pct, st);
    } else snprintf(b, sizeof b, "нет данных");
    set_val(R_BATT, b);

    set_val(R_SD, plat_sd_present() ? "есть" : "нет");

    if (plat_local_ip(b, sizeof b)) set_val(R_NET, b);
    else                            set_val(R_NET, "нет сети");
}

static void tick_cb(lv_timer_t *t) { LV_UNUSED(t); refresh(); }

void about_open(lv_obj_t *launcher)
{
    g_launcher = launcher;
    scr = sys_screen_create_root("О часах", back_cb, &body);
    lv_obj_add_event_cb(scr, deleted_cb, LV_EVENT_DELETE, NULL);
    memset(g_val, 0, sizeof g_val);
    memset(g_row, 0, sizeof g_row);

    lv_obj_t *hero = ws_card(body);
    lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(hero, 4, 0);
    lv_obj_t *name = lv_label_create(hero);
    lv_obj_set_style_text_font(name, WATCH_FONT_BOLD, 0);
    lv_obj_set_style_text_color(name, lv_color_white(), 0);
    lv_label_set_text(name, "watch-os");
    lv_obj_t *sub = lv_label_create(hero);
    lv_obj_set_style_text_font(sub, WATCH_FONT_SMALL, 0);
    lv_obj_set_style_text_color(sub, lv_color_hex(WS_TEXT2), 0);
    lv_label_set_text(sub, "Waveshare ESP32-S3 AMOLED 2.06\"");

    lv_obj_t *fw = ws_card(body);
    lv_obj_set_flex_flow(fw, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(fw, 10, 0);
    add_row(fw, R_VERSION, "Версия");
    add_row(fw, R_BUILT, "Сборка");
    add_row(fw, R_PLATFORM, "Платформа");
    add_row(fw, R_SCREEN, "Экран");
    add_row(fw, R_LVGL, "LVGL");
    char st[32];
    snprintf(st, sizeof st, "%d x %d", (int)lv_display_get_horizontal_resolution(NULL), (int)lv_display_get_vertical_resolution(NULL));
    set_val(R_SCREEN, st);
    snprintf(st, sizeof st, "%d.%d.%d", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    set_val(R_LVGL, st);

    lv_obj_t *st_card = ws_card(body);
    lv_obj_set_flex_flow(st_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(st_card, 10, 0);
    add_row(st_card, R_UPTIME, "Работает");
    add_row(st_card, R_RAM, "Память");
    add_row(st_card, R_PSRAM, "PSRAM");
    add_row(st_card, R_STORE, "Хранилище");
    add_row(st_card, R_BATT, "Заряд");
    add_row(st_card, R_SD, "microSD");
    add_row(st_card, R_NET, "Адрес");

    lv_obj_t *hint = lv_label_create(body);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, WATCH_FONT_SMALL, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(WS_TEXT2), 0);
    lv_label_set_text(hint, "Память, PSRAM и хранилище: свободно / всего.");

    refresh();
    g_tick = lv_timer_create(tick_cb, 2000, NULL);
    lv_screen_load_anim(scr, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 200, 0, false);   // лаунчер не удаляем
}
