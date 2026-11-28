// stopwatch_app.h - встроенное приложение "Секундомер": старт/стоп, круги, сброс.
// Отсчёт идёт и при закрытом приложении (считается по lv_tick_get, таймеров не держит).
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "lvgl_inc.h"

void stopwatch_open(lv_obj_t *launcher);

bool     stopwatch_running(void);
uint32_t stopwatch_elapsed_ms(void);
