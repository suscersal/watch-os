#include "messages_app.h"
#include "clock.h"
#include "messages.h"
#include "sys_ui.h"
#include "watch_fonts.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *g_launcher, *g_list_scr, *g_list_body;

static void list_deleted_cb(lv_event_t *e) { LV_UNUSED(e); g_list_scr = g_list_body = NULL; }

static void back_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_screen_load_anim(g_launcher, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, 200, 0, true);
}

static void detail_back_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_screen_load_anim(g_list_scr, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, 200, 0, true);   // удаляется экран сообщения
}

static lv_obj_t *label(lv_obj_t *p, const char *t, uint32_t color, const lv_font_t *font)
{
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_text(l, t);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    if (font) lv_obj_set_style_text_font(l, font, 0);
    return l;
}

static void build_list(void);
static void build_async(void *ud) { LV_UNUSED(ud); build_list(); }   // кнопка, чей обработчик работает, не удаляем из него же

static void item_cb(lv_event_t *e)
{
    size_t i = (size_t)(uintptr_t)lv_event_get_user_data(e);
    const msg_t *m = messages_get(i);
    if (!m) return;
    messages_mark_read(i);

    lv_obj_t *body;
    lv_obj_t *scr = sys_screen_create(m->app[0] ? m->app : "Message", detail_back_cb, &body);
    if (m->ts) {
        char tb[32];
        clock_format_epoch(m->ts, tb, sizeof tb);
        label(body, tb, 0x9CA3AF, WATCH_FONT_SMALL);
    }
    if (m->title[0]) label(body, m->title, 0xFFFFFF, WATCH_FONT_LARGE);
    label(body, m->text[0] ? m->text : "(no text)", 0xD1D5DB, NULL);
    lv_screen_load_anim(scr, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 200, 0, false);   // список не удаляем
    lv_async_call(build_async, NULL);   // пометка «прочитано» видна при возврате
}

static void clear_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    messages_clear();
    lv_async_call(build_async, NULL);
}

static void build_list(void)
{
    if (!g_list_body) return;
    lv_obj_clean(g_list_body);
    size_t n = messages_count();
    if (n == 0) { label(g_list_body, "No messages", 0x9CA3AF, NULL); return; }
    for (size_t i = 0; i < n; i++) {
        const msg_t *m = messages_get(i);
        lv_obj_t *b = lv_button_create(g_list_body);
        lv_obj_set_size(b, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_style_radius(b, 14, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(m->read ? 0x111827 : 0x1F2937), 0);
        lv_obj_set_style_pad_all(b, 10, 0);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(b, 2, 0);
        char head[80];
        snprintf(head, sizeof head, "%s%s", m->read ? "" : LV_SYMBOL_BULLET " ", m->app[0] ? m->app : "Message");
        lv_obj_t *a = label(b, head, m->read ? 0x6B7280 : 0x22C55E, WATCH_FONT_SMALL);
        lv_label_set_long_mode(a, LV_LABEL_LONG_DOT);
        lv_obj_t *t = label(b, m->title[0] ? m->title : m->text, 0xFFFFFF, NULL);
        lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
        lv_obj_set_height(t, 26);
        lv_obj_add_event_cb(b, item_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
    }
    lv_obj_t *c = lv_button_create(g_list_body);
    lv_obj_set_size(c, lv_pct(100), 48);
    lv_obj_set_style_radius(c, 14, 0);
    lv_obj_set_style_shadow_width(c, 0, 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(0x6B7280), 0);
    lv_obj_t *cl = lv_label_create(c);
    lv_label_set_text(cl, "Clear all");
    lv_obj_center(cl);
    lv_obj_add_event_cb(c, clear_cb, LV_EVENT_CLICKED, NULL);
}

void messages_open(lv_obj_t *launcher)
{
    g_launcher = launcher;
    g_list_scr = sys_screen_create_root("Messages", back_cb, &g_list_body);
    lv_obj_add_event_cb(g_list_scr, list_deleted_cb, LV_EVENT_DELETE, NULL);
    build_list();
    lv_screen_load_anim(g_list_scr, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
}
