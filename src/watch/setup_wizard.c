#include "setup_wizard.h"
#include "auth.h"
#include "pinpad.h"
#include "power.h"
#include "settings.h"
#include "settings_app.h"
#include "sys_ui.h"
#include "watch_fonts.h"
#include "watchface.h"
#include "wifi_ctl.h"
#include "ws_style.h"
#include <stdio.h>
#include <string.h>

typedef enum { ST_HELLO, ST_WIFI, ST_PIN, ST_BRIGHT, ST_DONE, ST_N } step_t;

static struct {
    lv_obj_t   *scr, *page, *dots, *skip, *launcher;   // dots - ряд из ST_N кружков
    lv_timer_t *timer;
    void      (*tick)(void);
    int         step, next_step;
    // шаг Wi-Fi
    lv_obj_t   *w_status, *w_next_label;
    // шаг PIN
    lv_obj_t   *pad;
    char        pin_first[AUTH_PIN_MAX + 1];
    bool        have_first;
    // шаг Hello
    lv_obj_t   *hello;
    int         hello_i;
} W;

static void go_async(int step);

bool setup_wizard_needed(void)
{
    if (settings_setup_done()) return false;
    if (auth_has_pin()) return false;
    if (settings_wifi_get(NULL, 0, NULL, 0)) return false;
    return true;
}

// ---------- мелочи оформления ----------
static lv_obj_t *text(const char *t, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = lv_label_create(W.page);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, lv_pct(86));
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, t);
    return l;
}

static lv_obj_t *pill(const char *t, uint32_t bg, uint32_t fg, lv_event_cb_t cb, void *ud, lv_obj_t **label_out)
{
    lv_obj_t *b = lv_button_create(W.page);
    lv_obj_set_size(b, lv_pct(86), 56);
    lv_obj_set_style_radius(b, 28, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, t);
    lv_obj_set_style_text_color(l, lv_color_hex(fg), 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    if (label_out) *label_out = l;
    return b;
}

static void update_dots(void)
{
    for (int i = 0; i < ST_N; i++)
        lv_obj_set_style_bg_color(lv_obj_get_child(W.dots, i), lv_color_hex(i == W.step ? 0xFFFFFF : WS_FILL2), 0);
}

// ---------- переходы ----------
static void stop_timer(void)
{
    if (W.timer) { lv_timer_delete(W.timer); W.timer = NULL; }
    W.tick = NULL;
}

static void timer_cb(lv_timer_t *t) { LV_UNUSED(t); if (W.tick) W.tick(); }

static void build(int step);

static void go_cb(void *ud)
{
    LV_UNUSED(ud);
    if (!W.scr) return;
    build(W.next_step);
}

// Строить новую страницу можно только вне обработчиков событий её же кнопок, поэтому через очередь LVGL.
static void go_async(int step)
{
    W.next_step = step;
    lv_async_call(go_cb, NULL);
}

static void next_cb(lv_event_t *e) { LV_UNUSED(e); go_async(W.step + 1); }

static void finish_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    stop_timer();
    settings_set_setup_done(true);
    lv_obj_t *launcher = W.launcher, *scr = W.scr;
    W.scr = NULL;
    watchface_show(launcher);
    lv_screen_load_anim(launcher, LV_SCREEN_LOAD_ANIM_FADE_IN, 300, 0, false);
    lv_obj_delete_delayed(scr, 400);
}

static void scr_deleted_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    stop_timer();
    memset(&W, 0, sizeof W);
}

// ---------- шаг 1: Hello + язык клавиатуры ----------
static void hello_tick(void)
{
    static const char *hello[] = { "Hello", "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82", "Hallo", "Hola", "Bonjour", "Ciao" };   // Привет
    W.hello_i = (W.hello_i + 1) % (int)(sizeof hello / sizeof hello[0]);
    lv_label_set_text(W.hello, hello[W.hello_i]);
    lv_obj_fade_in(W.hello, 400, 0);
}

static void lang_cb(lv_event_t *e)
{
    sys_keyboard_set_lang((int)(intptr_t)lv_event_get_user_data(e));
    go_async(ST_HELLO);
}

