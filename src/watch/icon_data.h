// icon_data.h - иконки приложений (круглые, 112x112, RGB565A8) во flash.
#pragma once
#include <string.h>
#include "lvgl_inc.h"

// Картинка по ключу иконки из манифеста; NULL, если такой нет (тогда рисуется глиф шрифта).
const lv_image_dsc_t *icon_image(const char *key);
