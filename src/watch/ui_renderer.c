#include "ui_renderer.h"
#include "sys_ui.h"
#include "syscalls.h"
#include "vars.h"
#include "watch_fonts.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_WIDGETS 64   // лимит виджетов на экран
#define MAX_DEPTH   4    // вложенность row/col

typedef struct { app_run_t *r; int count; bool over; } bctx_t;

static const char *jstr(const cJSON *o, const char *k)
{
    const cJSON *i = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsString(i) ? i->valuestring : NULL;
}

static uint32_t jcolor(const cJSON *o, const char *k, uint32_t def)
{
    const char *s = jstr(o, k);
    return (s && s[0] == '#' && strlen(s) == 7) ? (uint32_t)strtoul(s + 1, NULL, 16) : def;
}

static const lv_font_t *font_for(const char *size)
{
    if (size && strcmp(size, "large") == 0) return WATCH_FONT_LARGE;
    if (size && strcmp(size, "small") == 0) return WATCH_FONT_SMALL;
    return NULL;   // по умолчанию: шрифт экрана (WATCH_FONT_BODY)
}

// Текст с @переменными: запоминаем шаблон, чтобы обновлять при изменении переменных.
static void set_bound_text(app_run_t *r, lv_obj_t *l, const char *t)
{
    if (!strchr(t, '@')) { lv_label_set_text(l, t); return; }
    char buf[BIND_TMPL];
    vars_expand(t, buf, sizeof buf);
    lv_label_set_text(l, buf);
    if (r->nbind < MAX_BIND) {
        bind_t *b = &r->binds[r->nbind++];
        b->obj = l;
        snprintf(b->tmpl, sizeof b->tmpl, "%s", t);
    }
}

static void action_cb(lv_event_t *e)
{
    app_run_t *r = app_runtime_current();
    if (r) sys_run_actions(r, lv_event_get_user_data(e));
}

static void input_cb(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_FOCUSED) {
        sys_keyboard_attach(ta);
    } else if (c == LV_EVENT_VALUE_CHANGED) {
        const char *var = lv_event_get_user_data(e);
        app_run_t *r = app_runtime_current();
        if (var && r) {
            vars_set(var, lv_textarea_get_text(ta));
            app_refresh_bindings(r);
        }
    }
}

static void switch_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    const char *var = lv_event_get_user_data(e);
    app_run_t *r = app_runtime_current();
    if (!var || !r) return;
    vars_set(var, lv_obj_has_state(sw, LV_STATE_CHECKED) ? "1" : "0");
    app_refresh_bindings(r);
}

