// watch_fonts.h - шрифты Inter с кириллицей и символами LVGL (сгенерированы lv_font_conv, см. fonts/README.md).
#pragma once
#include "lvgl_inc.h"

LV_FONT_DECLARE(watch_font_14)
LV_FONT_DECLARE(watch_font_20)
LV_FONT_DECLARE(watch_font_28)
LV_FONT_DECLARE(watch_font_b20)
LV_FONT_DECLARE(watch_font_b28)
LV_FONT_DECLARE(watch_font_d64)
LV_FONT_DECLARE(watch_font_d96)

#define WATCH_FONT_SMALL  (&watch_font_14)
#define WATCH_FONT_BODY   (&watch_font_20)    // шрифт по умолчанию
#define WATCH_FONT_LARGE  (&watch_font_28)
#define WATCH_FONT_BOLD_S (&watch_font_b20)   // жирный 20: названия в карточках
#define WATCH_FONT_BOLD   (&watch_font_b28)   // жирный 28: заголовки экранов, время, название трека
#define WATCH_FONT_DIGITS (&watch_font_d96)   // огромные цифры таймера: только 0-9 : .
#define WATCH_FONT_HUGE   (&watch_font_d64)   // крупные цифры (громкость, таймер): только 0-9 % + - . :

#define WATCH_FONT_TIME   WATCH_FONT_DIGITS   // время на циферблате: тот же крупный шрифт цифр
