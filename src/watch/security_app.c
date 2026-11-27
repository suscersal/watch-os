#include "security_app.h"
#include "ws_style.h"
#include "auth.h"
#include "lock_screen.h"
#include "pinpad.h"
#include "sys_ui.h"
#include <stdio.h>
#include <string.h>

typedef enum { MODE_SET, MODE_CHANGE, MODE_REMOVE } mode_t_;
typedef enum { ST_CURRENT, ST_NEW, ST_REPEAT } stage_t;

static lv_obj_t *g_launcher;
static lv_obj_t *main_scr, *main_body;
static lv_obj_t *flow_pad;
static mode_t_   g_mode;
static stage_t   g_stage;
static char      new_pin[AUTH_PIN_MAX + 1];

static const int timeouts[] = { 15, 30, 60, 300, 0 };

static void refresh_main(void);

static void main_deleted_cb(lv_event_t *e) { LV_UNUSED(e); main_scr = main_body = NULL; }

static void main_back_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_screen_load_anim(g_launcher, sys_exit_anim(), 200, 0, true);   // экран Security удаляется
}

// ---------- сценарий ввода PIN ----------
static void flow_finish(void)
{
    memset(new_pin, 0, sizeof new_pin);
    flow_pad = NULL;
    refresh_main();
    lv_screen_load_anim(main_scr, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, 200, 0, true);     // экран ввода удаляется
}

static void flow_back_cb(lv_event_t *e) { LV_UNUSED(e); flow_finish(); }

static void set_stage(stage_t st)
{
    g_stage = st;
    pinpad_set_title(flow_pad, st == ST_CURRENT ? "Текущий PIN" : st == ST_NEW ? "Новый PIN" : "Повторите PIN");
    pinpad_clear(flow_pad);
}

static void flow_pin_cb(const char *pin, void *ud)
{
    LV_UNUSED(ud);
    if (g_stage == ST_CURRENT) {
        auth_result_t r = auth_verify(pin);
        if (r == AUTH_OK) {
            if (g_mode == MODE_REMOVE) { auth_remove_pin(); sys_toast("PIN удалён"); flow_finish(); }
            else { pinpad_set_message(flow_pad, "", 0xFFFFFF); set_stage(ST_NEW); }
        } else if (auth_lock_remaining_s() > 0) {
            char m[128];
            snprintf(m, sizeof m, "Слишком много попыток. Подождите %u с", (unsigned)auth_lock_remaining_s());
            pinpad_set_message(flow_pad, m, 0xF59E0B);
        } else {
            char m[96];
            snprintf(m, sizeof m, "Неверный PIN. Осталось попыток: %d", auth_attempts_left());
            pinpad_set_message(flow_pad, m, 0xEF4444);
        }
    } else if (g_stage == ST_NEW) {
        snprintf(new_pin, sizeof new_pin, "%s", pin);
        pinpad_set_message(flow_pad, "", 0xFFFFFF);
        set_stage(ST_REPEAT);
    } else {
        if (strcmp(pin, new_pin) == 0 && auth_set_pin(pin)) {
            sys_toast("PIN сохранён");
            flow_finish();
        } else {
            pinpad_set_message(flow_pad, "PIN не совпадают", 0xEF4444);
            set_stage(ST_NEW);
        }
    }
}

static void start_flow(mode_t_ mode)
{
    g_mode = mode;
    lv_obj_t *body;
    lv_obj_t *scr = sys_screen_create(mode == MODE_SET ? "Задать PIN" : mode == MODE_CHANGE ? "Сменить PIN" : "Удалить PIN",
                                      flow_back_cb, &body);
    flow_pad = pinpad_create(body, 0, flow_pin_cb, NULL);
    set_stage(mode == MODE_SET ? ST_NEW : ST_CURRENT);
    lv_screen_load_anim(scr, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 200, 0, false);          // главный экран не удаляем
}

// ---------- главный экран ----------
static void set_cb(lv_event_t *e)    { LV_UNUSED(e); start_flow(auth_has_pin() ? MODE_CHANGE : MODE_SET); }
static void remove_cb(lv_event_t *e) { LV_UNUSED(e); start_flow(MODE_REMOVE); }
static void lock_cb(lv_event_t *e)   { LV_UNUSED(e); lock_screen_lock(); }

static void timeout_cb(lv_event_t *e)
{
    uint32_t sel = lv_dropdown_get_selected(lv_event_get_target(e));
    if (sel < sizeof timeouts / sizeof timeouts[0]) auth_set_timeout_s(timeouts[sel]);
}

static lv_obj_t *add_button(const char *text, uint32_t color, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(main_body);
    lv_obj_set_size(b, lv_pct(100), 52);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    return b;
}

static void refresh_main(void)
{
    if (!main_body) return;
    lv_obj_clean(main_body);

    lv_obj_t *st = lv_label_create(main_body);
    lv_label_set_text(st, auth_has_pin() ? "PIN задан" : "PIN не задан");
    lv_obj_set_style_text_color(st, lv_color_hex(auth_has_pin() ? 0x22C55E : 0x9CA3AF), 0);

    add_button(auth_has_pin() ? "Сменить PIN" : "Задать PIN", WS_BLUE, set_cb);
    if (auth_has_pin()) {
        add_button("Заблокировать", 0x6B7280, lock_cb);
        add_button("Удалить PIN", 0xEF4444, remove_cb);

        lv_obj_t *h = lv_label_create(main_body);
        lv_label_set_text(h, "Автоблокировка через");
        lv_obj_set_style_text_color(h, lv_color_hex(0x9CA3AF), 0);
        lv_obj_t *dd = lv_dropdown_create(main_body);
        lv_obj_set_width(dd, lv_pct(100));
        lv_dropdown_set_options(dd, "15 с\n30 с\n1 мин\n5 мин\nНикогда");
        uint32_t sel = 1;
        for (uint32_t i = 0; i < sizeof timeouts / sizeof timeouts[0]; i++) if (timeouts[i] == auth_timeout_s()) sel = i;
        lv_dropdown_set_selected(dd, sel);
        lv_obj_add_event_cb(dd, timeout_cb, LV_EVENT_VALUE_CHANGED, NULL);
    }
}

void security_open(lv_obj_t *launcher)
{
    g_launcher = launcher;
    main_scr = sys_screen_create_root("Защита", main_back_cb, &main_body);
    lv_obj_add_event_cb(main_scr, main_deleted_cb, LV_EVENT_DELETE, NULL);
    refresh_main();
    lv_screen_load_anim(main_scr, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 200, 0, false);     // лаунчер не удаляем
}
