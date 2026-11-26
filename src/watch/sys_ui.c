#include "sys_ui.h"
#include "watch_fonts.h"
#include "ws_style.h"
#include <string.h>

static lv_obj_t   *kb;
static lv_obj_t   *toast_obj;
static lv_timer_t *toast_timer;
static lv_obj_t   *kb_scr;      // экран, который сжат под клавиатуру
static int         kb_lang;     // 0 = EN, 1 = RU

void sys_ui_init(void)
{
    // шрифт по умолчанию для системных слоёв (тост, подтверждение, клавиатура, блокировка)
    lv_obj_set_style_text_font(lv_layer_top(), WATCH_FONT_BODY, 0);
    lv_obj_set_style_text_font(lv_layer_sys(), WATCH_FONT_BODY, 0);
}

// ---------- клавиатура с крупными клавишами ----------
// Раскладка обычная (QWERTY / ЙЦУКЕН), каждый ряд разрезан пополам: на первой странице левые половины рядов,
// после нажатия ">>" - правые, "<<" возвращает назад. Так клавиши остаются крупными, а буквы - там, где их ждут пальцы.
// Третья страница: цифры, знаки, курсор, Aa (заглавная на одну букву) и смена языка.
#define BKSP LV_SYMBOL_BACKSPACE

typedef struct { const char *t; uint8_t w; } kb_key_t;     // t == NULL - конец строки клавиш; t == "" и w == 0 - конец раскладки
#define KB_ROW         { NULL, 0 }
#define KB_END         { "", 0 }

static const kb_key_t kb_en_a[] = {      // левая половина: q w e r t / a s d f g / z x c v
    {"q",1},{"w",1},{"e",1},{"r",1},{"t",1}, KB_ROW,
    {"a",1},{"s",1},{"d",1},{"f",1},{"g",1}, KB_ROW,
    {"z",1},{"x",1},{"c",1},{"v",1},{BKSP,1}, KB_ROW,
    {">>",1},{" ",2},{"123",1},{LV_SYMBOL_OK,1}, KB_END };
static const kb_key_t kb_en_b[] = {      // правая половина: y u i o p / h j k l ' / b n m
    {"y",1},{"u",1},{"i",1},{"o",1},{"p",1}, KB_ROW,
    {"h",1},{"j",1},{"k",1},{"l",1},{"'",1}, KB_ROW,
    {"b",1},{"n",1},{"m",1},{BKSP,2}, KB_ROW,
    {"<<",1},{" ",2},{"123",1},{LV_SYMBOL_OK,1}, KB_END };
static const kb_key_t kb_ru_a[] = {      // левая половина: й ц у к е н / ф ы в а п р / я ч с м и
    {"й",1},{"ц",1},{"у",1},{"к",1},{"е",1},{"н",1}, KB_ROW,
    {"ф",1},{"ы",1},{"в",1},{"а",1},{"п",1},{"р",1}, KB_ROW,
    {"я",1},{"ч",1},{"с",1},{"м",1},{"и",1},{BKSP,1}, KB_ROW,
    {">>",1},{" ",3},{"123",1},{LV_SYMBOL_OK,1}, KB_END };
static const kb_key_t kb_ru_b[] = {      // правая половина: г ш щ з х ъ / о л д ж э ё / т ь б ю
    {"г",1},{"ш",1},{"щ",1},{"з",1},{"х",1},{"ъ",1}, KB_ROW,
    {"о",1},{"л",1},{"д",1},{"ж",1},{"э",1},{"ё",1}, KB_ROW,
    {"т",1},{"ь",1},{"б",1},{"ю",1},{BKSP,2}, KB_ROW,
    {"<<",1},{" ",3},{"123",1},{LV_SYMBOL_OK,1}, KB_END };
static const kb_key_t kb_sym[] = {
    {"1",1},{"2",1},{"3",1},{"4",1},{"5",1}, KB_ROW,
    {"6",1},{"7",1},{"8",1},{"9",1},{"0",1}, KB_ROW,
    {".",1},{",",1},{"!",1},{"?",1},{"-",1}, KB_ROW,
    {"@",1},{":",1},{"_",1},{"/",1},{"#",1}, KB_ROW,
    {LV_SYMBOL_LEFT,1},{LV_SYMBOL_RIGHT,1},{"Aa",1},{"@LANG",1},{BKSP,1}, KB_ROW,
    {"abc",1},{" ",3},{LV_SYMBOL_OK,1}, KB_END };