static lv_obj_t *make_box(lv_obj_t *parent, bool row, bool in_row)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_height(o, LV_SIZE_CONTENT);
    if (in_row) lv_obj_set_flex_grow(o, 1); else lv_obj_set_width(o, lv_pct(100));
    lv_obj_set_flex_flow(o, row ? LV_FLEX_FLOW_ROW : LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(o, 8, 0);
    if (row) lv_obj_set_flex_align(o, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return o;
}

static void add_widget(bctx_t *c, lv_obj_t *parent, const cJSON *n, int depth, bool in_row);

static void add_children(bctx_t *c, lv_obj_t *parent, const cJSON *arr, int depth, bool in_row)
{
    if (!cJSON_IsArray(arr)) return;
    const cJSON *n;
    cJSON_ArrayForEach(n, arr) {
        if (c->count >= MAX_WIDGETS) {
            if (!c->over) { c->over = true; sys_toast("Too many widgets"); }
            return;
        }
        add_widget(c, parent, n, depth, in_row);
    }
}

static void add_widget(bctx_t *c, lv_obj_t *parent, const cJSON *n, int depth, bool in_row)
{
    const char *type = jstr(n, "type");
    if (!type) return;
    c->count++;

    if (strcmp(type, "label") == 0) {
        lv_obj_t *l = lv_label_create(parent);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        if (in_row) lv_obj_set_flex_grow(l, 1); else lv_obj_set_width(l, lv_pct(100));
        lv_obj_set_style_text_color(l, lv_color_hex(jcolor(n, "color", 0xFFFFFF)), 0);
        const lv_font_t *f = font_for(jstr(n, "size"));
        if (f) lv_obj_set_style_text_font(l, f, 0);
        set_bound_text(c->r, l, jstr(n, "text") ? jstr(n, "text") : "");

    } else if (strcmp(type, "button") == 0) {
        lv_obj_t *b = lv_button_create(parent);
        lv_obj_set_height(b, 52);
        if (in_row) lv_obj_set_flex_grow(b, 1); else lv_obj_set_width(b, lv_pct(100));
        lv_obj_set_style_radius(b, 14, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(jcolor(n, "color", 0x3B82F6)), 0);
        lv_obj_t *l = lv_label_create(b);
        lv_obj_set_style_text_color(l, lv_color_white(), 0);
        set_bound_text(c->r, l, jstr(n, "text") ? jstr(n, "text") : "");
        lv_obj_center(l);
        const cJSON *oc = cJSON_GetObjectItemCaseSensitive(n, "on_click");
        if (cJSON_IsArray(oc)) lv_obj_add_event_cb(b, action_cb, LV_EVENT_CLICKED, (void *)oc);

    } else if (strcmp(type, "input") == 0) {
        lv_obj_t *ta = lv_textarea_create(parent);
        lv_textarea_set_one_line(ta, true);
        lv_textarea_set_max_length(ta, VAR_VAL - 1);
        if (in_row) lv_obj_set_flex_grow(ta, 1); else lv_obj_set_width(ta, lv_pct(100));
        lv_obj_set_style_bg_color(ta, lv_color_hex(0x1F2937), 0);
        lv_obj_set_style_text_color(ta, lv_color_white(), 0);
        lv_obj_set_style_border_color(ta, lv_color_hex(0x374151), 0);
        lv_obj_set_style_radius(ta, 12, 0);
        const char *ph = jstr(n, "placeholder");
        if (ph) lv_textarea_set_placeholder_text(ta, ph);
        const char *var = jstr(n, "var");
        if (!vars_valid_name(var)) var = NULL;
        if (var) lv_textarea_set_text(ta, vars_get(var));
        lv_obj_add_event_cb(ta, input_cb, LV_EVENT_ALL, (void *)var);

    } else if (strcmp(type, "switch") == 0) {
        lv_obj_t *row = make_box(parent, true, in_row);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_t *l = lv_label_create(row);
        lv_obj_set_style_text_color(l, lv_color_white(), 0);
        set_bound_text(c->r, l, jstr(n, "text") ? jstr(n, "text") : "");
        lv_obj_t *sw = lv_switch_create(row);
        const char *var = jstr(n, "var");
        if (!vars_valid_name(var)) var = NULL;
        if (var && strcmp(vars_get(var), "1") == 0) lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, switch_cb, LV_EVENT_VALUE_CHANGED, (void *)var);

    } else if (strcmp(type, "spacer") == 0) {
        const cJSON *h = cJSON_GetObjectItemCaseSensitive(n, "h");
        int hh = cJSON_IsNumber(h) ? (int)h->valuedouble : 10;
        if (hh < 1) hh = 1;
        if (hh > 200) hh = 200;
        lv_obj_t *s = lv_obj_create(parent);
        lv_obj_remove_style_all(s);
        lv_obj_set_size(s, 1, hh);

    } else if (strcmp(type, "row") == 0 || strcmp(type, "col") == 0) {
        if (depth >= MAX_DEPTH) return;
        lv_obj_t *box = make_box(parent, type[0] == 'r', in_row);
        add_children(c, box, cJSON_GetObjectItemCaseSensitive(n, "children"), depth + 1, type[0] == 'r');
    }
    // неизвестные типы молча пропускаются
}

static void back_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    app_run_t *r = app_runtime_current();
    if (r) app_nav_back(r);
}

lv_obj_t *ui_build_screen(app_run_t *r, const char *name)
{
    const cJSON *sc = cJSON_GetObjectItemCaseSensitive(r->screens, name);
    if (!cJSON_IsObject(sc)) return NULL;
    r->nbind = 0;

    lv_obj_t *body;
    lv_obj_t *scr = sys_screen_create(jstr(sc, "title") ? jstr(sc, "title") : r->app->name, back_cb, &body);

    bctx_t c = { r, 0, false };
    add_children(&c, body, cJSON_GetObjectItemCaseSensitive(sc, "ui"), 1, false);
    return scr;
}
