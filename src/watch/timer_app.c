// Экран по макету: название и длительность сверху, "ОСТАЛОСЬ", крупные цифры, статус, кнопки "Сброс" и "Пауза",
// прогресс - оранжевая линия по рамке экрана (по часовой стрелке от середины верха).
#include "timer_app.h"
#include "sys_ui.h"
#include "watch_fonts.h"
#include "ws_style.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define W        410
#define H        502
#define FR_R     40        // радиус угла рамки
#define FR_W     5         // толщина рамки
#define ORANGE   0xFF8800

typedef enum { T_IDLE, T_RUN, T_PAUSE, T_DONE } tstate_t;

static const uint32_t presets_ms[] = { 10000, 30000, 60000, 180000, 300000, 600000 };
static const char *const preset_names[] = { "10 сек", "30 сек", "1 мин", "3 мин", "5 мин", "10 мин" };
#define NPRESET 6

// remain_ms - сколько оставалось в момент t0 (для T_RUN); для остальных состояний - текущий остаток
static struct { int preset; tstate_t st; uint32_t remain_ms, t0; } T;

static lv_obj_t *g_launcher, *scr, *frame, *right_lbl, *cap_lbl, *time_lbl, *status_lbl, *btn_reset, *btn_main, *main_lbl;
static lv_timer_t *tick;

static uint32_t total_ms(void) { return presets_ms[T.preset]; }

static uint32_t remaining(void)
{
    if (T.st != T_RUN) return T.remain_ms;
    uint32_t el = lv_tick_get() - T.t0;
    return el >= T.remain_ms ? 0 : T.remain_ms - el;
}

// ---------- рамка: дорожка и прогресс ----------
typedef struct { bool arc; float x1, y1, x2, y2, cx, cy, r; float a0; float len; } seg_t;
static seg_t S[9];
static float S_total;

static void build_path(void)
{
    const float hw = FR_W / 2.0f, r = FR_R - hw, cx = W / 2.0f;
    const float pi2 = 3.14159265f / 2.0f;
    seg_t *s = S;
    *s++ = (seg_t){ false, cx, hw, W - FR_R, hw, 0, 0, 0, 0, W - FR_R - cx };
    *s++ = (seg_t){ true, 0, 0, 0, 0, W - FR_R, FR_R, r, 270, pi2 * r };
    *s++ = (seg_t){ false, W - hw, FR_R, W - hw, H - FR_R, 0, 0, 0, 0, H - 2.0f * FR_R };
    *s++ = (seg_t){ true, 0, 0, 0, 0, W - FR_R, H - FR_R, r, 0, pi2 * r };
    *s++ = (seg_t){ false, W - FR_R, H - hw, FR_R, H - hw, 0, 0, 0, 0, W - 2.0f * FR_R };
    *s++ = (seg_t){ true, 0, 0, 0, 0, FR_R, H - FR_R, r, 90, pi2 * r };
    *s++ = (seg_t){ false, hw, H - FR_R, hw, FR_R, 0, 0, 0, 0, H - 2.0f * FR_R };
    *s++ = (seg_t){ true, 0, 0, 0, 0, FR_R, FR_R, r, 180, pi2 * r };
    *s++ = (seg_t){ false, FR_R, hw, cx, hw, 0, 0, 0, 0, cx - FR_R };
    S_total = 0;
    for (int i = 0; i < 9; i++) S_total += S[i].len;
}

static float progress(void)
{
    if (T.st == T_DONE) return 1.0f;
    uint32_t tot = total_ms();
    return tot ? 1.0f - (float)remaining() / (float)tot : 0.0f;
}

static void frame_draw_cb(lv_event_t *e)
{
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(frame, &a);
    float left = progress() * S_total;
    for (int i = 0; i < 9 && left > 0.01f; i++) {
        const seg_t *s = &S[i];
        float take = left < s->len ? left : s->len;
        left -= take;
        float f = take / s->len;
        if (!s->arc) {
            lv_draw_line_dsc_t ld;
            lv_draw_line_dsc_init(&ld);
            ld.color = lv_color_hex(ORANGE);
            ld.width = FR_W;
            ld.round_start = ld.round_end = 1;
            ld.p1.x = a.x1 + s->x1;                 ld.p1.y = a.y1 + s->y1;
            ld.p2.x = a.x1 + s->x1 + (s->x2 - s->x1) * f;
            ld.p2.y = a.y1 + s->y1 + (s->y2 - s->y1) * f;
            lv_draw_line(layer, &ld);
        } else {
            lv_draw_arc_dsc_t ad;
            lv_draw_arc_dsc_init(&ad);
            ad.color = lv_color_hex(ORANGE);
            ad.width = FR_W;
            ad.rounded = 1;
            ad.center.x = (int32_t)(a.x1 + s->cx);
            ad.center.y = (int32_t)(a.y1 + s->cy);
            ad.radius = (uint16_t)s->r;
            ad.start_angle = (lv_value_precise_t)s->a0;
            ad.end_angle = (lv_value_precise_t)(s->a0 + 90.0f * f);
            lv_draw_arc(layer, &ad);
        }
    }
}

