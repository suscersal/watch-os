#include "ws_style.h"
#include "clock.h"
#include "watch_fonts.h"
#include <stdio.h>

static uint32_t g_accent = WS_RED;

void     ws_set_accent(uint32_t rgb) { g_accent = rgb; }
uint32_t ws_accent(void)             { return g_accent; }

bool ws_now(struct tm *out)
{
    if (clock_local(out)) return true;
    time_t t = time(NULL);
    if (t < 1700000000) return false;        // системные часы не заданы (ESP32 до синхронизации)
    localtime_r(&t, out);
    return true;
}

static void time_update(lv_timer_t *t)
{
    lv_obj_t *l = lv_timer_get_user_data(t);
    struct tm tm;
    if (ws_now(&tm)) {
        char b[8];
        snprintf(b, sizeof b, "%02d:%02d", tm.tm_hour, tm.tm_min);
        lv_label_set_text(l, b);
    } else {
        lv_label_set_text(l, "");
    }
}

static void time_deleted(lv_event_t *e)
{
    lv_timer_t *t = lv_event_get_user_data(e);
    lv_timer_delete(t);
}

lv_obj_t *ws_time_label(lv_obj_t *parent)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, WATCH_FONT_LARGE, 0);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_label_set_text(l, "");
    lv_timer_t *t = lv_timer_create(time_update, 1000, l);
    lv_obj_add_event_cb(l, time_deleted, LV_EVENT_DELETE, t);
    time_update(t);
    return l;
}

lv_obj_t *ws_card(lv_obj_t *parent)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(c, lv_pct(100));
    lv_obj_set_height(c, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(c, lv_color_hex(WS_CARD), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, 22, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 14, 0);
    lv_obj_set_style_shadow_width(c, 0, 0);
    return c;
}

lv_obj_t *ws_round_button(lv_obj_t *parent, int d, const char *text, uint32_t bg, uint32_t fg,
                          const lv_font_t *font, lv_event_cb_t cb, void *ud)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, d, d);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(WS_FILL2), LV_STATE_PRESSED);
    if (text) {
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, text);
        lv_obj_set_style_text_color(l, lv_color_hex(fg), 0);
        if (font) lv_obj_set_style_text_font(l, font, 0);
        lv_obj_center(l);
    }
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    return b;
}

void ws_style_dropdown(lv_obj_t *dd)
{
    lv_obj_set_style_bg_color(dd, lv_color_hex(WS_CARD), 0);
    lv_obj_set_style_text_color(dd, lv_color_white(), 0);
    lv_obj_set_style_border_width(dd, 0, 0);
    lv_obj_set_style_radius(dd, 18, 0);
    lv_obj_set_style_pad_all(dd, 14, 0);
    lv_obj_set_style_shadow_width(dd, 0, 0);
    lv_obj_t *list = lv_dropdown_get_list(dd);
    if (list) {
        lv_obj_set_style_bg_color(list, lv_color_hex(WS_FILL2), 0);
        lv_obj_set_style_text_color(list, lv_color_white(), 0);
        lv_obj_set_style_border_width(list, 0, 0);
        lv_obj_set_style_radius(list, 18, 0);
        lv_obj_set_style_bg_color(list, lv_color_hex(WS_FILL2), LV_PART_SELECTED | LV_STATE_CHECKED);
        lv_obj_set_style_text_font(list, WATCH_FONT_BODY, 0);
    }
}
