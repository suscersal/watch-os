// timer_app.h - встроенное приложение "Таймер": обратный отсчёт, прогресс рисуется по рамке экрана.
// Таймер живёт и при закрытом приложении; строка состояния (statusbar) показывает его и управляет им через функции ниже.
#pragma once
#include <stdint.h>
#include "lvgl_inc.h"

void timer_open(lv_obj_t *launcher);

typedef enum { TIMER_IDLE, TIMER_RUNNING, TIMER_PAUSED, TIMER_DONE } timer_state_t;

timer_state_t timer_state(void);
uint32_t      timer_remaining_ms(void);   // сколько осталось (в IDLE - заданная длительность, в DONE - 0)
uint32_t      timer_total_ms(void);       // заданная длительность
void          timer_pause(void);          // идёт -> пауза
void          timer_resume(void);         // пауза -> идёт; после "время вышло" - запустить заново
void          timer_stop(void);           // сбросить: таймер больше не активен
void          timer_set_change_cb(void (*cb)(void));   // вызывается при каждой смене состояния
