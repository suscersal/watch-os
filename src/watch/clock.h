// clock.h - время на часах. Источника времени нет, пока телефон не пришлёт его (кадр TIME в протоколе синхронизации).
// Дальше время идёт по таймеру LVGL. На ESP32 позже можно заменить на RTC.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

void clock_set(int64_t epoch_s, int tz_offset_min);   // epoch в UTC и смещение пояса в минутах (Москва +180)
bool clock_is_set(void);
bool clock_local(struct tm *out);                      // местное время; false, если время ещё не задано

// "2026-11-19 14:30" (с учётом пояса, если время задано, иначе в UTC с пометкой). Для подписей к фото.
bool clock_format_epoch(int64_t epoch_s, char *out, unsigned n);