static void build_hello(void)
{
    W.hello = text("Hello", WATCH_FONT_BOLD, 0xFFFFFF);
    lv_obj_set_style_pad_top(W.hello, 30, 0);
    text("Let's set up your watch.", WATCH_FONT_BODY, WS_TEXT2);
    text("Keyboard language", WATCH_FONT_SMALL, WS_TEXT2);
    int cur = sys_keyboard_get_lang();
    pill("English", cur == 0 ? WS_BLUE : WS_FILL, 0xFFFFFF, lang_cb, (void *)(intptr_t)0, NULL);
    pill("\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9", cur == 1 ? WS_BLUE : WS_FILL, 0xFFFFFF, lang_cb, (void *)(intptr_t)1, NULL);   // Русский
    pill("Continue", WS_GREEN, 0x000000, next_cb, NULL, NULL);
    W.tick = hello_tick;
}

// ---------- шаг 2: Wi-Fi ----------
static void wifi_tick(void)
{
    char b[96];
    uint32_t color = WS_TEXT2;
    bool connected = false;
    switch (wifi_ctl_state()) {
    case WIFI_ST_CONNECTED:  snprintf(b, sizeof b, "Connected to %s", wifi_ctl_ssid()); color = WS_GREEN; connected = true; break;
    case WIFI_ST_CONNECTING: snprintf(b, sizeof b, "Connecting to %s...", wifi_ctl_ssid()); color = WS_ORANGE; break;
    case WIFI_ST_FAILED:     snprintf(b, sizeof b, "Could not connect. Check the password."); color = WS_RED; break;
    default:                 snprintf(b, sizeof b, "Not connected"); break;
    }
    if (strcmp(lv_label_get_text(W.w_status), b) != 0) lv_label_set_text(W.w_status, b);
    lv_obj_set_style_text_color(W.w_status, lv_color_hex(color), 0);
    lv_label_set_text(W.w_next_label, connected ? "Continue" : "Skip for now");
}

static void wifi_open_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    settings_open_wifi_from(W.scr);
}

static void build_wifi(void)
{
    text("Wi-Fi", WATCH_FONT_BOLD, 0xFFFFFF);
    text("Connect to a network to sync photos, time and notifications.", WATCH_FONT_BODY, WS_TEXT2);
    W.w_status = text("", WATCH_FONT_BODY, WS_TEXT2);
    pill("Choose a network", WS_BLUE, 0xFFFFFF, wifi_open_cb, NULL, NULL);
    pill("Skip for now", WS_FILL, 0xFFFFFF, next_cb, NULL, &W.w_next_label);
    W.tick = wifi_tick;
    wifi_tick();
}

// ---------- шаг 3: код-пароль ----------
static void pin_title(void)
{
    pinpad_set_title(W.pad, W.have_first ? "Repeat passcode" : "Create a passcode");
}

static void pin_cb(const char *pin, void *ud)
{
    LV_UNUSED(ud);
    if (!W.have_first) {
        snprintf(W.pin_first, sizeof W.pin_first, "%s", pin);
        W.have_first = true;
        pin_title();
        pinpad_set_message(W.pad, "", WS_TEXT2);
        return;
    }
    if (strcmp(pin, W.pin_first) == 0 && auth_set_pin(pin)) {
        memset(W.pin_first, 0, sizeof W.pin_first);
        W.have_first = false;
        go_async(W.step + 1);
        return;
    }
    memset(W.pin_first, 0, sizeof W.pin_first);
    W.have_first = false;
    pin_title();
    pinpad_set_message(W.pad, "Passcodes don't match. Try again.", WS_RED);
}

static void build_pin(void)
{
    W.have_first = false;
    W.pad = pinpad_create(W.page, 0, pin_cb, NULL);
    pin_title();
    pinpad_set_message(W.pad, "4 to 6 digits", WS_TEXT2);
}

// ---------- шаг 4: яркость ----------
static void bright_cb(lv_event_t *e)
{
    power_preview_brightness((int)lv_slider_get_value(lv_event_get_target_obj(e)));
}

static void bright_release_cb(lv_event_t *e)
{
    int dim, off, br;
    power_get_settings(&dim, &off, &br);
    power_set_settings(dim, off, (int)lv_slider_get_value(lv_event_get_target_obj(e)));
}

