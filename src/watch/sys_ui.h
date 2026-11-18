// sys_ui.h - системный UI: тост и клавиатура. Рисуется только ядром, поверх приложений (lv_layer_top).
#pragma once
#include "lvgl_inc.h"

void sys_ui_init(void);                 // один раз при старте: шрифты системных слоёв
void sys_toast(const char *text);
void sys_keyboard_attach(lv_obj_t *textarea);
void sys_keyboard_hide(void);
void sys_keyboard_set_lang(int ru);     // 0 = EN, 1 = RU (по умолчанию EN)
int  sys_keyboard_get_lang(void);

// Окно подтверждения поверх всего. cb(ok, ud) вызывается после нажатия кнопки.
typedef void (*sys_confirm_cb)(bool ok, void *ud);
void sys_confirm(const char *title, const char *msg, sys_confirm_cb cb, void *ud);
void sys_confirm_close(void);   // закрыть без вызова cb

// Системный каркас экрана: верхняя панель с кнопкой "назад" и прокручиваемое тело.
lv_obj_t *sys_screen_create(const char *title, lv_event_cb_t back_cb, lv_obj_t **body_out);
