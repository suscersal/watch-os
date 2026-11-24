#include "statusbar.h"
#include "clock.h"
#include "haptic.h"
#include "lock_screen.h"
#include "plat.h"
#include "power.h"
#include "timer_app.h"
#include "watch_fonts.h"
#include "watchface.h"
#include "ws_style.h"
#include <stdio.h>
#include <string.h>

#define SIDE       34                     // отступ от краёв: углы экрана скруглены
#define STOPWATCH  "\xEF\x8B\xB2"         // FontAwesome f2f2, есть в шрифтах проекта
#define ORANGE     0xFF8800

static struct {
    lv_obj_t   *time, *chip, *chip_txt, *wifi, *batt;
    lv_timer_t *timer;
    // меню таймера
    lv_obj_t   *scrim, *p_state, *p_time, *p_main_lbl, *p_bar, *p_el, *p_tot;
    lv_timer_t *ptimer;
} B;

static void fmt_ms(char *b, size_t n, uint32_t ms)       // "03:41", секунды округляются вверх, как в приложении
{
    uint32_t s = (ms + 999) / 1000;
    snprintf(b, n, "%02u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
}

// ---------- меню таймера ----------
static void popup_update(void);

void statusbar_popup_close(void)
{
    if (B.ptimer) { lv_timer_delete(B.ptimer); B.ptimer = NULL; }
    if (B.scrim)  { lv_obj_delete(B.scrim); B.scrim = NULL; }
    B.p_state = B.p_time = B.p_main_lbl = B.p_bar = B.p_el = B.p_tot = NULL;
}

static void scrim_cb(lv_event_t *e)
{
    if (lv_event_get_target(e) == lv_event_get_current_target(e)) statusbar_popup_close();   // нажатие мимо карточки
}

static void stop_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    haptic_tick();
    timer_stop();                 // меню закроется само: таймер больше не активен
}

static void main_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    haptic_tick();
    if (timer_state() == TIMER_RUNNING) timer_pause(); else timer_resume();   // пауза / продолжить / заново
}

static lv_obj_t *label_at(lv_obj_t *p, const char *t, const lv_font_t *f, uint32_t color, lv_align_t al, int x, int y)
{
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, t);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_align(l, al, x, y);
    return l;
}

