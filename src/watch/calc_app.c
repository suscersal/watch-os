// calc_app.c - калькулятор: + - x / % +/-, дробные числа, цепочки операций (как на часах/телефоне, без приоритета операций).
// Экран: крупное число сверху, под ним 5 рядов по 4 кнопки. Выход - свайп вправо или вверх (как у остальных приложений).
// В шрифтах проекта нет знаков × и ÷, поэтому на кнопках стоят x и /.
#include "calc_app.h"
#include "sys_ui.h"
#include "watch_fonts.h"
#include "ws_style.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DIGITS 8      // цифр в вводимом числе
#define GAP        8
#define DISP_H     84     // высота зоны с числом

static lv_obj_t *g_launcher, *scr, *disp, *clear_lbl;
static lv_obj_t *op_btn[4], *op_lbl[4];
static const char OPS[4] = {'+', '-', '*', '/'};
static int  disp_w;

static char   entry[20];   // вводимое число строкой; пусто - ввода нет
static double acc;         // накопленное значение или последний результат
static char   op;          // отложенная операция или 0
static bool   fresh;       // показан результат после "=": следующая цифра начинает новое число
static bool   err;         // деление на ноль

static void fmt_value(double v, char *out, size_t n)
{
    if (!isfinite(v)) { snprintf(out, n, "Error"); return; }
    if (fabs(v) < 1e-12) v = 0;                 // без "-0" и хвостов вида 1e-17
    snprintf(out, n, "%.8g", v);
}

// Число без экспоненты (для кнопки %, чтобы результат можно было продолжать вводить).
static void fmt_plain(double v, char *out, size_t n)
{
    snprintf(out, n, "%.7f", v);
    char *dot = strchr(out, '.');
    if (dot) {
        char *e = out + strlen(out) - 1;
        while (e > dot && *e == '0') *e-- = 0;
        if (e == dot) *e = 0;
    }
    if (strcmp(out, "-0") == 0) snprintf(out, n, "0");
}

static void show(void)
{
    char buf[32];
    if (err)           snprintf(buf, sizeof buf, "Error");
    else if (entry[0]) snprintf(buf, sizeof buf, "%s", entry);
    else               fmt_value(acc, buf, sizeof buf);

    // Крупный шрифт знает только 0-9 . - %: для "Error" и чисел с экспонентой, а также если число не влезает, берём обычный
    const lv_font_t *f = WATCH_FONT_HUGE;
    lv_point_t sz;
    lv_text_get_size(&sz, buf, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if (err || strpbrk(buf, "eEr") || sz.x > disp_w) f = WATCH_FONT_BOLD;

    lv_obj_set_style_text_font(disp, f, 0);
    lv_label_set_text(disp, buf);
    lv_obj_set_y(disp, DISP_H - lv_font_get_line_height(f) - 2);

    lv_label_set_text(clear_lbl, (entry[0] && !err) ? "C" : "AC");
    for (int i = 0; i < 4; i++) {               // выбранная операция подсвечена: белая кнопка, оранжевый знак
        bool sel = op == OPS[i] && !entry[0] && !err;
        lv_obj_set_style_bg_color(op_btn[i], lv_color_hex(sel ? 0xFFFFFF : WS_ORANGE), 0);
        lv_obj_set_style_text_color(op_lbl[i], lv_color_hex(sel ? WS_ORANGE : 0xFFFFFF), 0);
    }
}

static bool compute(double a, char o, double b, double *r)
{
    switch (o) {
        case '+': *r = a + b; break;
        case '-': *r = a - b; break;
        case '*': *r = a * b; break;
        case '/': if (b == 0) return false; *r = a / b; break;
        default:  *r = b; break;
    }
    return isfinite(*r);
}

static void reset_all(void)
{
    entry[0] = 0; acc = 0; op = 0; fresh = false; err = false;
}

static void press(char k)
{
    if (err) {                                  // после ошибки любая клавиша сначала сбрасывает калькулятор
        reset_all();
        if (k == 'C') { show(); return; }
    }

    if (k >= '0' && k <= '9') {
        if (fresh) { entry[0] = 0; fresh = false; }
        int digits = 0;
        for (const char *p = entry; *p; p++) if (*p >= '0' && *p <= '9') digits++;
        if (digits >= MAX_DIGITS) return;
        if (strcmp(entry, "0") == 0)  entry[0] = 0;               // 0 + цифра = цифра
        if (strcmp(entry, "-0") == 0) snprintf(entry, sizeof entry, "-");
        size_t n = strlen(entry);
        entry[n] = k; entry[n + 1] = 0;
    } else if (k == '.') {
        if (fresh) { entry[0] = 0; fresh = false; }
        if (strchr(entry, '.')) return;
        if (!entry[0])                    snprintf(entry, sizeof entry, "0.");
        else if (strcmp(entry, "-") == 0) snprintf(entry, sizeof entry, "-0.");
        else                              strcat(entry, ".");
    } else if (k == '+' || k == '-' || k == '*' || k == '/') {
        if (entry[0]) {
            double v = atof(entry);
            if (op) { if (!compute(acc, op, v, &acc)) { err = true; show(); return; } }
            else acc = v;
            entry[0] = 0;
        }
        op = k; fresh = false;
    } else if (k == '=') {
        if (op) {
            double v = entry[0] ? atof(entry) : acc;                 // "5 + =" даёт 10, как на телефоне
            if (!compute(acc, op, v, &acc)) { err = true; show(); return; }
            op = 0; entry[0] = 0; fresh = true;
        } else if (entry[0]) {
            acc = atof(entry); entry[0] = 0; fresh = true;
        }
    } else if (k == 'n') {                                           // смена знака
        if (entry[0]) {
            if (entry[0] == '-') memmove(entry, entry + 1, strlen(entry));
            else { memmove(entry + 1, entry, strlen(entry) + 1); entry[0] = '-'; }
        } else if (op) {
            snprintf(entry, sizeof entry, "-0");
        } else {
            acc = -acc;
        }
    } else if (k == '%') {
        if (entry[0]) fmt_plain(atof(entry) / 100.0, entry, sizeof entry);
        else          acc = acc / 100.0;
    } else if (k == 'C') {
        if (entry[0]) entry[0] = 0;                                  // C стирает ввод, AC - всё
        else reset_all();
    }
    show();
}

static void key_cb(lv_event_t *e)
{
    press((char)(intptr_t)lv_event_get_user_data(e));
}

static void back_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_screen_load_anim(g_launcher, sys_exit_anim(), 200, 0, true);
}

