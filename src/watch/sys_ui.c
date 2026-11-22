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

// ---------- раскладки: EN и RU, строчные и заглавные ----------
#define BKSP LV_SYMBOL_BACKSPACE
static const char *const map_en_l[] = {
    "q","w","e","r","t","y","u","i","o","p",BKSP,"\n",
    "ABC","a","s","d","f","g","h","j","k","l","\n",
    "_","-","z","x","c","v","b","n","m",".",",",":","\n",
    "1#","RU",LV_SYMBOL_LEFT," ",LV_SYMBOL_RIGHT,LV_SYMBOL_OK,""};
static const char *const map_en_u[] = {
    "Q","W","E","R","T","Y","U","I","O","P",BKSP,"\n",
    "abc","A","S","D","F","G","H","J","K","L","\n",
    "_","-","Z","X","C","V","B","N","M",".",",",":","\n",
    "1#","RU",LV_SYMBOL_LEFT," ",LV_SYMBOL_RIGHT,LV_SYMBOL_OK,""};
static const char *const map_ru_l[] = {
    "й","ц","у","к","е","н","г","ш","щ","з","х",BKSP,"\n",
    "ABC","ф","ы","в","а","п","р","о","л","д","ж","э","\n",
    "ё","я","ч","с","м","и","т","ь","б","ю","ъ","\n",
    "1#","EN",LV_SYMBOL_LEFT," ",LV_SYMBOL_RIGHT,".",LV_SYMBOL_OK,""};
static const char *const map_ru_u[] = {
    "Й","Ц","У","К","Е","Н","Г","Ш","Щ","З","Х",BKSP,"\n",
    "abc","Ф","Ы","В","А","П","Р","О","Л","Д","Ж","Э","\n",
    "Ё","Я","Ч","С","М","И","Т","Ь","Б","Ю","Ъ","\n",
    "1#","EN",LV_SYMBOL_LEFT," ",LV_SYMBOL_RIGHT,".",LV_SYMBOL_OK,""};

#define KB_MAX_KEYS 48
static lv_buttonmatrix_ctrl_t kb_ctrl[4][KB_MAX_KEYS];

static void build_ctrl(const char *const *map, lv_buttonmatrix_ctrl_t *ctrl)
{
    int i = 0;
    for (const char *const *p = map; p[0][0] != '\0' && i < KB_MAX_KEYS; p++) {
        const char *t = *p;
        if (t[0] == '\n') continue;
        int w = 4;                                               // обычная буква
        if (strcmp(t, " ") == 0) w = 12;                         // пробел
        else if ((unsigned char)t[0] == 0xEF || (strlen(t) >= 2 && (unsigned char)t[0] < 0x80)) w = 6;   // символ или "ABC"/"1#"
        ctrl[i++] = (lv_buttonmatrix_ctrl_t)(LV_BUTTONMATRIX_CTRL_NO_REPEAT | LV_BUTTONMATRIX_CTRL_CLICK_TRIG | w);
    }
}

static void kb_show_lower(void) { lv_keyboard_set_mode(kb, kb_lang ? LV_KEYBOARD_MODE_USER_3 : LV_KEYBOARD_MODE_USER_1); }
static void kb_show_upper(void) { lv_keyboard_set_mode(kb, kb_lang ? LV_KEYBOARD_MODE_USER_4 : LV_KEYBOARD_MODE_USER_2); }

