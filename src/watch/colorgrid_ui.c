#include "colorgrid_ui.h"
#include "colorcode.h"
#include "sys_ui.h"
#include "watch_fonts.h"
#include "ws_style.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CELL 16
#define GAP  1
#define SEP  5      // зазор перед проверочными строкой и столбцом (последние)
#define SIDE (CC_N * CELL + (CC_N - 1) * GAP + SEP)

static const uint32_t PAL[4] = { 0xE11D48, 0x22C55E, 0x3B82F6, 0xFACC15 };   // R G B Y
static const char     LET[4] = { 'R', 'G', 'B', 'Y' };

typedef struct {
    uint8_t           cells[CC_CELLS];   // 0..3 или CC_UNSET
    int               cur;               // клетка под курсором
    lv_obj_t         *cell[CC_CELLS];
    lv_obj_t         *grid, *status, *btn_fix, *btn_ok;
    colorgrid_done_cb cb;
    void             *ud;
    cc_report_t       rep;
} st_t;

static st_t *g_live;   // для окна подтверждения: экран мог закрыться, пока оно было открыто

static int pos(int i) { return i * (CELL + GAP) + (i == CC_N - 1 ? SEP : 0); }

static int nearest(int x)
{
    int best = 0, bd = 1 << 30;
    for (int i = 0; i < CC_N; i++) {
        int d = x - (pos(i) + CELL / 2);
        if (d < 0) d = -d;
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

static void paint(st_t *s, int i)
{
    lv_obj_t *c = s->cell[i];
    uint8_t v = s->cells[i];
    lv_obj_t *l = lv_obj_get_child(c, 0);
    lv_obj_set_style_bg_color(c, lv_color_hex(v <= 3 ? PAL[v] : 0x26262B), 0);
    if (v <= 3) {
        char t[2] = { LET[v], 0 };
        lv_label_set_text(l, t);
        lv_obj_set_style_text_color(l, lv_color_hex(v == 1 || v == 3 ? 0x000000 : 0xFFFFFF), 0);
        lv_obj_remove_flag(l, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
    }
    bool bad = s->rep.status == CC_FIXABLE && i == s->rep.row * CC_N + s->rep.col;
    lv_obj_set_style_border_width(c, (i == s->cur || bad) ? 2 : 0, 0);
    lv_obj_set_style_border_color(c, lv_color_hex(bad && i != s->cur ? WS_RED : 0xFFFFFF), 0);
}

static void set_status(st_t *s, const char *t, uint32_t color)
{
    lv_label_set_text(s->status, t);
    lv_obj_set_style_text_color(s->status, lv_color_hex(color), 0);
}

static void refresh(st_t *s)
{
    s->rep = cc_check(s->cells);
    char b[128];
    bool ok = false;
    switch (s->rep.status) {
    case CC_EMPTY:
        snprintf(b, sizeof b, "Строка %d, столбец %d  -  осталось %d", s->cur / CC_N + 1, s->cur % CC_N + 1, s->rep.empty);
        set_status(s, b, WS_TEXT2);
        break;
    case CC_OK: {
        char ssid[33], pass[64];
        if (cc_wifi_decode(s->cells, ssid, pass) == 0) {
            snprintf(b, sizeof b, "Верно: %s", ssid);
            set_status(s, b, WS_GREEN);
            ok = true;
        } else {
            set_status(s, "Верно, но это не код Wi-Fi", WS_RED);
        }
        break;
    }
    case CC_FIXABLE:
        snprintf(b, sizeof b, "Ошибка в клетке: строка %d, столбец %d", s->rep.row + 1, s->rep.col + 1);
        set_status(s, b, WS_ORANGE);
        break;
    default:
        snprintf(b, sizeof b, "Не сходится: строк %d, столбцов %d", s->rep.bad_rows, s->rep.bad_cols);
        set_status(s, b, WS_RED);
        break;
    }
    if (ok) lv_obj_remove_flag(s->btn_ok, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(s->btn_ok, LV_OBJ_FLAG_HIDDEN);
    if (s->rep.status == CC_FIXABLE) lv_obj_remove_flag(s->btn_fix, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(s->btn_fix, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < CC_CELLS; i++) paint(s, i);
}

static void key_cb(lv_event_t *e)
{
    st_t *s = lv_event_get_user_data(e);
    int v = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e));
    if (v < 4) {
        s->cells[s->cur] = (uint8_t)v;
        if (s->cur < CC_CELLS - 1) s->cur++;
    } else if (s->cells[s->cur] != CC_UNSET) {
        s->cells[s->cur] = CC_UNSET;
    } else if (s->cur > 0) {
        s->cells[--s->cur] = CC_UNSET;
    }
    refresh(s);
}

static void grid_click_cb(lv_event_t *e)
{
    st_t *s = lv_event_get_user_data(e);
    lv_indev_t *in = lv_indev_active();
    if (!in) return;
    lv_point_t p;
    lv_indev_get_point(in, &p);
    lv_area_t a;
    lv_obj_get_coords(s->grid, &a);
    s->cur = nearest(p.y - a.y1) * CC_N + nearest(p.x - a.x1);
    refresh(s);
}

static void fix_cb(lv_event_t *e)
{
    st_t *s = lv_event_get_user_data(e);
    cc_apply_fix(s->cells, &s->rep);
    refresh(s);
}

static void ok_cb(lv_event_t *e)
{
    st_t *s = lv_event_get_user_data(e);
    char ssid[33], pass[64];
    if (cc_check(s->cells).status == CC_OK && cc_wifi_decode(s->cells, ssid, pass) == 0 && s->cb) s->cb(ssid, pass, s->ud);
}

static void clear_confirm_cb(bool ok, void *ud)
{
    st_t *s = ud;
    if (!ok || s != g_live) return;
    memset(s->cells, CC_UNSET, sizeof s->cells);
    s->cur = 0;
    refresh(s);
}

static void clear_cb(lv_event_t *e)
{
    sys_confirm("Стереть", "Стереть все клетки?", clear_confirm_cb, lv_event_get_user_data(e));
}

static void root_deleted_cb(lv_event_t *e)
{
    st_t *s = lv_event_get_user_data(e);
    if (g_live == s) g_live = NULL;
    free(s);
}

static lv_obj_t *pill(lv_obj_t *p, int w, int h, uint32_t bg, const char *text, const lv_font_t *font, uint32_t fg, lv_event_cb_t cb, st_t *s)
{
    lv_obj_t *b = lv_button_create(p);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_radius(b, 16, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(fg), 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, s);
    return b;
}

lv_obj_t *colorgrid_create(lv_obj_t *parent, colorgrid_done_cb cb, void *ud)
{
    st_t *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    memset(s->cells, CC_UNSET, sizeof s->cells);
    s->cb = cb;
    s->ud = ud;
    g_live = s;

    lv_obj_t *root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(root, 8, 0);
    lv_obj_add_event_cb(root, root_deleted_cb, LV_EVENT_DELETE, s);

    s->status = lv_label_create(root);
    lv_label_set_long_mode(s->status, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s->status, lv_pct(100));
    lv_obj_set_style_text_align(s->status, LV_TEXT_ALIGN_CENTER, 0);

    // Connect / исправить появляются над клавишами и сдвигают всё вниз только когда нужны
    s->btn_ok = pill(root, SIDE, 44, WS_BLUE, "Подключить", WATCH_FONT_BODY, 0xFFFFFF, ok_cb, s);
    s->btn_fix = pill(root, SIDE, 44, WS_ORANGE, "Исправьте отмеченную клетку", WATCH_FONT_BODY, 0x000000, fix_cb, s);

    lv_obj_t *row = lv_obj_create(root);
    lv_obj_remove_style_all(row);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row, SIDE, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    const int bw = (SIDE - 4 * 6) / 5;
    for (int v = 0; v < 4; v++) {
        char t[2] = { LET[v], 0 };
        lv_obj_t *b = pill(row, bw, 56, PAL[v], t, WATCH_FONT_BOLD, v == 1 || v == 3 ? 0x000000 : 0xFFFFFF, key_cb, s);
        lv_obj_set_user_data(b, (void *)(intptr_t)v);
    }
    lv_obj_t *bk = pill(row, bw, 56, WS_FILL2, LV_SYMBOL_BACKSPACE, WATCH_FONT_BODY, 0xFFFFFF, key_cb, s);
    lv_obj_set_user_data(bk, (void *)(intptr_t)4);

    s->grid = lv_obj_create(root);
    lv_obj_remove_style_all(s->grid);
    lv_obj_set_size(s->grid, SIDE, SIDE);
    lv_obj_remove_flag(s->grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s->grid, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s->grid, grid_click_cb, LV_EVENT_CLICKED, s);
    for (int i = 0; i < CC_CELLS; i++) {
        lv_obj_t *c = lv_obj_create(s->grid);
        lv_obj_remove_style_all(c);
        lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(c, CELL, CELL);
        lv_obj_set_pos(c, pos(i % CC_N), pos(i / CC_N));
        lv_obj_set_style_radius(c, 3, 0);
        lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
        lv_obj_t *l = lv_label_create(c);
        lv_obj_set_style_text_font(l, WATCH_FONT_SMALL, 0);
        lv_obj_center(l);
        s->cell[i] = c;
    }

    lv_obj_t *hint = lv_label_create(root);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, lv_pct(100));
    lv_obj_set_style_text_font(hint, WATCH_FONT_SMALL, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(WS_TEXT2), 0);
    lv_label_set_text(hint, "Нажимайте цвета в том порядке, в каком они на экране: строка за строкой, слева направо. "
                            "Последние строка и столбец - проверочные, вводите их тоже.");
    pill(root, SIDE, 44, WS_FILL, "Очистить всё", WATCH_FONT_BODY, WS_TEXT2, clear_cb, s);

    refresh(s);
    return root;
}