static lv_obj_t *pill(lv_obj_t *p, int x, int w, uint32_t bg, uint32_t fg, lv_event_cb_t cb, lv_obj_t **lbl)
{
    lv_obj_t *b = lv_button_create(p);
    lv_obj_set_size(b, w, 56);
    lv_obj_set_pos(b, x, 92);
    lv_obj_set_style_radius(b, 28, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, "");
    lv_obj_set_style_text_font(l, WATCH_FONT_BOLD_S, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(fg), 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    *lbl = l;
    return b;
}

static void popup_timer_cb(lv_timer_t *t) { LV_UNUSED(t); popup_update(); }

static void popup_open(void)
{
    if (B.scrim || timer_state() == TIMER_IDLE) return;

    B.scrim = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(B.scrim);
    lv_obj_set_size(B.scrim, lv_pct(100), lv_pct(100));
    lv_obj_add_flag(B.scrim, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(B.scrim, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(B.scrim, LV_OPA_50, 0);
    lv_obj_add_event_cb(B.scrim, scrim_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *card = lv_obj_create(B.scrim);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(card, 378, 196);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, STATUSBAR_H + 6);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_radius(card, 36, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x050505), 0);
    lv_obj_set_style_bg_grad_color(card, lv_color_hex(0x3A2110), 0);
    lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x2A2A2E), 0);
    lv_obj_set_style_shadow_width(card, 0, 0);

    lv_obj_t *ico = lv_obj_create(card);                    // оранжевая плитка с секундомером, как обложка в плеере
    lv_obj_remove_flag(ico, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(ico, 60, 60);
    lv_obj_set_pos(ico, 18, 16);
    lv_obj_set_style_radius(ico, 16, 0);
    lv_obj_set_style_border_width(ico, 0, 0);
    lv_obj_set_style_pad_all(ico, 0, 0);
    lv_obj_set_style_bg_color(ico, lv_color_hex(ORANGE), 0);
    lv_obj_t *g = label_at(ico, STOPWATCH, WATCH_FONT_LARGE, 0x000000, LV_ALIGN_CENTER, 0, 0);
    LV_UNUSED(g);

    label_at(card, "Таймер", WATCH_FONT_BOLD_S, 0xFFFFFF, LV_ALIGN_TOP_LEFT, 90, 20);
    B.p_state = label_at(card, "", WATCH_FONT_SMALL, 0x9CA3AF, LV_ALIGN_TOP_LEFT, 90, 48);
    B.p_time = label_at(card, "00:00", WATCH_FONT_MID, 0xFFFFFF, LV_ALIGN_TOP_RIGHT, -20, 16);

    lv_obj_t *stop_lbl;
    pill(card, 18, 165, WS_FILL2, 0xFFFFFF, stop_cb, &stop_lbl);
    lv_label_set_text(stop_lbl, LV_SYMBOL_STOP "  Стоп");
    pill(card, 195, 165, ORANGE, 0x000000, main_cb, &B.p_main_lbl);

    B.p_el = label_at(card, "00:00", WATCH_FONT_SMALL, 0x9CA3AF, LV_ALIGN_TOP_LEFT, 20, 162);
    B.p_tot = label_at(card, "00:00", WATCH_FONT_SMALL, 0x9CA3AF, LV_ALIGN_TOP_RIGHT, -20, 162);
    B.p_bar = lv_bar_create(card);
    lv_obj_set_size(B.p_bar, 250, 8);
    lv_obj_align(B.p_bar, LV_ALIGN_TOP_MID, 0, 168);
    lv_bar_set_range(B.p_bar, 0, 1000);
    lv_obj_set_style_bg_color(B.p_bar, lv_color_hex(0x55555A), LV_PART_MAIN);
    lv_obj_set_style_bg_color(B.p_bar, lv_color_hex(0xE5E5E5), LV_PART_INDICATOR);

    B.ptimer = lv_timer_create(popup_timer_cb, 250, NULL);
    popup_update();
}

static void popup_update(void)
{
    if (!B.scrim) return;
    timer_state_t st = timer_state();
    if (st == TIMER_IDLE || lock_screen_is_locked() || power_screen_off()) { statusbar_popup_close(); return; }
    uint32_t total = timer_total_ms(), rem = timer_remaining_ms();
    char b[12];
    fmt_ms(b, sizeof b, rem);
    lv_label_set_text(B.p_time, b);
    const char *state = "Идёт отсчёт", *main_txt = LV_SYMBOL_PAUSE "  Пауза";
    if (st == TIMER_PAUSED) { state = "Пауза"; main_txt = LV_SYMBOL_PLAY " Продолжить"; }
    if (st == TIMER_DONE)   { state = "Время вышло"; main_txt = LV_SYMBOL_REFRESH "  Заново"; }
    lv_label_set_text(B.p_state, state);
    lv_label_set_text(B.p_main_lbl, main_txt);
    uint32_t el = total > rem ? total - rem : 0;
    fmt_ms(b, sizeof b, el);
    lv_label_set_text(B.p_el, b);
    fmt_ms(b, sizeof b, total);
    lv_label_set_text(B.p_tot, b);
    lv_bar_set_value(B.p_bar, total ? (int32_t)((uint64_t)el * 1000 / total) : 0, LV_ANIM_OFF);
}

// ---------- сама строка ----------
static void chip_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    haptic_tick();
    if (B.scrim) statusbar_popup_close(); else popup_open();
}

