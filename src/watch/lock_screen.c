#include "lock_screen.h"
#include "auth.h"
#include "clock.h"
#include "pinpad.h"
#include "sys_ui.h"
#include "watch_fonts.h"
#include <stdio.h>

static lv_obj_t   *ov;          // затемнение + клавиатура
static lv_obj_t   *pad;
static lv_obj_t   *time_lbl;    // время (если телефон его синхронизировал)
static lv_timer_t *count_timer; // обратный отсчёт во время задержки
static lv_timer_t *idle_timer;

static void refresh_state(void);

static void refresh_time(void)
{
    if (!ov || !time_lbl) return;
    struct tm t;
    char s[8] = "";
    if (clock_local(&t)) snprintf(s, sizeof s, "%02d:%02d", t.tm_hour, t.tm_min);
    lv_label_set_text(time_lbl, s);
}

static void count_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    refresh_state();
}

// Показывает задержку после серии неудач и блокирует ввод, пока она не закончится.
static void refresh_state(void)
{
    if (!ov) return;
    uint32_t rem = auth_lock_remaining_s();
    if (rem > 0) {
        char m[64];
        snprintf(m, sizeof m, "Too many attempts. Try again in %us", (unsigned)rem);
        pinpad_set_enabled(pad, false);
        pinpad_set_message(pad, m, 0xF59E0B);
        if (!count_timer) count_timer = lv_timer_create(count_cb, 1000, NULL);
    } else {
        if (count_timer) { lv_timer_delete(count_timer); count_timer = NULL; pinpad_set_message(pad, "", 0xFFFFFF); }
        pinpad_set_enabled(pad, true);
    }
}

static void unlock(void)
{
    if (count_timer) { lv_timer_delete(count_timer); count_timer = NULL; }
    lv_obj_delete_async(ov);
    ov = NULL; pad = NULL; time_lbl = NULL;
    lv_display_trigger_activity(NULL);   // отсчёт бездействия начинается заново
}

static void pin_cb(const char *pin, void *ud)
{
    LV_UNUSED(ud);
    switch (auth_verify(pin)) {
    case AUTH_OK:
    case AUTH_NO_PIN:
        unlock();
        break;
    case AUTH_LOCKED:
        refresh_state();
        break;
    case AUTH_WRONG:
        if (auth_lock_remaining_s() > 0) {
            refresh_state();
        } else {
            char m[48];
            snprintf(m, sizeof m, "Wrong PIN. Attempts left: %d", auth_attempts_left());
            pinpad_set_message(pad, m, 0xEF4444);
        }
        break;
    }
}

void lock_screen_lock(void)
{
    if (ov || !auth_has_pin()) return;
    sys_keyboard_hide();
    sys_confirm_close();

    ov = lv_obj_create(lv_layer_sys());
    lv_obj_remove_style_all(ov);
    lv_obj_remove_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ov, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(ov, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, 0);
    lv_obj_add_flag(ov, LV_OBJ_FLAG_CLICKABLE);   // перехватывает все касания

    time_lbl = lv_label_create(ov);
    lv_obj_set_style_text_font(time_lbl, WATCH_FONT_LARGE, 0);
    lv_obj_set_style_text_color(time_lbl, lv_color_hex(0x9CA3AF), 0);
    lv_obj_align(time_lbl, LV_ALIGN_TOP_MID, 0, 18);
    refresh_time();

    pad = pinpad_create(ov, auth_pin_len(), pin_cb, NULL);
    lv_obj_center(pad);
    pinpad_set_title(pad, "Enter PIN");
    refresh_state();   // после перезапуска задержка может ещё действовать
}

bool lock_screen_is_locked(void) { return ov != NULL; }

static void idle_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    refresh_time();   // минуты меняются, пока экран заблокирован
    int to = auth_timeout_s();
    if (!ov && auth_has_pin() && to > 0 && lv_display_get_inactive_time(NULL) > (uint32_t)to * 1000u)
        lock_screen_lock();
}

void lock_screen_init(void)
{
    if (!idle_timer) idle_timer = lv_timer_create(idle_cb, 1000, NULL);
    lock_screen_lock();
}
