// watch_fonts.h - шрифты с кириллицей и символами LVGL (сгенерированы lv_font_conv, см. fonts/README.md).
#pragma once
#include "lvgl_inc.h"

LV_FONT_DECLARE(watch_font_14)
LV_FONT_DECLARE(watch_font_20)
LV_FONT_DECLARE(watch_font_28)

#define WATCH_FONT_SMALL (&watch_font_14)
#define WATCH_FONT_BODY  (&watch_font_20)   // шрифт по умолчанию
#define WATCH_FONT_LARGE (&watch_font_28)