#define KB_MAX_KEYS 48
static const char *kb_map[KB_MAX_KEYS + 16];            // метки и "\n" между строками, в конце ""
static lv_buttonmatrix_ctrl_t kb_ctrl[KB_MAX_KEYS];
static char kb_txt[KB_MAX_KEYS][4];                     // заглавные буквы и подпись языка
static int  kb_page;                                    // 0 - левые половины рядов, 1 - правые, 2 - цифры и знаки
static int  kb_lpage;                                   // последняя страница с буквами (куда возвращает "abc")
static bool kb_shift;                                   // следующая буква заглавная

static bool kb_is_letter(const char *t)
{
    unsigned char c = (unsigned char)t[0];
    if (t[1] == '\0') return c >= 'a' && c <= 'z';
    return (c == 0xD0 || c == 0xD1) && t[2] == '\0';     // кириллица в UTF-8: 2 байта
}

static void kb_to_upper(const char *t, char *out)
{
    unsigned char c = (unsigned char)t[0], d = (unsigned char)t[1];
    if (d == '\0') { out[0] = (char)(c - 32); out[1] = '\0'; return; }
    out[0] = (char)0xD0;
    if (c == 0xD0)      out[1] = (char)(d - 0x20);      // а-п (0xD0 0xB0-0xBF) -> А-П
    else if (d == 0x91) out[1] = (char)0x81;             // ё -> Ё
    else                out[1] = (char)(d + 0x20);       // р-я (0xD1 0x80-0x8F) -> Р-Я
    out[2] = '\0';
}

static void kb_apply(void)
{
    const kb_key_t *tbl = kb_page == 2 ? kb_sym : kb_lang ? (kb_page ? kb_ru_b : kb_ru_a) : (kb_page ? kb_en_b : kb_en_a);
    int n = 0, nk = 0;
    for (const kb_key_t *k = tbl; k->w || k->t == NULL || k->t[0]; k++) {
        if (!k->t) { kb_map[n++] = "\n"; continue; }
        if (nk >= KB_MAX_KEYS) break;
        const char *t = k->t;
        lv_buttonmatrix_ctrl_t ctrl = LV_BUTTONMATRIX_CTRL_NO_REPEAT | LV_BUTTONMATRIX_CTRL_CLICK_TRIG | k->w;
        if (strcmp(t, "@LANG") == 0) { t = kb_lang ? "EN" : "RU"; }
        else if (kb_shift && kb_is_letter(t)) { kb_to_upper(t, kb_txt[nk]); t = kb_txt[nk]; }
        else if (strcmp(t, "Aa") == 0 && kb_shift) ctrl |= LV_BUTTONMATRIX_CTRL_CHECKED;
        kb_map[n++] = t;
        kb_ctrl[nk++] = ctrl;
    }
    kb_map[n] = "";
    lv_buttonmatrix_set_map(kb, kb_map);
    lv_buttonmatrix_set_ctrl_map(kb, kb_ctrl);
}

static void kb_reset(void) { kb_page = 0; kb_lpage = 0; kb_shift = false; kb_apply(); }

