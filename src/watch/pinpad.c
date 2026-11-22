#include "pinpad.h"
#include "ws_style.h"
#include "auth.h"
#include "watch_fonts.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { KEY_BACK = 10, KEY_OK = 11 };

typedef struct {
    lv_obj_t *title, *dots, *msg;
    lv_obj_t *keys[12];
    char      pin[AUTH_PIN_MAX + 1];
    int       n, auto_len;
    bool      enabled;
    pinpad_cb cb;
    void     *ud;
} pad_t;

static pad_t *pad_of(lv_obj_t *pad) { return lv_obj_get_user_data(pad); }

static void update_dots(pad_t *p)
{
    char buf[AUTH_PIN_MAX * 4 + 1] = "";
    for (int i = 0; i < p->n; i++) strcat(buf, i ? " \xE2\x80\xA2" : "\xE2\x80\xA2");   // •
    lv_label_set_text(p->dots, buf[0] ? buf : " ");
}

static void wipe(pad_t *p)
{
    memset(p->pin, 0, sizeof p->pin);
    p->n = 0;
    update_dots(p);
}

static void submit(pad_t *p)
{
    char pin[AUTH_PIN_MAX + 1];
    memcpy(pin, p->pin, sizeof pin);
    pinpad_cb cb = p->cb;
    void *ud = p->ud;
    wipe(p);                       // дальше p может быть удалён внутри cb
    if (cb) cb(pin, ud);
    memset(pin, 0, sizeof pin);
}

static void key_cb(lv_event_t *e)
{
    pad_t *p = lv_event_get_user_data(e);
    int key = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    if (!p->enabled) return;

    if (key == KEY_BACK) {
        if (p->n > 0) p->pin[--p->n] = '\0';
        update_dots(p);
    } else if (key == KEY_OK) {
        if (p->n >= AUTH_PIN_MIN) submit(p);
        else pinpad_set_message(lv_obj_get_parent(p->title), "At least 4 digits", 0xEF4444);
    } else if (p->n < AUTH_PIN_MAX) {
        p->pin[p->n++] = (char)('0' + key);
        update_dots(p);
        if (p->auto_len > 0 && p->n == p->auto_len) submit(p);
    }
}

static void root_deleted_cb(lv_event_t *e)
{
    pad_t *p = lv_event_get_user_data(e);
    memset(p->pin, 0, sizeof p->pin);
    free(p);
}

lv_obj_t *pinpad_create(lv_obj_t *parent, int auto_len, pinpad_cb cb, void *ud)
{
    pad_t *p = calloc(1, sizeof *p);
    p->auto_len = auto_len; p->cb = cb; p->ud = ud; p->enabled = true;

    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(root, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(root, 6, 0);
    lv_obj_set_user_data(root, p);
    lv_obj_add_event_cb(root, root_deleted_cb, LV_EVENT_DELETE, p);

    p->title = lv_label_create(root);
    lv_obj_set_style_text_font(p->title, WATCH_FONT_LARGE, 0);
    lv_obj_set_style_text_color(p->title, lv_color_white(), 0);
    lv_label_set_text(p->title, "");

    p->dots = lv_label_create(root);
    lv_obj_set_style_text_font(p->dots, WATCH_FONT_LARGE, 0);
    lv_obj_set_style_text_color(p->dots, lv_color_white(), 0);
    lv_obj_set_height(p->dots, 40);
    update_dots(p);

    p->msg = lv_label_create(root);
    lv_label_set_long_mode(p->msg, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(p->msg, lv_pct(90));
    lv_obj_set_style_text_align(p->msg, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_min_height(p->msg, 26, 0);
    lv_label_set_text(p->msg, "");

    static const char *labels[12] = {"1","2","3","4","5","6","7","8","9", LV_SYMBOL_BACKSPACE, "0", LV_SYMBOL_OK};
    static const int ids[12] = {1,2,3,4,5,6,7,8,9, KEY_BACK, 0, KEY_OK};
    for (int r = 0; r < 4; r++) {
        lv_obj_t *row = lv_obj_create(root);
        lv_obj_remove_style_all(row);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 14, 0);
        for (int c = 0; c < 3; c++) {
            int i = r * 3 + c;
            lv_obj_t *b = lv_button_create(row);
            lv_obj_set_size(b, 74, 74);
            lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_shadow_width(b, 0, 0);
            lv_obj_set_style_bg_color(b, lv_color_hex(i == 11 ? WS_GREEN : WS_FILL), 0);
            lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(b, lv_color_hex(WS_FILL2), LV_STATE_PRESSED);
            lv_obj_t *l = lv_label_create(b);
            lv_label_set_text(l, labels[i]);
            lv_obj_set_style_text_font(l, WATCH_FONT_LARGE, 0);
            lv_obj_center(l);
            lv_obj_set_user_data(b, (void *)(intptr_t)ids[i]);
            lv_obj_add_event_cb(b, key_cb, LV_EVENT_CLICKED, p);
            p->keys[i] = b;
        }
    }
    return root;
}

void pinpad_set_title(lv_obj_t *pad, const char *t) { lv_label_set_text(pad_of(pad)->title, t); }

void pinpad_set_message(lv_obj_t *pad, const char *msg, uint32_t color)
{
    pad_t *p = pad_of(pad);
    lv_label_set_text(p->msg, msg);
    lv_obj_set_style_text_color(p->msg, lv_color_hex(color), 0);
}

void pinpad_set_enabled(lv_obj_t *pad, bool en)
{
    pad_t *p = pad_of(pad);
    p->enabled = en;
    for (int i = 0; i < 12; i++) {
        if (en) lv_obj_remove_state(p->keys[i], LV_STATE_DISABLED);
        else    lv_obj_add_state(p->keys[i], LV_STATE_DISABLED);
    }
    if (!en) wipe(p);
}

void pinpad_clear(lv_obj_t *pad) { wipe(pad_of(pad)); }
