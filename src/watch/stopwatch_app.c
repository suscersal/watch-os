// Секундомер: крупное время, кнопки "Круг/Сброс" и "Старт/Стоп", список кругов (лучший - зелёным, худший - красным).
#include "stopwatch_app.h"
#include "haptic.h"
#include "sys_ui.h"
#include "watch_fonts.h"
#include "ws_style.h"
#include <stdio.h>
#include <string.h>

#define W        410
#define ORANGE   0xFF8800
#define LAP_MAX  30
#define MAX_MS   (99u * 3600u * 1000u)     // 99 часов - дальше не считаем

static struct { bool run; uint32_t base_ms, t0; } S;       // base - накоплено до последнего старта
static uint32_t g_lap_total[LAP_MAX];                      // время секундомера в момент каждого круга
static int      g_nlaps;

static lv_obj_t *g_launcher, *scr, *time_lbl, *btn_left, *left_lbl, *btn_main, *main_lbl, *lap_list;
static lv_timer_t *tick;

static uint32_t elapsed(void)
{
    uint32_t e = S.base_ms + (S.run ? lv_tick_elaps(S.t0) : 0);
    return e > MAX_MS ? MAX_MS : e;
}

bool     stopwatch_running(void)    { return S.run; }
uint32_t stopwatch_elapsed_ms(void) { return elapsed(); }

// "MM:SS.cc", а от часа - "H:MM:SS"
static void fmt_big(char *b, size_t n, uint32_t ms)
{
    unsigned h = ms / 3600000, m = ms / 60000 % 60, s = ms / 1000 % 60, cs = ms / 10 % 100;
    if (h) snprintf(b, n, "%u:%02u:%02u", h, m, s);
    else   snprintf(b, n, "%02u:%02u.%02u", m, s, cs);
}

static void fmt_lap(char *b, size_t n, uint32_t ms)       // "M:SS.cc", с часами - "H:MM:SS.cc"
{
    unsigned h = ms / 3600000, m = ms / 60000 % 60, s = ms / 1000 % 60, cs = ms / 10 % 100;
    if (h) snprintf(b, n, "%u:%02u:%02u.%02u", h, m, s, cs);
    else   snprintf(b, n, "%u:%02u.%02u", m, s, cs);
}

static uint32_t lap_len(int i) { return g_lap_total[i] - (i ? g_lap_total[i - 1] : 0); }

static void rebuild_laps(void)
{
    if (!lap_list) return;
    lv_obj_clean(lap_list);
    uint32_t best = 0xFFFFFFFFu, worst = 0;
    for (int i = 0; i < g_nlaps; i++) {
        uint32_t l = lap_len(i);
        if (l < best) best = l;
        if (l > worst) worst = l;
    }
    for (int i = g_nlaps - 1; i >= 0; i--) {                 // новые сверху
        lv_obj_t *row = lv_obj_create(lap_list);
        lv_obj_remove_style_all(row);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(row, lv_pct(100), 44);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(WS_FILL2), 0);

        uint32_t l = lap_len(i);
        uint32_t col = 0xFFFFFF;
        if (g_nlaps >= 3) col = l == best ? WS_GREEN : l == worst ? WS_RED : 0xFFFFFF;

        char b[24];
        snprintf(b, sizeof b, "Круг %d", i + 1);
        lv_obj_t *a = lv_label_create(row);
        lv_label_set_text(a, b);
        lv_obj_set_style_text_font(a, WATCH_FONT_BODY, 0);
        lv_obj_set_style_text_color(a, lv_color_hex(WS_TEXT2), 0);
        lv_obj_align(a, LV_ALIGN_LEFT_MID, 8, 0);

        fmt_lap(b, sizeof b, l);
        lv_obj_t *t = lv_label_create(row);
        lv_label_set_text(t, b);
        lv_obj_set_style_text_font(t, WATCH_FONT_BODY, 0);
        lv_obj_set_style_text_color(t, lv_color_hex(col), 0);
        lv_obj_align(t, LV_ALIGN_RIGHT_MID, -8, 0);
    }
}

