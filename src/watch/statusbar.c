#include "statusbar.h"
#include "clock.h"
#include "plat.h"
#include "power.h"
#include "watch_fonts.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    lv_obj_t   *time, *wifi, *batt;
    lv_timer_t *timer;
    bool        show_time;
} sb_t;

static void refresh(sb_t *s)
{
    if (power_screen_off()) return;      // экран выключен: ничего не перерисовываем

    if (s->show_time) {
        struct tm t;
        char b[8] = "--:--";
        if (clock_local(&t)) snprintf(b, sizeof b, "%02d:%02d", t.tm_hour, t.tm_min);
        if (strcmp(lv_label_get_text(s->time), b) != 0) lv_label_set_text(s->time, b);
    }

    char ip[40];
    lv_obj_set_style_text_color(s->wifi, plat_local_ip(ip, sizeof ip) ? lv_color_white() : lv_color_hex(0x4B5563), 0);

    int pct; bool chg;
    if (plat_battery(&pct, &chg)) {
        const char *sym = chg ? LV_SYMBOL_CHARGE
                        : pct > 85 ? LV_SYMBOL_BATTERY_FULL : pct > 60 ? LV_SYMBOL_BATTERY_3
                        : pct > 35 ? LV_SYMBOL_BATTERY_2 : pct > 10 ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY;
        char b[24];
        snprintf(b, sizeof b, "%s %d%%", sym, pct);
        if (strcmp(lv_label_get_text(s->batt), b) != 0) lv_label_set_text(s->batt, b);
        lv_obj_set_style_text_color(s->batt, chg ? lv_color_hex(0x22C55E) : pct <= 15 ? lv_color_hex(0xEF4444) : lv_color_white(), 0);
        lv_obj_remove_flag(s->batt, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s->batt, LV_OBJ_FLAG_HIDDEN);    // батареи нет или не читается: не рисуем неправду
    }
}

static void timer_cb(lv_timer_t *t) { refresh(lv_timer_get_user_data(t)); }

static void deleted_cb(lv_event_t *e)
{
    sb_t *s = lv_event_get_user_data(e);
    lv_timer_delete(s->timer);
    free(s);
}

static lv_obj_t *mk_label(lv_obj_t *row, const char *text)
{
    lv_obj_t *l = lv_label_create(row);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, WATCH_FONT_SMALL, 0);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    return l;
}

lv_obj_t *statusbar_create(lv_obj_t *parent, bool show_time)
{
    sb_t *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->show_time = show_time;

    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row, LV_SIZE_CONTENT, 22);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 8);

    if (show_time) s->time = mk_label(row, "--:--");
    s->wifi = mk_label(row, LV_SYMBOL_WIFI);
    s->batt = mk_label(row, "");
    s->timer = lv_timer_create(timer_cb, 1000, s);
    lv_obj_add_event_cb(row, deleted_cb, LV_EVENT_DELETE, s);
    refresh(s);
    return row;
}
