#include "ui_renderer.h"
#include "sys_ui.h"
#include "syscalls.h"
#include "tables.h"
#include "ws_style.h"
#include "vars.h"
#include "watch_fonts.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_WIDGETS 64   // лимит виджетов на экран
#define MAX_DEPTH   4    // вложенность row/col
#define MAX_LIST    12   // пунктов в списке

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
    static char buf[VAR_EXP];   // не на стеке: ответ ИИ длинный
    vars_expand(t, buf, sizeof buf);
    lv_label_set_text(l, buf);
    if (r->nbind < MAX_BIND) {
        bind_t *b = &r->binds[r->nbind++];
        b->obj = l;
        b->kind = BIND_LABEL;
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
    if (c == LV_EVENT_FOCUSED || c == LV_EVENT_CLICKED) {
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

// ---- список: пункты-кнопки; по нажатию переменная (по умолчанию "item") получает текст пункта ----
typedef struct { char var[VAR_NAME]; const cJSON *actions; } list_ctx_t;

static void list_item_cb(lv_event_t *e)
{
    const list_ctx_t *ctx = lv_event_get_user_data(e);
    lv_obj_t *btn = lv_event_get_target_obj(e);
    app_run_t *r = app_runtime_current();
    lv_obj_t *lab = lv_obj_get_child(btn, 0);
    if (!r || !lab) return;
    vars_set(ctx->var, lv_label_get_text(lab));
    if (ctx->actions) sys_run_actions(r, ctx->actions);
    else app_refresh_bindings(r);
}

static void list_free_cb(lv_event_t *e)
{
    free(lv_event_get_user_data(e));
}

// ---- ползунок: значение пишется в переменную ----
static void slider_cb(lv_event_t *e)
{
    lv_obj_t *sl = lv_event_get_target_obj(e);
    const char *var = lv_event_get_user_data(e);
    app_run_t *r = app_runtime_current();
    if (!var || !r) return;
    char buf[16];
    snprintf(buf, sizeof buf, "%d", (int)lv_slider_get_value(sl));
    vars_set(var, buf);
    app_refresh_bindings(r);
}

// ---- таблица: первая строка закрашивается как заголовок ----
static void table_draw_cb(lv_event_t *e)
{
    lv_draw_task_t *task = lv_event_get_draw_task(e);
    lv_draw_dsc_base_t *base = lv_draw_task_get_draw_dsc(task);
    if (!base || base->part != LV_PART_ITEMS || base->id1 != 0) return;
    lv_draw_fill_dsc_t *fill = lv_draw_task_get_fill_dsc(task);
    if (fill) { fill->color = lv_color_hex(ws_accent()); fill->opa = LV_OPA_COVER; }
    lv_draw_label_dsc_t *lab = lv_draw_task_get_label_dsc(task);
    if (lab) lab->color = lv_color_white();
}

static void build_table(lv_obj_t *parent, const wtable_t *wt, bool header)
{
    lv_obj_t *t = lv_table_create(parent);
    lv_table_set_column_count(t, (uint32_t)wt->cols);
    lv_table_set_row_count(t, (uint32_t)wt->rows);
    int32_t colw = (lv_display_get_horizontal_resolution(NULL) - 48) / wt->cols;
    for (int c = 0; c < wt->cols; c++) lv_table_set_column_width(t, (uint32_t)c, colw);
    for (int r = 0; r < wt->rows; r++)
        for (int c = 0; c < wt->cols; c++)
            lv_table_set_cell_value(t, (uint32_t)r, (uint32_t)c, wt->cell[r][c]);

    lv_obj_set_width(t, lv_pct(100));
    lv_obj_remove_flag(t, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(t, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(t, 0, 0);
    lv_obj_set_style_pad_all(t, 0, 0);
    lv_obj_set_style_text_font(t, WATCH_FONT_SMALL, LV_PART_ITEMS);
    lv_obj_set_style_text_color(t, lv_color_white(), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(t, lv_color_hex(WS_CARD), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(t, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_border_color(t, lv_color_hex(WS_FILL), LV_PART_ITEMS);
    lv_obj_set_style_border_width(t, 1, LV_PART_ITEMS);
    lv_obj_set_style_pad_all(t, 8, LV_PART_ITEMS);
    if (header) {
        lv_obj_add_flag(t, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
        lv_obj_add_event_cb(t, table_draw_cb, LV_EVENT_DRAW_TASK_ADDED, NULL);
    }
}

// Заполняет таблицу из JSON-строк ("rows" в описании экрана).
static bool table_from_rows(wtable_t *t, const cJSON *rows)
{
    memset(t, 0, sizeof *t);
    if (!cJSON_IsArray(rows)) return false;
    char *txt = cJSON_PrintUnformatted(rows);
    if (!txt) return false;
    wtable_t *tmp = t;
    bool ok = tables_fill_json(tmp, txt);
    free(txt);
    return ok;
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
        lv_obj_set_style_radius(b, 26, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(jcolor(n, "color", WS_FILL)), 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(WS_FILL2), LV_STATE_PRESSED);
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
        lv_obj_set_style_bg_color(ta, lv_color_hex(WS_CARD), 0);
        lv_obj_set_style_text_color(ta, lv_color_white(), 0);
        lv_obj_set_style_text_color(ta, lv_color_hex(WS_TEXT2), LV_PART_TEXTAREA_PLACEHOLDER);
        lv_obj_set_style_border_width(ta, 0, 0);
        lv_obj_set_style_radius(ta, 20, 0);
        lv_obj_set_style_pad_all(ta, 14, 0);
        lv_obj_set_style_bg_color(ta, lv_color_hex(ws_accent()), LV_PART_CURSOR);
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
        lv_obj_set_style_bg_color(sw, lv_color_hex(WS_FILL2), 0);
        lv_obj_set_style_bg_color(sw, lv_color_hex(WS_GREEN), LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(sw, lv_color_white(), LV_PART_KNOB);
        const char *var = jstr(n, "var");
        if (!vars_valid_name(var)) var = NULL;
        if (var && strcmp(vars_get(var), "1") == 0) lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, switch_cb, LV_EVENT_VALUE_CHANGED, (void *)var);

    } else if (strcmp(type, "list") == 0) {
        lv_obj_t *box = make_box(parent, false, in_row);
        lv_obj_set_style_pad_gap(box, 6, 0);
        list_ctx_t *ctx = calloc(1, sizeof *ctx);
        if (!ctx) return;
        const char *var = jstr(n, "var");
        snprintf(ctx->var, sizeof ctx->var, "%s", vars_valid_name(var) ? var : "item");
        const cJSON *oc = cJSON_GetObjectItemCaseSensitive(n, "on_click");
        ctx->actions = cJSON_IsArray(oc) ? oc : NULL;
        lv_obj_add_event_cb(box, list_free_cb, LV_EVENT_DELETE, ctx);
        int k = 0;
        const cJSON *it;
        cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(n, "items")) {
            if (k >= MAX_LIST) break;
            const char *txt = cJSON_IsString(it) ? it->valuestring : jstr(it, "text");
            if (!txt) continue;
            k++;
            lv_obj_t *b = lv_button_create(box);
            lv_obj_set_width(b, lv_pct(100));
            lv_obj_set_height(b, LV_SIZE_CONTENT);
            lv_obj_set_style_min_height(b, 48, 0);
            lv_obj_set_style_radius(b, 22, 0);
            lv_obj_set_style_shadow_width(b, 0, 0);
            lv_obj_set_style_bg_color(b, lv_color_hex(WS_CARD), 0);
            lv_obj_set_style_bg_color(b, lv_color_hex(WS_FILL), LV_STATE_PRESSED);
            lv_obj_t *l = lv_label_create(b);
            lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(l, lv_pct(100));
            lv_obj_set_style_text_color(l, lv_color_white(), 0);
            set_bound_text(c->r, l, txt);
            lv_obj_add_event_cb(b, list_item_cb, LV_EVENT_CLICKED, ctx);
        }

    } else if (strcmp(type, "table") == 0) {
        const char *src = jstr(n, "src");
        bool header = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(n, "header"));
        if (src) {
            const wtable_t *wt = tables_get(src);
            if (wt) build_table(parent, wt, header);
        } else {
            wtable_t *tmp = malloc(sizeof *tmp);
            if (tmp && table_from_rows(tmp, cJSON_GetObjectItemCaseSensitive(n, "rows"))) build_table(parent, tmp, header);
            free(tmp);
        }

    } else if (strcmp(type, "slider") == 0) {
        lv_obj_t *row = make_box(parent, false, in_row);
        if (jstr(n, "text")) {
            lv_obj_t *l = lv_label_create(row);
            lv_obj_set_style_text_color(l, lv_color_white(), 0);
            set_bound_text(c->r, l, jstr(n, "text"));
        }
        const cJSON *mn = cJSON_GetObjectItemCaseSensitive(n, "min");
        const cJSON *mx = cJSON_GetObjectItemCaseSensitive(n, "max");
        int lo = cJSON_IsNumber(mn) ? (int)mn->valuedouble : 0;
        int hi = cJSON_IsNumber(mx) ? (int)mx->valuedouble : 100;
        if (hi <= lo) hi = lo + 1;
        lv_obj_t *sl = lv_slider_create(row);
        lv_obj_set_width(sl, lv_pct(90));
        lv_obj_set_style_pad_all(sl, 6, LV_PART_KNOB);
        lv_obj_set_style_bg_color(sl, lv_color_hex(WS_FILL2), 0);
        lv_obj_set_style_bg_color(sl, lv_color_hex(ws_accent()), LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(sl, lv_color_white(), LV_PART_KNOB);
        lv_slider_set_range(sl, lo, hi);
        const char *var = jstr(n, "var");
        if (!vars_valid_name(var)) var = NULL;
        if (var) lv_slider_set_value(sl, (int32_t)strtod(vars_get(var), NULL), LV_ANIM_OFF);
        lv_obj_add_event_cb(sl, slider_cb, LV_EVENT_VALUE_CHANGED, (void *)var);

    } else if (strcmp(type, "bar") == 0) {
        lv_obj_t *bar = lv_bar_create(parent);
        lv_obj_set_size(bar, lv_pct(100), 14);
        const cJSON *mn = cJSON_GetObjectItemCaseSensitive(n, "min");
        const cJSON *mx = cJSON_GetObjectItemCaseSensitive(n, "max");
        int lo = cJSON_IsNumber(mn) ? (int)mn->valuedouble : 0;
        int hi = cJSON_IsNumber(mx) ? (int)mx->valuedouble : 100;
        if (hi <= lo) hi = lo + 1;
        lv_bar_set_range(bar, lo, hi);
        lv_obj_set_style_bg_color(bar, lv_color_hex(WS_FILL2), 0);
        lv_obj_set_style_bg_color(bar, lv_color_hex(jcolor(n, "color", ws_accent())), LV_PART_INDICATOR);
        const char *var = jstr(n, "var");
        if (vars_valid_name(var)) {
            lv_bar_set_value(bar, (int32_t)strtod(vars_get(var), NULL), LV_ANIM_OFF);
            if (c->r->nbind < MAX_BIND) {
                bind_t *b = &c->r->binds[c->r->nbind++];
                b->obj = bar;
                b->kind = BIND_BAR;
                snprintf(b->tmpl, sizeof b->tmpl, "%s", var);
            }
        }

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
    bool is_root = r->depth >= 1 && strcmp(name, r->hist[0]) == 0;
    const char *ttl = jstr(sc, "title") ? jstr(sc, "title") : r->app->name;
    lv_obj_t *scr = is_root ? sys_screen_create_root(ttl, back_cb, &body) : sys_screen_create(ttl, back_cb, &body);

    bctx_t c = { r, 0, false };
    add_children(&c, body, cJSON_GetObjectItemCaseSensitive(sc, "ui"), 1, false);
    return scr;
}
