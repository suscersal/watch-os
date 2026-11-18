// pinpad.h - цифровая клавиатура для PIN: заголовок, точки, сообщение, кнопки 0-9, стереть и OK.
#pragma once
#include "lvgl/lvgl.h"
#include <stdbool.h>

// Вызывается по нажатию OK (если введено >= 4 цифр) или автоматически, когда введено auto_len цифр.
// Буфер pin действителен только внутри вызова. После вызова поле ввода уже очищено.
typedef void (*pinpad_cb)(const char *pin, void *ud);

lv_obj_t *pinpad_create(lv_obj_t *parent, int auto_len, pinpad_cb cb, void *ud);
void pinpad_set_title(lv_obj_t *pad, const char *title);
void pinpad_set_message(lv_obj_t *pad, const char *msg, uint32_t color);
void pinpad_set_enabled(lv_obj_t *pad, bool enabled);
void pinpad_clear(lv_obj_t *pad);