static void build_bright(void)
{
    text("Brightness", WATCH_FONT_BOLD, 0xFFFFFF);
    text("Drag the slider. You can change it later in Display.", WATCH_FONT_BODY, WS_TEXT2);
    int dim, off, br;
    power_get_settings(&dim, &off, &br);
    lv_obj_t *sl = lv_slider_create(W.page);
    lv_obj_set_size(sl, lv_pct(80), 18);
    lv_obj_set_style_pad_ver(sl, 8, LV_PART_KNOB);
    lv_obj_set_style_bg_color(sl, lv_color_hex(WS_FILL2), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, lv_color_hex(WS_BLUE), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_slider_set_range(sl, 5, 100);
    lv_slider_set_value(sl, br < 5 ? 5 : br, LV_ANIM_OFF);
    lv_obj_add_event_cb(sl, bright_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(sl, bright_release_cb, LV_EVENT_RELEASED, NULL);
    pill("Continue", WS_GREEN, 0x000000, next_cb, NULL, NULL);
}

// ---------- шаг 5: готово ----------
static void build_done(void)
{
    text("All set", WATCH_FONT_BOLD, 0xFFFFFF);
    lv_obj_set_style_pad_top(lv_obj_get_child(W.page, -1), 30, 0);
    text("Open Sync here and in the phone app to set the time and add photos. Swipe up on the watch face for your apps.",
         WATCH_FONT_BODY, WS_TEXT2);
    pill("Start", WS_GREEN, 0x000000, finish_cb, NULL, NULL);
}

// ---------- каркас ----------
static void build(int step)
{
    if (step >= ST_N) step = ST_N - 1;
    stop_timer();
    lv_obj_clean(W.page);
    W.step = step;
    W.w_status = W.w_next_label = W.pad = W.hello = NULL;
    switch (step) {
    case ST_HELLO:  build_hello();  break;
    case ST_WIFI:   build_wifi();   break;
    case ST_PIN:    build_pin();    break;
    case ST_BRIGHT: build_bright(); break;
    default:        build_done();   break;
    }
    update_dots();
    // цифровая клавиатура занимает почти всю высоту: на этом шаге точки прогресса и верхний отступ убираем
    if (step == ST_PIN) lv_obj_add_flag(W.dots, LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(W.dots, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_pad_top(W.page, step == ST_PIN ? 44 : 56, 0);
    if (step == ST_WIFI || step == ST_PIN || step == ST_BRIGHT) lv_obj_remove_flag(W.skip, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(W.skip, LV_OBJ_FLAG_HIDDEN);
    if (W.tick) W.timer = lv_timer_create(timer_cb, step == ST_HELLO ? 2200 : 500, NULL);
    lv_obj_fade_in(W.page, 200, 0);
}

static void skip_cb(lv_event_t *e) { LV_UNUSED(e); go_async(W.step + 1); }

void setup_wizard_show(lv_obj_t *launcher)
{
    memset(&W, 0, sizeof W);
    W.launcher = launcher;

    W.scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(W.scr, lv_color_hex(WS_BG), 0);
    lv_obj_set_style_bg_opa(W.scr, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(W.scr, WATCH_FONT_BODY, 0);
    lv_obj_remove_flag(W.scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(W.scr, scr_deleted_cb, LV_EVENT_DELETE, NULL);

    W.page = lv_obj_create(W.scr);
    lv_obj_remove_style_all(W.page);
    lv_obj_set_size(W.page, lv_pct(100), lv_pct(100));
    lv_obj_set_style_pad_top(W.page, 56, 0);
    lv_obj_set_style_pad_bottom(W.page, 40, 0);
    lv_obj_set_flex_flow(W.page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(W.page, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(W.page, 14, 0);

    W.dots = lv_obj_create(W.scr);
    lv_obj_remove_style_all(W.dots);
    lv_obj_set_size(W.dots, LV_SIZE_CONTENT, 10);
    lv_obj_set_flex_flow(W.dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(W.dots, 10, 0);
    lv_obj_align(W.dots, LV_ALIGN_BOTTOM_MID, 0, -16);
    for (int i = 0; i < ST_N; i++) {
        lv_obj_t *d = lv_obj_create(W.dots);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, 10, 10);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    }

    W.skip = lv_button_create(W.scr);
    lv_obj_remove_style_all(W.skip);
    lv_obj_set_size(W.skip, 110, 44);
    lv_obj_align(W.skip, LV_ALIGN_TOP_RIGHT, -8, 8);
    lv_obj_t *l = lv_label_create(W.skip);
    lv_label_set_text(l, "Skip");
    lv_obj_set_style_text_color(l, lv_color_hex(WS_BLUE), 0);
    lv_obj_align(l, LV_ALIGN_RIGHT_MID, -12, 0);
    lv_obj_add_event_cb(W.skip, skip_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(W.skip, LV_OBJ_FLAG_HIDDEN);

    build(ST_HELLO);
    lv_screen_load(W.scr);
}
