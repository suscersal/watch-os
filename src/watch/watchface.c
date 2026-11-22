#include "watchface.h"
#include "clock.h"
#include "messages.h"
#include "power.h"
#include "statusbar.h"
#include "watch_fonts.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t   *g_launcher;
static lv_obj_t   *g_face, *g_time, *g_date, *g_unread, *g_hint;
static lv_timer_t *g_timer;
static bool        g_hooks;

static const char *const days[]   = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static const char *const months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

static void set_text(lv_obj_t *l, const char *t) { if (strcmp(lv_label_get_text(l), t) != 0) lv_label_set_text(l, t); }

static void refresh(void)
{
    if (!g_face) return;
    struct tm t;
    char b[40];
    if (clock_local(&t)) {
        snprintf(b, sizeof b, "%02d:%02d", t.tm_hour, t.tm_min);
        set_text(g_time, b);
        snprintf(b, sizeof b, "%s, %d %s", days[t.tm_wday % 7], t.tm_mday, months[t.tm_mon % 12]);
        set_text(g_date, b);
        lv_obj_add_flag(g_hint, LV_OBJ_FLAG_HIDDEN);
    } else {
        set_text(g_time, "--:--");
        set_text(g_date, "Time not set");
        lv_obj_remove_flag(g_hint, LV_OBJ_FLAG_HIDDEN);   // подсказка, как задать время
    }
    size_t n = messages_unread();
    if (n) { snprintf(b, sizeof b, LV_SYMBOL_ENVELOPE "  %u new", (unsigned)n); set_text(g_unread, b); lv_obj_remove_flag(g_unread, LV_OBJ_FLAG_HIDDEN); }
    else   lv_obj_add_flag(g_unread, LV_OBJ_FLAG_HIDDEN);
}

static void timer_cb(lv_timer_t *t) { LV_UNUSED(t); if (!power_screen_off()) refresh(); }

static void face_deleted_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (g_timer) { lv_timer_delete(g_timer); g_timer = NULL; }
    g_face = g_time = g_date = g_unread = g_hint = NULL;
}

static void slide_done_cb(lv_anim_t *a) { lv_obj_delete_async((lv_obj_t *)a->var); }

static void slide_y_cb(void *obj, int32_t v) { lv_obj_set_y(obj, v); }

void watchface_hide(bool animate)
{
    if (!g_face) return;
    lv_obj_t *f = g_face;
    g_face = NULL;                      // дальше лаунчер считает, что циферблата нет, а объект доживает анимацию
    if (g_timer) { lv_timer_delete(g_timer); g_timer = NULL; }
    g_time = g_date = g_unread = g_hint = NULL;
    lv_obj_remove_event_cb(f, face_deleted_cb);
    lv_obj_remove_flag(f, LV_OBJ_FLAG_CLICKABLE);
    if (!animate) { lv_obj_delete(f); return; }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, f);
    lv_anim_set_values(&a, 0, -lv_obj_get_height(f));
    lv_anim_set_duration(&a, 220);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_exec_cb(&a, slide_y_cb);
    lv_anim_set_completed_cb(&a, slide_done_cb);
    lv_anim_start(&a);
}

static void face_event_cb(lv_event_t *e)
{
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_CLICKED) watchface_hide(true);
    else if (c == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_TOP) watchface_hide(true);
}

// Свайп вниз по сетке (если она не прокручивается) возвращает циферблат.
static void launcher_gesture_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (!g_face && lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_BOTTOM) watchface_show(g_launcher);
}

static void on_wake(void)
{
    if (g_launcher && lv_screen_active() == g_launcher && !g_face) watchface_show(g_launcher);
}

static void msgs_changed(void) { refresh(); }

void watchface_show(lv_obj_t *launcher)
{
    g_launcher = launcher;
    if (!g_hooks) {
        g_hooks = true;
        power_set_wake_cb(on_wake);
        messages_set_change_cb(msgs_changed);
        lv_obj_add_event_cb(launcher, launcher_gesture_cb, LV_EVENT_GESTURE, NULL);
    }
    if (g_face) return;

    g_face = lv_obj_create(launcher);
    lv_obj_remove_style_all(g_face);
    lv_obj_remove_flag(g_face, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(g_face, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(g_face, 0, 0);
    lv_obj_set_style_bg_color(g_face, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_face, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(g_face, face_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(g_face, face_event_cb, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(g_face, face_deleted_cb, LV_EVENT_DELETE, NULL);

    statusbar_create(g_face, false);

    g_time = lv_label_create(g_face);
    lv_obj_set_style_text_font(g_time, WATCH_FONT_TIME, 0);
    lv_obj_set_style_text_color(g_time, lv_color_white(), 0);
    lv_label_set_text(g_time, "--:--");
    lv_obj_align(g_time, LV_ALIGN_CENTER, 0, -50);

    g_date = lv_label_create(g_face);
    lv_obj_set_style_text_font(g_date, WATCH_FONT_LARGE, 0);
    lv_obj_set_style_text_color(g_date, lv_color_hex(0x9CA3AF), 0);
    lv_label_set_text(g_date, "");
    lv_obj_align_to(g_date, g_time, LV_ALIGN_OUT_BOTTOM_MID, 0, 18);

    g_unread = lv_label_create(g_face);
    lv_obj_set_style_text_color(g_unread, lv_color_hex(0x22C55E), 0);
    lv_label_set_text(g_unread, "");
    lv_obj_align(g_unread, LV_ALIGN_CENTER, 0, 120);

    g_hint = lv_label_create(g_face);
    lv_obj_set_style_text_font(g_hint, WATCH_FONT_SMALL, 0);
    lv_obj_set_style_text_color(g_hint, lv_color_hex(0x6B7280), 0);
    lv_label_set_long_mode(g_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g_hint, 300);
    lv_obj_set_style_text_align(g_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(g_hint, "Set the time: open Sync and connect the phone app");
    lv_obj_align(g_hint, LV_ALIGN_CENTER, 0, 170);

    lv_obj_t *up = lv_label_create(g_face);
    lv_obj_set_style_text_color(up, lv_color_hex(0x4B5563), 0);
    lv_label_set_text(up, LV_SYMBOL_UP);
    lv_obj_align(up, LV_ALIGN_BOTTOM_MID, 0, -14);

    g_timer = lv_timer_create(timer_cb, 1000, NULL);
    lv_obj_move_foreground(g_face);
    refresh();
}

bool watchface_visible(void) { return g_face != NULL; }