static void refresh(void)
{
    if (!scr) return;
    char b[16];
    fmt_big(b, sizeof b, elapsed());
    lv_label_set_text(time_lbl, b);
    lv_label_set_text(main_lbl, S.run ? "Стоп" : "Старт");
    lv_obj_set_style_bg_color(btn_main, lv_color_hex(S.run ? WS_RED : ORANGE), 0);
    lv_obj_set_style_text_color(main_lbl, S.run ? lv_color_white() : lv_color_black(), 0);
    lv_label_set_text(left_lbl, S.run ? "Круг" : "Сброс");
    bool can = S.run ? g_nlaps < LAP_MAX : (S.base_ms > 0);
    lv_obj_set_style_opa(btn_left, can ? LV_OPA_COVER : LV_OPA_40, 0);
}

static void tick_cb(lv_timer_t *t) { LV_UNUSED(t); if (S.run) refresh(); }

static void main_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    haptic_tick();
    if (S.run) {
        S.base_ms = elapsed();
        S.run = false;
        if (tick) lv_timer_pause(tick);
    } else {
        if (S.base_ms >= MAX_MS) return;
        S.t0 = lv_tick_get();
        S.run = true;
        if (tick) lv_timer_resume(tick);
    }
    refresh();
}

static void left_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (S.run) {                                   // круг
        if (g_nlaps >= LAP_MAX) { sys_toast("Кругов максимум"); return; }
        haptic_tick();
        g_lap_total[g_nlaps++] = elapsed();
        rebuild_laps();
    } else if (S.base_ms > 0) {                    // сброс
        haptic_tick();
        S.base_ms = 0;
        g_nlaps = 0;
        rebuild_laps();
    }
    refresh();
}

static void exit_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (tick) { lv_timer_delete(tick); tick = NULL; }
    scr = lap_list = NULL;
    lv_screen_load_anim(g_launcher, sys_exit_anim(), 200, 0, true);
}

static lv_obj_t *pill(lv_obj_t *body, int x, int w, uint32_t bg, uint32_t fg, lv_event_cb_t cb, lv_obj_t **text_out)
{
    lv_obj_t *b = lv_button_create(body);
    lv_obj_set_size(b, w, 65);
    lv_obj_set_pos(b, x, 110);
    lv_obj_set_style_radius(b, 33, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(WS_FILL2), LV_STATE_PRESSED);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, "");
    lv_obj_set_style_text_font(l, WATCH_FONT_BOLD_S, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(fg), 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    if (text_out) *text_out = l;
    return b;
}

void stopwatch_open(lv_obj_t *launcher)
{
    g_launcher = launcher;
    lv_obj_t *body;
    scr = sys_screen_create_root("Секундомер", exit_cb, &body);
    lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(body, LV_LAYOUT_NONE);
    lv_obj_set_style_pad_all(body, 0, 0);

    time_lbl = lv_label_create(body);
    lv_obj_set_width(time_lbl, W);
    lv_obj_set_style_text_font(time_lbl, WATCH_FONT_HUGE, 0);
    lv_obj_set_style_text_color(time_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_align(time_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(time_lbl, 0, 24);

    btn_left = pill(body, 39, 140, WS_FILL, 0xFFFFFF, left_cb, &left_lbl);
    btn_main = pill(body, 191, 180, ORANGE, 0x000000, main_cb, &main_lbl);

    lap_list = lv_obj_create(body);
    lv_obj_remove_style_all(lap_list);
    lv_obj_set_size(lap_list, W - 60, 190);
    lv_obj_set_pos(lap_list, 30, 200);
    lv_obj_set_flex_flow(lap_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(lap_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(lap_list, LV_DIR_VER);

    tick = lv_timer_create(tick_cb, 50, NULL);
    if (!S.run) lv_timer_pause(tick);                       // стоит - экран не перерисовываем
    rebuild_laps();
    refresh();
    lv_screen_load_anim(scr, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
}
