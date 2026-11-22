// sys_ui.h - системный UI: тост и клавиатура. Рисуется только ядром, поверх приложений (lv_layer_top).
#pragma once
#include <stdbool.h>
#include "lvgl_inc.h"

void sys_ui_init(void);                 // один раз при старте: шрифты системных слоёв
void sys_toast(const char *text);
void sys_keyboard_attach(lv_obj_t *textarea);
void sys_keyboard_hide(void);
bool sys_keyboard_visible(void);
// Нажатие на объект target: если клавиатура открыта, а нажали не на неё и не на поле ввода - закрыть её.
// Вызывается автоматически для каждого нажатия пальцем или мышью (после создания клавиатуры).
void sys_keyboard_press_outside(lv_obj_t *target);
void sys_keyboard_set_lang(int ru);     // 0 = EN, 1 = RU (по умолчанию EN)
int  sys_keyboard_get_lang(void);

// Окно подтверждения поверх всего. cb(ok, ud) вызывается после нажатия кнопки.
typedef void (*sys_confirm_cb)(bool ok, void *ud);
void sys_confirm(const char *title, const char *msg, sys_confirm_cb cb, void *ud);
void sys_confirm_close(void);   // закрыть без вызова cb

// Системный каркас экрана: верхняя панель с кнопкой "назад" и прокручиваемое тело.
// Вложенный экран: круглая кнопка "назад" слева, название по центру.
lv_obj_t *sys_screen_create(const char *title, lv_event_cb_t back_cb, lv_obj_t **body_out);
// Выйти из приложения так же, как свайпом вправо по главному экрану (для тестов и аппаратной кнопки). false, если экран не главный.
bool sys_screen_exit(lv_obj_t *scr);
// Главный экран приложения: название цветом приложения слева, время справа, выход свайпом вправо (вызывает exit_cb).
lv_obj_t *sys_screen_create_root(const char *title, lv_event_cb_t exit_cb, lv_obj_t **body_out);
// То же, но справа вместо времени свой текст (метка возвращается в right_out, чтобы менять текст).
lv_obj_t *sys_screen_create_root_text(const char *title, lv_event_cb_t exit_cb, lv_obj_t **body_out, const char *right_text, lv_obj_t **right_out);

// Анимация выхода из приложения в лаунчер: при выходе свайпом вверх экран уезжает вверх (как на iPhone), иначе уходит вправо.
// Вызывать прямо в обработчике выхода: lv_screen_load_anim(g_launcher, sys_exit_anim(), 200, 0, true);
lv_screen_load_anim_t sys_exit_anim(void);
