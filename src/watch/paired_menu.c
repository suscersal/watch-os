#include "paired_menu.h"
#include "paired.h"
#include "sync_net.h"
#include "sys_ui.h"
#include "watch_fonts.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *g_from, *g_body;

static void rebuild(void);
static void rebuild_async(void *ud) { LV_UNUSED(ud); rebuild(); }

static lv_obj_t *add_label(const char *text, uint32_t color, const lv_font_t *font)
{
    lv_obj_t *l = lv_label_create(g_body);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    if (font) lv_obj_set_style_text_font(l, font, 0);
    return l;
}

static void back_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_obj_t *to = g_from;
    g_from = g_body = NULL;
    if (to && lv_obj_is_valid(to)) lv_screen_load_anim(to, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, 200, 0, true);
}

static void deleted_cb(lv_event_t *e) { LV_UNUSED(e); g_body = NULL; }

// Забыть телефон: его токен сразу перестаёт работать. Порт закроется, если больше нет причин слушать.
static void forget_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (paired_remove(idx)) sys_toast("Phone forgotten");
    sync_net_notify_apply();
    lv_async_call(rebuild_async, NULL);      // перестроить после выхода из обработчика кнопки
}

static void forget_all_done(bool ok, void *ud)
{
    LV_UNUSED(ud);
    if (!ok) return;
    paired_clear();
    sync_net_notify_apply();
    sys_toast("All phones forgotten");
    lv_async_call(rebuild_async, NULL);
}

static void forget_all_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    sys_confirm("Forget all?", "Every phone will need the code from Sync again.", forget_all_done, NULL);
}

static void rebuild(void)
{
    if (!g_body) return;
    lv_obj_clean(g_body);
    int n = paired_count();
    if (n == 0) {
        add_label("No paired phones. Open Sync and enter the code in the phone app once; after that the phone connects by itself.", 0x9CA3AF, WATCH_FONT_SMALL);
        return;
    }
    add_label("A phone on this list connects without a code: it sends its saved token.", 0x9CA3AF, WATCH_FONT_SMALL);
    for (int i = 0; i < n; i++) {
        char name[PAIRED_NAME_MAX + 1], id[16], idl[24];
        if (!paired_info(i, name, sizeof name)) break;
        if (!paired_id(i, id, sizeof id)) id[0] = '\0';
        snprintf(idl, sizeof idl, "token %s", id);
        lv_obj_t *row = lv_obj_create(g_body);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, lv_pct(100), 58);
        lv_obj_t *l = lv_label_create(row);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        lv_obj_set_width(l, lv_pct(58));
        lv_label_set_text(l, name);
        lv_obj_set_style_text_color(l, lv_color_white(), 0);
        lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, 4);
        lv_obj_t *il = lv_label_create(row);
        lv_label_set_text(il, idl);
        lv_obj_set_style_text_color(il, lv_color_hex(0x9CA3AF), 0);
        lv_obj_set_style_text_font(il, WATCH_FONT_SMALL, 0);
        lv_obj_align(il, LV_ALIGN_TOP_LEFT, 0, 34);
        lv_obj_t *b = lv_button_create(row);
        lv_obj_set_size(b, LV_SIZE_CONTENT, 40);
        lv_obj_set_style_radius(b, 12, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(0xEF4444), 0);
        lv_obj_align(b, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_t *bl = lv_label_create(b);
        lv_label_set_text(bl, LV_SYMBOL_TRASH "  Forget");
        lv_obj_center(bl);
        lv_obj_add_event_cb(b, forget_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    if (n > 1) {
        lv_obj_t *b = lv_button_create(g_body);
        lv_obj_set_size(b, lv_pct(100), 52);
        lv_obj_set_style_radius(b, 14, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x6B7280), 0);
        lv_obj_t *bl = lv_label_create(b);
        lv_label_set_text(bl, "Forget all");
        lv_obj_center(bl);
        lv_obj_add_event_cb(b, forget_all_cb, LV_EVENT_CLICKED, NULL);
    }
}

void paired_menu_open(lv_obj_t *from)
{
    g_from = from;
    lv_obj_t *scr = sys_screen_create("Paired phones", back_cb, &g_body);
    lv_obj_add_event_cb(scr, deleted_cb, LV_EVENT_DELETE, NULL);
    rebuild();
    lv_screen_load_anim(scr, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 200, 0, false);   // родитель не удаляем
}