static void deleted_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    scr = disp = clear_lbl = NULL;
    for (int i = 0; i < 4; i++) op_btn[i] = op_lbl[i] = NULL;
}

typedef struct { const char *text; char key; uint32_t bg, fg; int row, col, span; } key_def_t;

#define GREY 0xA5A5A5
#define DARK 0x333336

static const key_def_t KEYS[] = {
    {"AC", 'C', GREY, 0x000000, 0, 0, 1}, {"+/-", 'n', GREY, 0x000000, 0, 1, 1},
    {"%",  '%', GREY, 0x000000, 0, 2, 1}, {"/",  '/', WS_ORANGE, 0xFFFFFF, 0, 3, 1},
    {"7", '7', DARK, 0xFFFFFF, 1, 0, 1}, {"8", '8', DARK, 0xFFFFFF, 1, 1, 1},
    {"9", '9', DARK, 0xFFFFFF, 1, 2, 1}, {"x", '*', WS_ORANGE, 0xFFFFFF, 1, 3, 1},
    {"4", '4', DARK, 0xFFFFFF, 2, 0, 1}, {"5", '5', DARK, 0xFFFFFF, 2, 1, 1},
    {"6", '6', DARK, 0xFFFFFF, 2, 2, 1}, {"\xE2\x88\x92", '-', WS_ORANGE, 0xFFFFFF, 2, 3, 1},   // U+2212
    {"1", '1', DARK, 0xFFFFFF, 3, 0, 1}, {"2", '2', DARK, 0xFFFFFF, 3, 1, 1},
    {"3", '3', DARK, 0xFFFFFF, 3, 2, 1}, {"+", '+', WS_ORANGE, 0xFFFFFF, 3, 3, 1},
    {"0", '0', DARK, 0xFFFFFF, 4, 0, 2}, {".", '.', DARK, 0xFFFFFF, 4, 2, 1},
    {"=", '=', WS_ORANGE, 0xFFFFFF, 4, 3, 1},
};

void calc_open(lv_obj_t *launcher)
{
    g_launcher = launcher;
    reset_all();

    lv_obj_t *body;
    scr = sys_screen_create_root("Калькулятор", back_cb, &body);
    lv_obj_add_event_cb(scr, deleted_cb, LV_EVENT_DELETE, NULL);
    lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(body, LV_LAYOUT_NONE);
    lv_obj_update_layout(scr);

    int cw = lv_obj_get_content_width(body);
    int ch = lv_obj_get_content_height(body);
    if (cw < 200) cw = 370;
    if (ch < 250) ch = 330;
    disp_w = cw;

    disp = lv_label_create(body);
    lv_obj_set_width(disp, cw);
    lv_label_set_long_mode(disp, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(disp, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(disp, lv_color_white(), 0);
    lv_obj_set_pos(disp, 0, 0);

    int bw = (cw - 3 * GAP) / 4;
    int bh = (ch - DISP_H - 5 * GAP) / 5;
    if (bh > 64) bh = 64;
    if (bh < 36) bh = 36;
    int top = DISP_H + GAP;

    for (size_t i = 0; i < sizeof KEYS / sizeof KEYS[0]; i++) {
        const key_def_t *k = &KEYS[i];
        lv_obj_t *b = lv_button_create(body);
        lv_obj_set_size(b, bw * k->span + GAP * (k->span - 1), bh);
        lv_obj_set_pos(b, k->col * (bw + GAP), top + k->row * (bh + GAP));
        lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(k->bg), 0);
        lv_obj_set_style_bg_color(b, lv_color_lighten(lv_color_hex(k->bg), 60), LV_STATE_PRESSED);
        lv_obj_add_event_cb(b, key_cb, LV_EVENT_CLICKED, (void *)(intptr_t)k->key);

        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, k->text);
        lv_obj_set_style_text_font(l, WATCH_FONT_BOLD, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(k->fg), 0);
        if (k->span == 2) {                      // широкий ноль: цифра слева, как на телефоне
            lv_obj_align(l, LV_ALIGN_LEFT_MID, bh / 2 - 8, 0);
        } else {
            lv_obj_center(l);
        }

        if (k->key == 'C') clear_lbl = l;
        for (int j = 0; j < 4; j++) if (k->key == OPS[j]) { op_btn[j] = b; op_lbl[j] = l; }
    }

    show();
    lv_screen_load_anim(scr, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
}
