// buttons_test_app.c - тест кнопок корпуса. На экране две карточки (BOOT и PWR): при коротком нажатии карточка
// на 0.4 с загорается зелёным, счётчик растёт. Пока приложение открыто, кнопки не выходят из приложения и не гасят
// экран (см. buttons_set_test_hook). Выход - свайп вправо или вверх. Долгое нажатие PWR выключает питание аппаратно.
#include "buttons_test_app.h"
#include "buttons.h"
#include "sys_ui.h"
#include "watch_fonts.h"
#include "ws_style.h"
#include <stdio.h>

#define LIT_MS 400

typedef struct { lv_obj_t *card, *state, *cnt; unsigned n; uint32_t t; bool lit; } cell_t;

static lv_obj_t *g_launcher, *scr;
static cell_t cell[2];
static lv_timer_t *tick;

static void set_lit(cell_t *c, bool on)
{
    c->lit = on;
    lv_obj_set_style_bg_color(c->card, lv_color_hex(on ? 0x1F7A3A : WS_CARD), 0);
    lv_label_set_text(c->state, on ? "НАЖАТА" : "ждёт нажатия");
    lv_obj_set_style_text_color(c->state, lv_color_hex(on ? WS_GREEN : WS_TEXT2), 0);
}

static void on_button(btn_id_t b)            // вызывается из потока LVGL (под bsp_display_lock)
{
    if (!scr || (int)b < 0 || b > BTN_PWR) return;
    cell_t *c = &cell[b];
    c->n++;
    char s[24];
    snprintf(s, sizeof s, "%u", c->n);
    lv_label_set_text(c->cnt, s);
    c->t = lv_tick_get();
    set_lit(c, true);
}

static void tick_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (!scr) return;
    for (int i = 0; i < 2; i++)
        if (cell[i].lit && lv_tick_elaps(cell[i].t) >= LIT_MS) set_lit(&cell[i], false);
}

static void deleted_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    buttons_set_test_hook(NULL);              // кнопки снова работают как обычно
    if (tick) { lv_timer_delete(tick); tick = NULL; }
    scr = NULL;
    for (int i = 0; i < 2; i++) cell[i] = (cell_t){0};
}

static void back_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_screen_load_anim(g_launcher, sys_exit_anim(), 200, 0, true);
}

static void make_cell(lv_obj_t *body, int i, const char *title)
{
    cell_t *c = &cell[i];
    c->n = 0;
    c->card = lv_obj_create(body);
    lv_obj_remove_flag(c->card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(c->card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(c->card, lv_pct(100), 120);
    lv_obj_set_style_radius(c->card, 28, 0);
    lv_obj_set_style_border_width(c->card, 0, 0);
    lv_obj_set_style_pad_all(c->card, 10, 0);
    lv_obj_set_style_pad_row(c->card, 2, 0);
    lv_obj_set_flex_flow(c->card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c->card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *t = lv_label_create(c->card);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_font(t, WATCH_FONT_BOLD_S, 0);
    lv_obj_set_style_text_color(t, lv_color_white(), 0);

    c->cnt = lv_label_create(c->card);
    lv_label_set_text(c->cnt, "0");
    lv_obj_set_style_text_font(c->cnt, WATCH_FONT_LARGE, 0);
    lv_obj_set_style_text_color(c->cnt, lv_color_white(), 0);

    c->state = lv_label_create(c->card);
    lv_obj_set_style_text_font(c->state, WATCH_FONT_SMALL, 0);
    set_lit(c, false);
}

void buttons_test_open(lv_obj_t *launcher)
{
    g_launcher = launcher;
    lv_obj_t *body;
    scr = sys_screen_create_root("Кнопки", back_cb, &body);
    lv_obj_add_event_cb(scr, deleted_cb, LV_EVENT_DELETE, NULL);

    make_cell(body, BTN_BOOT, "BOOT");
    make_cell(body, BTN_PWR, "PWR");

    lv_obj_t *hint = lv_label_create(body);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, lv_pct(100));
    lv_label_set_text(hint, "Жмите короткими нажатиями. Здесь кнопки не закрывают приложение и не гасят экран. Выход: свайп вправо.");
    lv_obj_set_style_text_font(hint, WATCH_FONT_SMALL, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(WS_TEXT2), 0);

    tick = lv_timer_create(tick_cb, 100, NULL);
    buttons_set_test_hook(on_button);

    lv_screen_load_anim(scr, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 200, 0, false);   // лаунчер не удаляем
}
