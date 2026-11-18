// lvgl_inc.h - единая точка подключения LVGL.
// ПК (CMake шаблона): lvgl лежит в подпапке проекта, подключается как "lvgl/lvgl.h".
// ESP-IDF: компонент lvgl подключается как "lvgl.h" (определён LV_LVGL_H_INCLUDE_SIMPLE).
#pragma once
#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif
