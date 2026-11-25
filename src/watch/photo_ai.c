#include "photo_ai.h"
#include "ai.h"
#include "photos.h"
#include "sys_ui.h"
#include "watch_fonts.h"
#include "ws_style.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ANSWER_MAX 1024

static lv_obj_t *g_prev, *g_scr, *g_ta, *g_answer;
static uint32_t  g_id;

static void gone_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    sys_keyboard_hide();
    g_scr = g_ta = g_answer = NULL;
}

static void back_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    sys_keyboard_hide();
    lv_screen_load_anim(g_prev, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, 200, 0, true);   // экран ИИ удаляется
}

static void ta_cb(lv_event_t *e)
{
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_FOCUSED || c == LV_EVENT_CLICKED) sys_keyboard_attach(lv_event_get_target_obj(e));
}

static void send(const char *q)
{
    if (!g_scr) return;
    size_t n = 0;
    uint8_t *jpg = photos_load(g_id, false, &n);       // полный кадр (до 256 КБ): мелкий текст на миниатюре не прочитать
    if (!jpg) { lv_label_set_text(g_answer, "Cannot read the photo"); return; }

    sys_keyboard_hide();
    lv_label_set_text(g_answer, "Asking AI...");
    lv_refr_now(NULL);                                  // запрос блокирующий: сначала рисуем статус

    char out[ANSWER_MAX], err[96];
    if (ai_ask_image(q, jpg, n, out, sizeof out, err, sizeof err)) lv_label_set_text(g_answer, out);
    else {
        char t[128];
        snprintf(t, sizeof t, "Error: %s", err);
        lv_label_set_text(g_answer, t);
    }
    free(jpg);
}

static void send_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    send(lv_textarea_get_text(g_ta));
}

void photo_ai_test_send(const char *question) { send(question); }

void photo_ai_open(uint32_t photo_id)
{
    g_id = photo_id;
    g_prev = lv_screen_active();
    lv_obj_t *body;
    g_scr = sys_screen_create("Ask AI", back_cb, &body);
    lv_obj_add_event_cb(g_scr, gone_cb, LV_EVENT_DELETE, NULL);

    g_ta = lv_textarea_create(body);
    lv_textarea_set_one_line(g_ta, true);
    lv_textarea_set_max_length(g_ta, 200);
    lv_textarea_set_text(g_ta, "What is in this photo?");
    lv_obj_set_width(g_ta, lv_pct(100));
    lv_obj_set_style_bg_color(g_ta, lv_color_hex(WS_CARD), 0);
    lv_obj_set_style_text_color(g_ta, lv_color_white(), 0);
    lv_obj_set_style_border_width(g_ta, 0, 0);
    lv_obj_set_style_radius(g_ta, 20, 0);
    lv_obj_set_style_pad_all(g_ta, 14, 0);
    lv_obj_add_event_cb(g_ta, ta_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *b = lv_button_create(body);
    lv_obj_set_size(b, lv_pct(100), 52);
    lv_obj_set_style_radius(b, 26, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(ws_accent()), 0);
    lv_obj_t *bl = lv_label_create(b);
    lv_label_set_text(bl, "Send photo");
    lv_obj_set_style_text_color(bl, lv_color_white(), 0);
    lv_obj_center(bl);
    lv_obj_add_event_cb(b, send_cb, LV_EVENT_CLICKED, NULL);

    g_answer = lv_label_create(body);
    lv_label_set_long_mode(g_answer, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_answer, lv_pct(100));
    lv_obj_set_style_text_color(g_answer, lv_color_white(), 0);
    lv_label_set_text(g_answer, "");

    lv_screen_load_anim(g_scr, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 200, 0, false);   // просмотр не удаляем
}