static void kb_key_cb(lv_event_t *e)
{
    lv_obj_t *k = lv_event_get_target(e);
    uint32_t id = lv_buttonmatrix_get_selected_button(k);
    if (id == LV_BUTTONMATRIX_BUTTON_NONE) return;
    const char *t = lv_buttonmatrix_get_button_text(k, id);
    if (!t) return;
    lv_obj_t *ta = lv_keyboard_get_textarea(k);

    // Копию текста берём сразу: перестройка раскладки меняет буферы, на которые указывает t.
    char key[8];
    strncpy(key, t, sizeof key - 1);
    key[sizeof key - 1] = '\0';

    if (strcmp(key, ">>") == 0 || strcmp(key, "<<") == 0) {
        kb_page = kb_lpage = kb_page ? 0 : 1;                                 // другая половина раскладки
        kb_apply();
    }
    else if (strcmp(key, "123") == 0)         { kb_lpage = kb_page; kb_page = 2; kb_apply(); }
    else if (strcmp(key, "abc") == 0)         { kb_page = kb_lpage; kb_apply(); }
    else if (strcmp(key, "Aa") == 0)          { kb_shift = !kb_shift; kb_apply(); }
    else if (strcmp(key, "RU") == 0)          { kb_lang = 1; kb_lpage = 0; kb_apply(); }
    else if (strcmp(key, "EN") == 0)          { kb_lang = 0; kb_lpage = 0; kb_apply(); }
    else if (strcmp(key, LV_SYMBOL_OK) == 0)    lv_obj_send_event(k, LV_EVENT_READY, NULL);
    else if (!ta)                               return;
    else if (strcmp(key, BKSP) == 0)            lv_textarea_delete_char(ta);
    else if (strcmp(key, LV_SYMBOL_LEFT) == 0)  lv_textarea_cursor_left(ta);
    else if (strcmp(key, LV_SYMBOL_RIGHT) == 0) lv_textarea_cursor_right(ta);
    else {
        lv_textarea_add_text(ta, key);
        if (kb_shift) { kb_shift = false; kb_apply(); }                       // заглавная только на одну букву
    }
}

static void kb_cb(lv_event_t *e)
{
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_READY || c == LV_EVENT_CANCEL) sys_keyboard_hide();
}

bool sys_keyboard_visible(void)
{
    return kb && !lv_obj_has_flag(kb, LV_OBJ_FLAG_HIDDEN);
}

void sys_keyboard_press_outside(lv_obj_t *target)
{
    if (!sys_keyboard_visible()) return;
    lv_obj_t *ta = lv_keyboard_get_textarea(kb);
    for (lv_obj_t *o = target; o; o = lv_obj_get_parent(o))
        if (o == kb || o == ta) return;   // нажали на клавиатуру или на само поле
    sys_keyboard_hide();
}

static void indev_press_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    sys_keyboard_press_outside(lv_indev_get_active_obj());
}

static void kb_create(void)
{
    kb = lv_keyboard_create(lv_layer_top());
    lv_obj_remove_event_cb(kb, lv_keyboard_def_event_cb);   // своя обработка клавиш
    lv_obj_add_event_cb(kb, kb_key_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(kb, kb_cb, LV_EVENT_ALL, NULL);
    lv_obj_set_size(kb, lv_pct(100), lv_pct(56));        // крупные клавиши: больше половины экрана
    lv_obj_set_style_text_font(kb, WATCH_FONT_LARGE, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, lv_color_hex(WS_BG), 0);
    lv_obj_set_style_pad_all(kb, 8, 0);
    lv_obj_set_style_pad_gap(kb, 8, 0);
    lv_obj_set_style_border_width(kb, 0, 0);
    lv_obj_set_style_bg_color(kb, lv_color_hex(WS_FILL), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, lv_color_hex(WS_FILL2), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(kb, lv_color_white(), LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 18, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, lv_color_hex(WS_BLUE), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);   // тема задаёт свой шрифт кнопкам: без этого кириллица не отрисуется
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);

    // Нажатие вне клавиатуры и поля закрывает её (иначе случайно открытую клавиатуру нечем убрать, кроме клавиши OK).
    for (lv_indev_t *i = lv_indev_get_next(NULL); i; i = lv_indev_get_next(i))
        lv_indev_add_event_cb(i, indev_press_cb, LV_EVENT_PRESSED, NULL);

}

void sys_keyboard_set_lang(int ru) { kb_lang = ru ? 1 : 0; }
int  sys_keyboard_get_lang(void)   { return kb_lang; }

void sys_keyboard_attach(lv_obj_t *ta)
{
    if (!kb) kb_create();
    lv_keyboard_set_textarea(kb, ta);
    kb_reset();
    lv_obj_remove_flag(kb, LV_OBJ_FLAG_HIDDEN);

    // Размер самого экрана менять нельзя, поэтому добавляем ему нижний отступ на высоту клавиатуры:
    // тело (flex_grow) сжимается, поле ввода прокручивается в видимую область.
    lv_obj_t *scr = lv_screen_active();
    if (kb_scr != scr) {
        kb_scr = scr;
        lv_obj_update_layout(kb);
        lv_obj_set_style_pad_bottom(scr, lv_obj_get_height(kb), 0);
        lv_obj_update_layout(scr);
    }
    lv_obj_scroll_to_view_recursive(ta, LV_ANIM_OFF);
}

void sys_keyboard_hide(void)
{
    if (!kb) return;
    lv_keyboard_set_textarea(kb, NULL);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    if (kb_scr && lv_obj_is_valid(kb_scr)) {
        lv_obj_set_style_pad_bottom(kb_scr, 0, 0);
        lv_obj_update_layout(kb_scr);
    }
    kb_scr = NULL;
}

static void toast_timer_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (toast_obj) { lv_obj_delete(toast_obj); toast_obj = NULL; }
    toast_timer = NULL;   // таймер одноразовый, удалится сам
}