static void refresh_bar(void)
{
    if (!B.time || power_screen_off()) return;      // экран выключен: ничего не перерисовываем

    if (watchface_visible()) {                      // на циферблате время и так огромное
        lv_obj_add_flag(B.time, LV_OBJ_FLAG_HIDDEN);
    } else {
        struct tm t;
        char b[8] = "--:--";
        if (clock_local(&t)) snprintf(b, sizeof b, "%02d:%02d", t.tm_hour, t.tm_min);
        if (strcmp(lv_label_get_text(B.time), b) != 0) lv_label_set_text(B.time, b);
        lv_obj_remove_flag(B.time, LV_OBJ_FLAG_HIDDEN);
    }

    char ip[40];
    lv_obj_set_style_text_color(B.wifi, plat_local_ip(ip, sizeof ip) ? lv_color_white() : lv_color_hex(0x4B5563), 0);

    int pct; bool chg;
    if (plat_battery(&pct, &chg)) {
        const char *sym = chg ? LV_SYMBOL_CHARGE
                        : pct > 85 ? LV_SYMBOL_BATTERY_FULL : pct > 60 ? LV_SYMBOL_BATTERY_3
                        : pct > 35 ? LV_SYMBOL_BATTERY_2 : pct > 10 ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY;
        char b[24];
        snprintf(b, sizeof b, "%s %d%%", sym, pct);
        if (strcmp(lv_label_get_text(B.batt), b) != 0) lv_label_set_text(B.batt, b);
        lv_obj_set_style_text_color(B.batt, chg ? lv_color_hex(0x22C55E) : pct <= 15 ? lv_color_hex(0xEF4444) : lv_color_white(), 0);
        lv_obj_remove_flag(B.batt, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(B.batt, LV_OBJ_FLAG_HIDDEN);   // батареи нет или не читается: не рисуем неправду
    }

    timer_state_t st = timer_state();
    if (st == TIMER_IDLE) {
        lv_obj_add_flag(B.chip, LV_OBJ_FLAG_HIDDEN);
    } else {
        char b[24], t[12];
        fmt_ms(t, sizeof t, timer_remaining_ms());
        snprintf(b, sizeof b, STOPWATCH " %s", t);
        if (strcmp(lv_label_get_text(B.chip_txt), b) != 0) lv_label_set_text(B.chip_txt, b);
        lv_obj_set_style_text_color(B.chip_txt, st == TIMER_DONE ? lv_color_hex(0xFF453A) : st == TIMER_PAUSED ? lv_color_hex(0xD1D5DB) : lv_color_hex(ORANGE), 0);
        lv_obj_remove_flag(B.chip, LV_OBJ_FLAG_HIDDEN);
    }
}

static void bar_timer_cb(lv_timer_t *t) { LV_UNUSED(t); refresh_bar(); }

static void on_timer_changed(void)
{
    refresh_bar();
    popup_update();
}

void statusbar_init(void)
{
    if (B.time) return;
    lv_obj_t *bar = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(bar);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(bar, lv_pct(100), STATUSBAR_H);
    lv_obj_set_pos(bar, 0, 0);

    B.time = label_at(bar, "--:--", WATCH_FONT_SMALL, 0xFFFFFF, LV_ALIGN_TOP_LEFT, SIDE, 6);

    lv_obj_t *right = lv_obj_create(bar);                    // Wi-Fi и батарея у правого края
    lv_obj_remove_style_all(right);
    lv_obj_remove_flag(right, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(right, LV_SIZE_CONTENT, 20);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(right, 10, 0);
    lv_obj_align(right, LV_ALIGN_TOP_RIGHT, -SIDE, 4);
    B.wifi = label_at(right, LV_SYMBOL_WIFI, WATCH_FONT_SMALL, 0xFFFFFF, LV_ALIGN_DEFAULT, 0, 0);
    B.batt = label_at(right, "", WATCH_FONT_SMALL, 0xFFFFFF, LV_ALIGN_DEFAULT, 0, 0);

    B.chip = lv_obj_create(bar);                             // значок таймера по центру, единственное, что принимает нажатия
    lv_obj_remove_flag(B.chip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(B.chip, LV_SIZE_CONTENT, 22);
    lv_obj_set_style_pad_hor(B.chip, 10, 0);
    lv_obj_set_style_pad_ver(B.chip, 0, 0);
    lv_obj_set_style_radius(B.chip, 11, 0);
    lv_obj_set_style_border_width(B.chip, 0, 0);
    lv_obj_set_style_bg_color(B.chip, lv_color_hex(ORANGE), 0);
    lv_obj_set_style_bg_opa(B.chip, LV_OPA_20, 0);
    lv_obj_align(B.chip, LV_ALIGN_TOP_MID, 0, 3);
    lv_obj_set_ext_click_area(B.chip, 14);
    lv_obj_add_event_cb(B.chip, chip_cb, LV_EVENT_CLICKED, NULL);
    B.chip_txt = label_at(B.chip, "", WATCH_FONT_SMALL, ORANGE, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(B.chip, LV_OBJ_FLAG_HIDDEN);

    timer_set_change_cb(on_timer_changed);
    B.timer = lv_timer_create(bar_timer_cb, 500, NULL);
    refresh_bar();
}
