#include "display_app.h"
#include "power.h"
#include "sys_ui.h"
#include <stdio.h>

static lv_obj_t *g_launcher;
static lv_obj_t *scr, *body, *bright_label;

// 0 = никогда
static const int dim_opts[] = { 0, 15, 30, 60, 300 };
static const int off_opts[] = { 0, 30, 60, 300, 600 };
#define NOPT 5

static void deleted_cb(lv_event_t *e) { LV_UNUSED(e); scr = body = bright_label = NULL; }

static void back_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_screen_load_anim(g_launcher, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, 200, 0, true);
}

static void show_bright(int pct)
{
    if (!bright_label) return;
    char b[32];
    snprintf(b, sizeof b, "Brightness: %d%%", pct);
    lv_label_set_text(bright_label, b);
}

static void slider_cb(lv_event_t *e)
{
    int v = (int)lv_slider_get_value(lv_event_get_target_obj(e));
    show_bright(v);
    power_preview_brightness(v);                 // видно сразу, но во флеш пишем только при отпускании
}

static void slider_release_cb(lv_event_t *e)
{
    int dim, off, br;
    power_get_settings(&dim, &off, &br);
    power_set_settings(dim, off, (int)lv_slider_get_value(lv_event_get_target_obj(e)));
}

static void dropdown_cb(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target_obj(e);
    uint32_t sel = lv_dropdown_get_selected(dd);
    if (sel >= NOPT) return;
    int dim, off, br;
    power_get_settings(&dim, &off, &br);
    if (lv_event_get_user_data(e) == (void *)dim_opts) dim = dim_opts[sel];
    else                                              off = off_opts[sel];
    power_set_settings(dim, off, br);            // при конфликте (off не позже dim) power сам поправит off
}

static void add_dropdown(const char *title, const char *options, const int *opts, int current)
{
    lv_obj_t *h = lv_label_create(body);
    lv_label_set_text(h, title);
    lv_obj_set_style_text_color(h, lv_color_hex(0x9CA3AF), 0);
    lv_obj_t *dd = lv_dropdown_create(body);
    lv_obj_set_width(dd, lv_pct(100));
    lv_dropdown_set_options(dd, options);
    uint32_t sel = 0;
    for (uint32_t i = 0; i < NOPT; i++) if (opts[i] == current) sel = i;
    lv_dropdown_set_selected(dd, sel);
    lv_obj_add_event_cb(dd, dropdown_cb, LV_EVENT_VALUE_CHANGED, (void *)opts);
}

void display_open(lv_obj_t *launcher)
{
    g_launcher = launcher;
    scr = sys_screen_create("Display", back_cb, &body);
    lv_obj_add_event_cb(scr, deleted_cb, LV_EVENT_DELETE, NULL);

    int dim, off, br;
    power_get_settings(&dim, &off, &br);

    bright_label = lv_label_create(body);
    lv_obj_set_style_text_color(bright_label, lv_color_white(), 0);
    show_bright(br);
    lv_obj_t *sl = lv_slider_create(body);
    lv_obj_set_width(sl, lv_pct(90));
    lv_obj_set_style_pad_all(sl, 6, LV_PART_KNOB);
    lv_slider_set_range(sl, 5, 100);
    lv_slider_set_value(sl, br, LV_ANIM_OFF);
    lv_obj_add_event_cb(sl, slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(sl, slider_release_cb, LV_EVENT_RELEASED, NULL);

    add_dropdown("Dim after", "Never\n15 s\n30 s\n1 min\n5 min", dim_opts, dim);
    add_dropdown("Screen off after", "Never\n30 s\n1 min\n5 min\n10 min", off_opts, off);

    lv_screen_load_anim(scr, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 200, 0, false);   // лаунчер не удаляем
}
