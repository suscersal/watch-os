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

// ---- math: текст с дробями, нарисованными стопкой ----
// Дробь приходит от телефона (MathFormat.kt) как FR_A числитель FR_B знаменатель FR_C. Строки без дробей - обычная метка,
// строки с дробями - ряд с переносом: слова-метки и дроби (числитель, черта, знаменатель).
#define FR_A '\x01'
#define FR_B '\x02'
#define FR_C '\x03'

static void math_words(lv_obj_t *row, const char *s, size_t n)
{
    size_t i = 0;
    while (i < n) {
        size_t st = i;
        while (i < n && s[i] != ' ') i++;
        while (i < n && s[i] == ' ') i++;
        char w[64];
        size_t k = i - st;
        if (k > sizeof w - 1) k = sizeof w - 1;
        while (k > 0 && ((unsigned char)s[st + k] & 0xC0) == 0x80) k--;   // не разрезать символ UTF-8
        if (k == 0) { i = st + 1; continue; }
        memcpy(w, s + st, k);
        w[k] = '\0';
        lv_obj_t *l = lv_label_create(row);
        lv_label_set_text(l, w);
    }
}

static void math_frac(lv_obj_t *row, const char *a, size_t an, const char *b, size_t bn)
{
    char na[96], nb[96];
    if (an > sizeof na - 1) an = sizeof na - 1;
    if (bn > sizeof nb - 1) bn = sizeof nb - 1;
    memcpy(na, a, an); na[an] = '\0';
    memcpy(nb, b, bn); nb[bn] = '\0';

    const lv_font_t *f = lv_obj_get_style_text_font(row, LV_PART_MAIN);
    lv_point_t sa, sb;
    lv_text_get_size(&sa, na, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_text_get_size(&sb, nb, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    int32_t w = (sa.x > sb.x ? sa.x : sb.x) + 8;

    lv_obj_t *col = lv_obj_create(row);
    lv_obj_remove_style_all(col);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(col, 3, 0);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *num = lv_label_create(col);
    lv_label_set_long_mode(num, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(num, w);
    lv_obj_set_style_text_align(num, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(num, na);

    lv_obj_t *den = lv_label_create(col);
    lv_label_set_long_mode(den, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(den, w);
    lv_obj_set_style_text_align(den, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_border_width(den, 2, 0);                       // черта дроби - верхняя граница знаменателя
    lv_obj_set_style_border_side(den, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(den, lv_obj_get_style_text_color(row, LV_PART_MAIN), 0);
    lv_obj_set_style_pad_top(den, 2, 0);
    lv_label_set_text(den, nb);
}

// ---- график: строка PL_A xmin|xmax|ymin|ymax|v,v,v... PL_C; значения 0..100 уже посчитаны телефоном (x - нет точки) ----
#define PL_A '\x04'
#define PL_C '\x05'
#define PLOT_MAX_PTS 64

static void math_plot(lv_obj_t *box, const char *s, size_t n)
{
    char tmp[420];
    if (n > sizeof tmp - 1) return;
    memcpy(tmp, s, n); tmp[n] = '\0';
    char *f[5] = { tmp, NULL, NULL, NULL, NULL };
    for (int i = 0, k = 0; tmp[i] && k < 4; i++)
        if (tmp[i] == '|') { tmp[i] = '\0'; f[++k] = tmp + i + 1; }
    if (!f[4]) return;

    int32_t v[PLOT_MAX_PTS];
    int cnt = 0;
    for (const char *p = f[4]; *p && cnt < PLOT_MAX_PTS; ) {
        if (*p == 'x') { v[cnt++] = LV_CHART_POINT_NONE; p++; }
        else { char *e; long x = strtol(p, &e, 10); if (e == p) break; v[cnt++] = (int32_t)x; p = e; }
        if (*p == ',') p++;
    }
    if (cnt < 2) return;

    lv_obj_t *ch = lv_chart_create(box);
    lv_obj_set_size(ch, lv_pct(100), 150);
    lv_chart_set_type(ch, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(ch, (uint32_t)cnt);
    lv_chart_set_axis_range(ch, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_div_line_count(ch, 3, 5);
    lv_obj_set_style_bg_color(ch, lv_color_hex(WS_CARD), 0);
    lv_obj_set_style_border_width(ch, 0, 0);
    lv_obj_set_style_radius(ch, 16, 0);
    lv_obj_set_style_pad_all(ch, 10, 0);
    lv_obj_set_style_line_color(ch, lv_color_hex(WS_FILL2), LV_PART_MAIN);   // сетка
    lv_obj_set_style_size(ch, 0, 0, LV_PART_INDICATOR);                      // без точек
    lv_obj_set_style_line_width(ch, 3, LV_PART_ITEMS);
    lv_chart_series_t *se = lv_chart_add_series(ch, lv_color_hex(ws_accent()), LV_CHART_AXIS_PRIMARY_Y);
    for (int i = 0; i < cnt; i++) lv_chart_set_series_value_by_id(ch, se, (uint32_t)i, v[i]);
    lv_chart_refresh(ch);

    lv_obj_t *cap = lv_label_create(box);
    lv_obj_set_style_text_font(cap, WATCH_FONT_SMALL, 0);
    lv_obj_set_style_text_color(cap, lv_color_hex(WS_TEXT2), 0);
    lv_label_set_text_fmt(cap, "x: %s ... %s   y: %s ... %s", f[0], f[1], f[2], f[3]);
}

static void math_line(lv_obj_t *box, const char *s, size_t n)
{
    if (n == 0) {                                                   // пустая строка - небольшой отступ
        lv_obj_t *sp = lv_obj_create(box);
        lv_obj_remove_style_all(sp);
        lv_obj_set_size(sp, 1, 8);
        return;
    }
    if (s[0] == PL_A) {
        const char *e = memchr(s + 1, PL_C, n - 1);
        if (e) { math_plot(box, s + 1, (size_t)(e - s - 1)); return; }
    }
    if (!memchr(s, FR_A, n)) {                                      // дробей нет: обычная метка с переносом
        char *t = malloc(n + 1);
        if (!t) return;
        memcpy(t, s, n); t[n] = '\0';
        lv_obj_t *l = lv_label_create(box);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(l, lv_pct(100));
        lv_label_set_text(l, t);
        free(t);
        return;
    }
    lv_obj_t *row = lv_obj_create(box);
    lv_obj_remove_style_all(row);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 0, 0);
    lv_obj_set_style_pad_row(row, 2, 0);

    const char *end = s + n;
    while (s < end) {
        const char *a = memchr(s, FR_A, (size_t)(end - s));
        if (!a) { math_words(row, s, (size_t)(end - s)); break; }
        const char *b = memchr(a + 1, FR_B, (size_t)(end - a - 1));
        const char *c = b ? memchr(b + 1, FR_C, (size_t)(end - b - 1)) : NULL;
        math_words(row, s, (size_t)(a - s));
        if (!b || !c) { math_words(row, a + 1, (size_t)(end - a - 1)); break; }   // обрезанная дробь - как текст
        math_frac(row, a + 1, (size_t)(b - a - 1), b + 1, (size_t)(c - b - 1));
        s = c + 1;
    }
}

// Пересобирает содержимое блока math из text. Вызывается при создании и при смене переменных.
void ui_math_fill(lv_obj_t *box, const char *text)
{
    lv_obj_clean(box);
    const char *p = text;
    while (1) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) : strlen(p);
        math_line(box, p, n);
        if (!nl) break;
        p = nl + 1;
    }
}

static void add_math(bctx_t *c, lv_obj_t *parent, const cJSON *n, bool in_row)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_height(box, LV_SIZE_CONTENT);
    if (in_row) lv_obj_set_flex_grow(box, 1); else lv_obj_set_width(box, lv_pct(100));
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_gap(box, 4, 0);
    lv_obj_set_style_text_color(box, lv_color_hex(jcolor(n, "color", 0xFFFFFF)), 0);
    const lv_font_t *f = font_for(jstr(n, "size"));
    if (f) lv_obj_set_style_text_font(box, f, 0);

    const char *t = jstr(n, "text") ? jstr(n, "text") : "";
    static char buf[VAR_EXP];   // не на стеке: ответ ИИ длинный
    vars_expand(t, buf, sizeof buf);
    ui_math_fill(box, buf);
    if (strchr(t, '@') && c->r->nbind < MAX_BIND) {
        bind_t *b = &c->r->binds[c->r->nbind++];
        b->obj = box;
        b->kind = BIND_MATH;
        snprintf(b->tmpl, sizeof b->tmpl, "%s", t);
    }
}


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

    } else if (strcmp(type, "math") == 0) {
        add_math(c, parent, n, in_row);

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
