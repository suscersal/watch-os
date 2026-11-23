// perf_esp.h - замер скорости отрисовки на плате, чтобы подогнать модель эмулятора (src/hal/emu_slow.c) под реальную плату.
// Включается в menuconfig: watch-os -> "Печатать в лог скорость отрисовки" (CONFIG_WATCH_PERF_LOG). Выключено - функция пустая.
#pragma once
#include "lvgl.h"

void perf_esp_start(lv_display_t *disp);   // один раз после bsp_display_start_with_config()