// ---------- обновление экрана ----------
static void refresh(void)
{
    if (!scr) return;
    uint32_t rem = remaining();
    if (T.st == T_RUN && rem == 0) { T.st = T_DONE; T.remain_ms = 0; }
    uint32_t secs = (rem + 999) / 1000;            // округляем вверх: 7 секунд с хвостиком показываем как 00:08, пока не пройдёт
    char b[12];
    snprintf(b, sizeof b, "%02u:%02u", (unsigned)(secs / 60), (unsigned)(secs % 60));
    lv_label_set_text(time_lbl, b);
    lv_label_set_text(right_lbl, preset_names[T.preset]);

    const char *cap = "ОСТАЛОСЬ", *status = "Идёт отсчёт", *main_txt = "Пауза", *reset_txt = "Сброс";
    switch (T.st) {
    case T_IDLE:  cap = "УСТАНОВЛЕНО"; status = "Нажмите на длительность"; main_txt = "Старт"; break;
    case T_RUN:   break;
    case T_PAUSE: status = "Пауза"; main_txt = "Продолжить"; break;
    case T_DONE:  cap = "ГОТОВО"; status = "Время вышло"; main_txt = "Заново"; break;
    }
    lv_label_set_text(cap_lbl, cap);
    lv_label_set_text(status_lbl, status);
    lv_label_set_text(main_lbl, main_txt);
    lv_obj_t *rl = lv_obj_get_child(btn_reset, 0);
    if (rl) lv_label_set_text(rl, reset_txt);
    lv_obj_invalidate(frame);
}

static void tick_cb(lv_timer_t *t) { LV_UNUSED(t); if (T.st == T_RUN) refresh(); }

static void start_from(uint32_t remain)
{
    T.remain_ms = remain;
    T.t0 = lv_tick_get();
    T.st = T_RUN;
}

static void main_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    switch (T.st) {
    case T_IDLE:  start_from(total_ms()); break;
    case T_RUN:   T.remain_ms = remaining(); T.st = T_PAUSE; break;
    case T_PAUSE: start_from(T.remain_ms); break;
    case T_DONE:  start_from(total_ms()); break;
    }
    refresh();
}

static void reset_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    T.st = T_IDLE;
    T.remain_ms = total_ms();
    refresh();
}

static void duration_cb(lv_event_t *e)          // нажатие на длительность вверху: следующая из списка (только пока не запущено)
{
    LV_UNUSED(e);
    if (T.st != T_IDLE && T.st != T_DONE) return;
    T.preset = (T.preset + 1) % NPRESET;
    T.st = T_IDLE;
    T.remain_ms = total_ms();
    refresh();
}

static void exit_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (tick) { lv_timer_delete(tick); tick = NULL; }
    scr = NULL;
    lv_screen_load_anim(g_launcher, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, 200, 0, true);
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color, int y, int w)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_width(l, w);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(l, (W - w) / 2, y);
    return l;
}

static lv_obj_t *pill(lv_obj_t *body, int x, int w, uint32_t bg, uint32_t fg, lv_event_cb_t cb, lv_obj_t **text_out)
{
    lv_obj_t *b = lv_button_create(body);
    lv_obj_set_size(b, w, 65);
    lv_obj_set_pos(b, x, 285);
    lv_obj_set_style_radius(b, 33, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg == ORANGE ? 0xC66A00 : WS_FILL2), LV_STATE_PRESSED);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, "");
    lv_obj_set_style_text_font(l, WATCH_FONT_BOLD_S, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(fg), 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    if (text_out) *text_out = l;
    return b;
}

void timer_open(lv_obj_t *launcher)
{
    g_launcher = launcher;
    build_path();
    memset(&T, 0, sizeof T);
    T.st = T_IDLE;
    T.remain_ms = total_ms();

    lv_obj_t *body;
    scr = sys_screen_create_root_text("Таймер", exit_cb, &body, "", &right_lbl);
    lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(body, LV_LAYOUT_NONE);
    lv_obj_set_style_pad_all(body, 0, 0);
    lv_obj_add_flag(right_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(right_lbl, 16);
    lv_obj_add_event_cb(right_lbl, duration_cb, LV_EVENT_CLICKED, NULL);

    cap_lbl = label(body, "", WATCH_FONT_SMALL, 0x8E8E93, 71, 300);
    lv_obj_set_style_text_letter_space(cap_lbl, 2, 0);
    time_lbl = label(body, "", WATCH_FONT_DIGITS, 0xFFFFFF, 110, 380);
    status_lbl = label(body, "", WATCH_FONT_BODY, 0xA4A5AA, 213, 340);
    btn_reset = pill(body, 39, 112, WS_FILL, 0xFFFFFF, reset_cb, NULL);
    btn_main  = pill(body, 163, 206, ORANGE, 0x000000, main_cb, &main_lbl);
    label(body, "Прогресс — по рамке экрана", WATCH_FONT_SMALL, 0x7B7C80, 385, 360);

    // рамка поверх всего: дорожка серая, прогресс рисуется поверх
    frame = lv_obj_create(scr);
    lv_obj_remove_style_all(frame);
    lv_obj_remove_flag(frame, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(frame, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(frame, W, H);
    lv_obj_set_pos(frame, 0, 0);
    lv_obj_set_style_radius(frame, FR_R, 0);
    lv_obj_set_style_border_width(frame, FR_W, 0);
    lv_obj_set_style_border_color(frame, lv_color_hex(WS_FILL2), 0);
    lv_obj_set_style_border_opa(frame, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(frame, frame_draw_cb, LV_EVENT_DRAW_POST, NULL);

    tick = lv_timer_create(tick_cb, 100, NULL);
    refresh();
    lv_screen_load_anim(scr, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
}