void sys_toast(const char *text)
{
    if (toast_timer) { lv_timer_delete(toast_timer); toast_timer = NULL; }
    if (toast_obj)   { lv_obj_delete(toast_obj); toast_obj = NULL; }

    toast_obj = lv_obj_create(lv_layer_top());
    lv_obj_remove_flag(toast_obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(toast_obj, LV_OBJ_FLAG_CLICKABLE);   // тост не перехватывает касания
    lv_obj_set_width(toast_obj, lv_pct(85));
    lv_obj_set_height(toast_obj, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(toast_obj, lv_color_hex(WS_FILL), 0);
    lv_obj_set_style_bg_opa(toast_obj, LV_OPA_90, 0);
    lv_obj_set_style_radius(toast_obj, 24, 0);
    lv_obj_set_style_border_width(toast_obj, 0, 0);
    lv_obj_set_style_pad_all(toast_obj, 12, 0);
    lv_obj_align(toast_obj, LV_ALIGN_BOTTOM_MID, 0, -24);

    lv_obj_t *l = lv_label_create(toast_obj);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);

    toast_timer = lv_timer_create(toast_timer_cb, 2200, NULL);
    lv_timer_set_repeat_count(toast_timer, 1);
}

// ---------- подтверждение ----------
static lv_obj_t      *dlg;
static sys_confirm_cb dlg_cb;
static void          *dlg_ud;

static void dlg_finish(bool ok)
{
    if (!dlg) return;
    sys_confirm_cb cb = dlg_cb;
    void *ud = dlg_ud;
    lv_obj_delete_async(dlg);
    dlg = NULL; dlg_cb = NULL; dlg_ud = NULL;
    if (cb) cb(ok, ud);
}

static void dlg_btn_cb(lv_event_t *e) { dlg_finish(lv_event_get_user_data(e) != NULL); }

void sys_confirm_close(void)
{
    if (!dlg) return;
    lv_obj_delete_async(dlg);
    dlg = NULL; dlg_cb = NULL; dlg_ud = NULL;
}

void sys_confirm(const char *title, const char *msg, sys_confirm_cb cb, void *ud)
{
    sys_confirm_close();
    sys_keyboard_hide();
    dlg_cb = cb; dlg_ud = ud;

    dlg = lv_obj_create(lv_layer_top());                 // затемнение, перехватывает все касания
    lv_obj_remove_style_all(dlg);
    lv_obj_set_size(dlg, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(dlg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(dlg, LV_OPA_60, 0);
    lv_obj_add_flag(dlg, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *card = lv_obj_create(dlg);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(card, lv_pct(88));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_center(card);
    lv_obj_set_style_bg_color(card, lv_color_hex(WS_CARD), 0);
    lv_obj_set_style_radius(card, 28, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_set_style_pad_row(card, 12, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *t = lv_label_create(card);
    lv_label_set_text(t, title);
    lv_obj_set_width(t, lv_pct(100));
    lv_obj_set_style_text_color(t, lv_color_white(), 0);
    lv_obj_set_style_text_font(t, WATCH_FONT_LARGE, 0);
    lv_obj_t *m = lv_label_create(card);
    lv_label_set_long_mode(m, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(m, lv_pct(100));
    lv_label_set_text(m, msg);
    lv_obj_set_style_text_color(m, lv_color_hex(WS_TEXT2), 0);

    lv_obj_t *row = lv_obj_create(card);
    lv_obj_remove_style_all(row);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_gap(row, 10, 0);
    for (int yes = 0; yes < 2; yes++) {
        lv_obj_t *b = lv_button_create(row);
        lv_obj_set_height(b, 52);
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_style_radius(b, 26, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(yes ? ws_accent() : WS_FILL), 0);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, yes ? "OK" : "Cancel");
        lv_obj_center(l);
        lv_obj_add_event_cb(b, dlg_btn_cb, LV_EVENT_CLICKED, yes ? (void *)1 : NULL);
    }
}

// ---------- каркас экрана ----------
// Свайп вправо - "назад": на главном экране приложения это выход, на вложенном - возврат на предыдущий экран
// (кнопка "назад" на вложенных экранах остаётся).
static void swipe_back_cb(lv_event_t *e)
{
    lv_indev_t *in = lv_indev_active();
    // Жест начался на ползунке/дуге (громкость, яркость): это перетаскивание значения, а не «назад».
    lv_obj_t *origin = lv_event_get_target_obj(e);
    if (origin && (lv_obj_check_type(origin, &lv_slider_class) || lv_obj_check_type(origin, &lv_arc_class))) return;
    if (in && lv_indev_get_gesture_dir(in) == LV_DIR_RIGHT) {
        lv_indev_wait_release(in);          // свайп не должен ещё и нажать кнопку, над которой закончился
        lv_event_cb_t cb = lv_event_get_user_data(e);
        if (cb) cb(e);
    }
}

// Как на iPhone: свайп вверх от нижнего края выходит из приложения. Узкая прозрачная полоса внизу ловит только такие
// свайпы (остальные жесты и нажатия её не касаются), а тонкая «планка» показывает, откуда тянуть.
#define HOME_BAR_H   96   // зона свайпа: нижние ~20% экрана, чтобы не нужно было попадать в самый край (на телефоне его перехватывает Android)
#define HOME_PILL_UP 30   // планка приподнята от края
// Свой распознаватель вместо жеста LVGL: тот сбрасывает накопленное движение, если между двумя опросами мыши палец почти не
// сдвинулся, и на «рваном» свайпе (Termux:X11, медленная рука) не срабатывает вовсе. Здесь важно только, куда ушёл палец.
#define HOME_SWIPE_MIN 70    // на столько пикселей вверх надо уйти от точки нажатия
static bool       g_exit_up;       // идёт выход свайпом вверх: sys_exit_anim() вернёт «вверх»
static lv_point_t g_home_start;
static bool       g_home_done;

// Полоса внизу НЕ кликабельна: иначе она перехватывала бы нажатия кнопок, стоящих в нижних 96 px (в «Музыке» там
// кнопки перемешивания, громкости и очереди). Нажатия идут к кнопкам как обычно, а свайп вверх от нижнего края
// отслеживается отдельно: PRESSED (индев) запоминает точку и «взводит» распознаватель, таймер следит за пальцем.
static void home_zone_marker_cb(lv_event_t *e) { LV_UNUSED(e); }   // метка: у экрана есть полоса, user_data = колбэк выхода

static lv_obj_t *home_zone_of_active(lv_event_cb_t *exit_cb)
{
    lv_obj_t *scr = lv_screen_active();
    if (!scr) return NULL;
    for (int32_t i = (int32_t)lv_obj_get_child_count(scr) - 1; i >= 0; i--) {
        lv_obj_t *ch = lv_obj_get_child(scr, i);
        uint32_t n = lv_obj_get_event_count(ch);
        for (uint32_t k = 0; k < n; k++) {
            lv_event_dsc_t *d = lv_obj_get_event_dsc(ch, k);
            if (lv_event_dsc_get_cb(d) == home_zone_marker_cb) {
                if (exit_cb) *exit_cb = (lv_event_cb_t)lv_event_dsc_get_user_data(d);
                return ch;
            }
        }
    }
    return NULL;
}

static lv_indev_t *g_home_in;
static bool        g_home_armed;
static lv_timer_t *g_home_timer;

static void home_indev_cb(lv_event_t *e)
{
    lv_event_code_t c = lv_event_get_code(e);
    lv_indev_t *in = lv_event_get_target(e);   // у событий самого индева цель - сам индев (lv_event_get_indev вернул бы объект)
    if (!in) return;
    if (c == LV_EVENT_RELEASED) { g_home_armed = false; return; }
    if (c != LV_EVENT_PRESSED) return;
    lv_obj_t *zone = home_zone_of_active(NULL);
    g_home_armed = false;
    if (!zone) return;
    lv_point_t p;
    lv_indev_get_point(in, &p);
    lv_area_t a;
    lv_obj_get_coords(zone, &a);
    if (p.y < a.y1) return;                      // касание началось выше полосы: это обычное нажатие или прокрутка
    g_home_in = in;
    g_home_start = p;
    g_home_done = false;
    g_home_armed = true;
}

static void home_timer_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (!g_home_armed || g_home_done || !g_home_in) return;
    if (lv_indev_get_state(g_home_in) != LV_INDEV_STATE_PRESSED) { g_home_armed = false; return; }
    lv_point_t p;
    lv_indev_get_point(g_home_in, &p);
    int dy = g_home_start.y - p.y, dx = p.x - g_home_start.x;
    if (dy >= HOME_SWIPE_MIN && dy > (dx < 0 ? -dx : dx)) {      // вверх и скорее вертикально, чем вбок
        lv_event_cb_t cb = NULL;
        if (!home_zone_of_active(&cb) || !cb) { g_home_armed = false; return; }
        g_home_done = true;
        g_home_armed = false;
        lv_indev_wait_release(g_home_in);                         // остаток движения не должен нажать кнопку под пальцем
        g_exit_up = true;
        cb(NULL);                                                 // анимацию выбирают внутри cb, синхронно
        g_exit_up = false;
    }
}

static void home_swipe_register(void)
{
    if (g_home_timer) return;
    lv_indev_t *in = lv_indev_get_next(NULL);
    if (!in) return;                                              // устройство ввода ещё не создано - повторим при следующем экране
    for (; in; in = lv_indev_get_next(in)) {
        lv_indev_add_event_cb(in, home_indev_cb, LV_EVENT_PRESSED, NULL);
        lv_indev_add_event_cb(in, home_indev_cb, LV_EVENT_RELEASED, NULL);
    }
    g_home_timer = lv_timer_create(home_timer_cb, 30, NULL);
}

lv_screen_load_anim_t sys_exit_anim(void)
{
    return g_exit_up ? LV_SCREEN_LOAD_ANIM_OUT_TOP : LV_SCREEN_LOAD_ANIM_MOVE_RIGHT;
}

static void add_home_bar(lv_obj_t *scr, lv_event_cb_t exit_cb)
{
    home_swipe_register();
    lv_obj_t *zone = lv_obj_create(scr);
    lv_obj_remove_style_all(zone);
    lv_obj_remove_flag(zone, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);   // не перехватывает нажатия (см. выше)
    lv_obj_add_flag(zone, LV_OBJ_FLAG_FLOATING);                // вне flex-раскладки экрана, поверх содержимого
    lv_obj_set_size(zone, lv_pct(100), HOME_BAR_H);
    lv_obj_align(zone, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(zone, home_zone_marker_cb, LV_EVENT_DELETE, exit_cb);
    lv_obj_t *pill = lv_obj_create(zone);
    lv_obj_remove_style_all(pill);
    lv_obj_remove_flag(pill, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(pill, 120, 5);
    lv_obj_set_style_radius(pill, 3, 0);
    lv_obj_set_style_bg_color(pill, lv_color_hex(0x8E8E93), 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_50, 0);
    lv_obj_align(pill, LV_ALIGN_BOTTOM_MID, 0, -HOME_PILL_UP);
}

// Общая сборка экрана. root=true: главный экран приложения (название цветом приложения слева, время справа,
// выход свайпом вправо). root=false: вложенный экран (круглая кнопка "назад" слева, название по центру).
static lv_obj_t *g_right_lbl;

static lv_obj_t *screen_build(const char *title, lv_event_cb_t back_cb, lv_obj_t **body_out, bool root, const char *right_text)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(scr, WATCH_FONT_BODY, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_pad_row(scr, 0, 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_remove_style_all(bar);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(bar, lv_pct(100));

    if (root) {
        lv_obj_set_height(bar, 62);
        lv_obj_t *t = lv_label_create(bar);
        lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
        lv_obj_set_width(t, 240);
        lv_label_set_text(t, title);
        lv_obj_set_style_text_font(t, WATCH_FONT_BOLD_S, 0);
        lv_obj_set_style_text_color(t, lv_color_hex(ws_accent()), 0);
        lv_obj_align(t, LV_ALIGN_TOP_LEFT, 42, 29);
        lv_obj_t *tm = NULL;
        if (right_text) {                                   // вместо времени свой текст (например, длительность таймера)
            tm = lv_label_create(bar);
            lv_label_set_text(tm, right_text);
            lv_obj_set_style_text_color(tm, lv_color_hex(WS_TEXT2), 0);
            lv_obj_set_style_text_font(tm, WATCH_FONT_BODY, 0);
            lv_obj_set_style_text_align(tm, LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_set_width(tm, 130);
            lv_obj_align(tm, LV_ALIGN_TOP_RIGHT, -42, 28);
        } else {
            tm = NULL;                                      // время показывает общая строка состояния
        }
        g_right_lbl = tm;
        if (back_cb) {
            lv_obj_add_event_cb(scr, swipe_back_cb, LV_EVENT_GESTURE, back_cb);
            lv_obj_set_user_data(scr, (void *)back_cb);       // для sys_screen_exit()
        }
    } else {
        lv_obj_set_height(bar, 110);
        lv_obj_t *back = ws_round_button(bar, 56, LV_SYMBOL_LEFT, WS_CARD, 0xFFFFFF, NULL, back_cb, NULL);
        lv_obj_align(back, LV_ALIGN_TOP_LEFT, 24, 29);   // ниже общей строки состояния
        if (back_cb) lv_obj_add_event_cb(scr, swipe_back_cb, LV_EVENT_GESTURE, back_cb);   // user_data не ставим: sys_screen_exit() работает только для главных экранов
        lv_obj_t *t = lv_label_create(bar);
        lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
        lv_obj_set_width(t, 220);
        lv_label_set_text(t, title);
        lv_obj_set_style_text_font(t, WATCH_FONT_BOLD, 0);
        lv_obj_set_style_text_color(t, lv_color_white(), 0);
        lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(t, LV_ALIGN_TOP_MID, 28, 41);
    }

    lv_obj_t *body = lv_obj_create(scr);
    lv_obj_set_width(body, lv_pct(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_radius(body, 0, 0);
    lv_obj_set_style_pad_all(body, 0, 0);
    lv_obj_set_style_pad_left(body, 20, 0);
    lv_obj_set_style_pad_right(body, 20, 0);
    lv_obj_set_style_pad_bottom(body, 48, 0);   // последние строки списка доезжают выше полоски "домой" и скруглённых углов
    lv_obj_set_style_pad_row(body, 12, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    *body_out = body;
    if (root && back_cb) add_home_bar(scr, back_cb);
    return scr;
}

bool sys_screen_exit(lv_obj_t *scr)
{
    lv_event_cb_t cb = (lv_event_cb_t)lv_obj_get_user_data(scr);
    if (!cb) return false;
    cb(NULL);
    return true;
}

lv_obj_t *sys_screen_create(const char *title, lv_event_cb_t back_cb, lv_obj_t **body_out)
{
    return screen_build(title, back_cb, body_out, false, NULL);
}

lv_obj_t *sys_screen_create_root(const char *title, lv_event_cb_t exit_cb, lv_obj_t **body_out)
{
    return screen_build(title, exit_cb, body_out, true, NULL);
}

lv_obj_t *sys_screen_create_root_text(const char *title, lv_event_cb_t exit_cb, lv_obj_t **body_out, const char *right_text, lv_obj_t **right_out)
{
    lv_obj_t *scr = screen_build(title, exit_cb, body_out, true, right_text);
    if (right_out) *right_out = g_right_lbl;
    return scr;
}