static void kb_key_cb(lv_event_t *e)
{
    lv_obj_t *k = lv_event_get_target(e);
    uint32_t id = lv_buttonmatrix_get_selected_button(k);
    if (id == LV_BUTTONMATRIX_BUTTON_NONE) return;
    const char *t = lv_buttonmatrix_get_button_text(k, id);
    if (!t) return;
    lv_obj_t *ta = lv_keyboard_get_textarea(k);
    lv_keyboard_mode_t mode = lv_keyboard_get_mode(k);

    if (strcmp(t, "ABC") == 0)                 kb_show_upper();
    else if (strcmp(t, "abc") == 0)            kb_show_lower();
    else if (strcmp(t, "1#") == 0)             lv_keyboard_set_mode(k, LV_KEYBOARD_MODE_SPECIAL);
    else if (strcmp(t, "RU") == 0)           { kb_lang = 1; kb_show_lower(); }
    else if (strcmp(t, "EN") == 0)           { kb_lang = 0; kb_show_lower(); }
    else if (strcmp(t, LV_SYMBOL_OK) == 0)     lv_obj_send_event(k, LV_EVENT_READY, NULL);
    else if (strcmp(t, LV_SYMBOL_CLOSE) == 0 || strcmp(t, LV_SYMBOL_KEYBOARD) == 0) lv_obj_send_event(k, LV_EVENT_CANCEL, NULL);
    else if (!ta)                              return;
    else if (strcmp(t, BKSP) == 0)             lv_textarea_delete_char(ta);
    else if (strcmp(t, LV_SYMBOL_LEFT) == 0)   lv_textarea_cursor_left(ta);
    else if (strcmp(t, LV_SYMBOL_RIGHT) == 0)  lv_textarea_cursor_right(ta);
    else if (strcmp(t, LV_SYMBOL_NEW_LINE) == 0) { if (!lv_textarea_get_one_line(ta)) lv_textarea_add_char(ta, '\n'); }
    else {
        lv_textarea_add_text(ta, t);
        if (mode == LV_KEYBOARD_MODE_USER_2 || mode == LV_KEYBOARD_MODE_USER_4) kb_show_lower();   // заглавная только на одну букву
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
    lv_obj_set_size(kb, lv_pct(100), lv_pct(45));
    lv_obj_set_style_text_font(kb, WATCH_FONT_BODY, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, lv_color_hex(WS_BG), 0);
    lv_obj_set_style_pad_all(kb, 6, 0);
    lv_obj_set_style_pad_gap(kb, 6, 0);
    lv_obj_set_style_border_width(kb, 0, 0);
    lv_obj_set_style_bg_color(kb, lv_color_hex(WS_FILL), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, lv_color_hex(WS_FILL2), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(kb, lv_color_white(), LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 12, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);   // тема задаёт свой шрифт кнопкам: без этого кириллица не отрисуется
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);

    // Нажатие вне клавиатуры и поля закрывает её (иначе случайно открытую клавиатуру нечем убрать, кроме клавиши OK).
    for (lv_indev_t *i = lv_indev_get_next(NULL); i; i = lv_indev_get_next(i))
        lv_indev_add_event_cb(i, indev_press_cb, LV_EVENT_PRESSED, NULL);

    const char *const *maps[4] = { map_en_l, map_en_u, map_ru_l, map_ru_u };
    lv_keyboard_mode_t modes[4] = { LV_KEYBOARD_MODE_USER_1, LV_KEYBOARD_MODE_USER_2, LV_KEYBOARD_MODE_USER_3, LV_KEYBOARD_MODE_USER_4 };
    for (int i = 0; i < 4; i++) {
        build_ctrl(maps[i], kb_ctrl[i]);
        lv_keyboard_set_map(kb, modes[i], maps[i], kb_ctrl[i]);
    }
}

void sys_keyboard_set_lang(int ru) { kb_lang = ru ? 1 : 0; }
int  sys_keyboard_get_lang(void)   { return kb_lang; }

void sys_keyboard_attach(lv_obj_t *ta)
{
    if (!kb) kb_create();
    lv_keyboard_set_textarea(kb, ta);
    kb_show_lower();
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
// Свайп вправо по главному экрану приложения - выход (на часах нет кнопки "назад" на корневом экране).
static void swipe_back_cb(lv_event_t *e)
{
    lv_indev_t *in = lv_indev_active();
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

static void home_swipe_cb(lv_event_t *e)
{
    lv_event_code_t c = lv_event_get_code(e);
    lv_indev_t *in = lv_indev_active();
    if (!in) return;
    lv_point_t p;
    lv_indev_get_point(in, &p);
    if (c == LV_EVENT_PRESSED) { g_home_start = p; g_home_done = false; return; }
    if (c != LV_EVENT_PRESSING || g_home_done) return;
    int dy = g_home_start.y - p.y, dx = p.x - g_home_start.x;
    if (dy >= HOME_SWIPE_MIN && dy > (dx < 0 ? -dx : dx)) {      // вверх и скорее вертикально, чем вбок
        g_home_done = true;
        lv_indev_wait_release(in);                                // остаток движения не должен нажать кнопку под пальцем
        lv_event_cb_t cb = lv_event_get_user_data(e);
        g_exit_up = true;
        if (cb) cb(e);                                            // анимацию выбирают внутри cb, синхронно
        g_exit_up = false;
    }
}

lv_screen_load_anim_t sys_exit_anim(void)
{
    return g_exit_up ? LV_SCREEN_LOAD_ANIM_OUT_TOP : LV_SCREEN_LOAD_ANIM_MOVE_RIGHT;
}

static void add_home_bar(lv_obj_t *scr, lv_event_cb_t exit_cb)
{
    lv_obj_t *zone = lv_obj_create(scr);
    lv_obj_remove_style_all(zone);
    lv_obj_remove_flag(zone, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(zone, LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_CLICKABLE);   // вне flex-раскладки экрана, поверх содержимого
    lv_obj_set_size(zone, lv_pct(100), HOME_BAR_H);
    lv_obj_align(zone, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(zone, home_swipe_cb, LV_EVENT_ALL, exit_cb);
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
        lv_obj_align(t, LV_ALIGN_TOP_LEFT, 35, 29);
        lv_obj_t *tm;
        if (right_text) {                                   // вместо времени свой текст (например, длительность таймера)
            tm = lv_label_create(bar);
            lv_label_set_text(tm, right_text);
            lv_obj_set_style_text_color(tm, lv_color_hex(WS_TEXT2), 0);
            lv_obj_set_style_text_font(tm, WATCH_FONT_BODY, 0);
            lv_obj_set_style_text_align(tm, LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_set_width(tm, 130);
            lv_obj_align(tm, LV_ALIGN_TOP_RIGHT, -33, 28);
        } else {
            tm = ws_time_label(bar);
            lv_obj_set_style_text_font(tm, WATCH_FONT_BOLD_S, 0);
            lv_obj_align(tm, LV_ALIGN_TOP_RIGHT, -33, 29);
        }
        g_right_lbl = tm;
        if (back_cb) {
            lv_obj_add_event_cb(scr, swipe_back_cb, LV_EVENT_GESTURE, back_cb);
            lv_obj_set_user_data(scr, (void *)back_cb);       // для sys_screen_exit()
        }
    } else {
        lv_obj_set_height(bar, 104);
        lv_obj_t *back = ws_round_button(bar, 56, LV_SYMBOL_LEFT, WS_CARD, 0xFFFFFF, NULL, back_cb, NULL);
        lv_obj_align(back, LV_ALIGN_TOP_LEFT, 24, 23);
        lv_obj_t *t = lv_label_create(bar);
        lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
        lv_obj_set_width(t, 220);
        lv_label_set_text(t, title);
        lv_obj_set_style_text_font(t, WATCH_FONT_BOLD, 0);
        lv_obj_set_style_text_color(t, lv_color_white(), 0);
        lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(t, LV_ALIGN_TOP_MID, 28, 35);
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
