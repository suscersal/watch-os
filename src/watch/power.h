// power.h - экономия энергии и защита AMOLED от выгорания: затемнение, потом выключение экрана при бездействии.
// Касание выключенного экрана только будит его и дальше не передаётся (случайное нажатие в кармане ничего не запустит).
#pragma once
#include <stdbool.h>
#include "lvgl_inc.h"

void power_init(const char *data_root);                 // читает <root>/power.json, создаёт таймер и чёрную накладку
// Платформа задаёт, как менять яркость (ESP32: яркость AMOLED через BSP). На ПК не нужен: накладка и так чёрная.
void power_set_backend(void (*set_brightness_pct)(int pct));

void power_get_settings(int *dim_s, int *off_s, int *brightness_pct);   // dim_s/off_s: 0 = никогда
void power_set_settings(int dim_s, int off_s, int brightness_pct);      // применяет и сохраняет
void power_preview_brightness(int brightness_pct);                      // применяет без сохранения (ползунок)

bool power_dimmed(void);
bool power_screen_off(void);
void power_wake(void);                                  // включить экран и сбросить таймер бездействия
