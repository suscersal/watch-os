// clock.h - время на часах. Источники: аппаратные часы (RTC, plat_rtc_read) при старте и телефон (кадр TIME).
// Дальше время идёт по таймеру LVGL; телефон или Settings могут его поправить, значение пишется обратно в RTC.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

// Один раз при старте, после settings_init: берёт время из RTC и пояс из настроек. Без этого вызова часы «не заданы», пока телефон не пришлёт время.
void clock_init(void);

void clock_set(int64_t epoch_s, int tz_offset_min);   // epoch в UTC и смещение пояса в минутах (Москва +180); пишет в RTC
bool clock_is_set(void);
bool clock_local(struct tm *out);                      // местное время; false, если время ещё не задано
bool clock_utc(int64_t *epoch_s);

// Пояс: если в настройках стоит «auto», clock_set берёт пояс из телефона, иначе остаётся выбранный вручную.
void clock_set_tz(bool is_auto, int tz_offset_min);   // из экрана Settings; применяется сразу и сохраняется
int  clock_tz_min(void);

// "2026-11-19 14:30" (с учётом пояса, если время задано, иначе в UTC с пометкой). Для подписей к фото.
bool clock_format_epoch(int64_t epoch_s, char *out, unsigned n);
