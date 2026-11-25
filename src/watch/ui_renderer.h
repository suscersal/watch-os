// ui_renderer.h - JSON-описание экрана -> виджеты LVGL.
#pragma once
#include "app_runtime.h"

// Строит экран с именем name из r->screens. NULL, если такого экрана нет.
// Верхняя панель с кнопкой "назад" рисуется ядром, приложение её скрыть не может.
lv_obj_t *ui_build_screen(app_run_t *r, const char *name);

// Пересобирает блок math (виджет "math"): дроби FR_A числитель FR_B знаменатель FR_C рисуются стопкой.
void ui_math_fill(lv_obj_t *box, const char *text);
