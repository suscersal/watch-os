// pm_esp.h - энергосбережение процессора: динамическая частота (DFS).
#pragma once
void pm_esp_start(void);   // после инициализации периферии; без CONFIG_PM_ENABLE ничего не делает

#include "lvgl.h"
// Держит максимальную частоту процессора, пока LVGL рисует кадр (иначе при DFS отрисовка идёт на 80 МГц).
void pm_esp_attach_display(lv_display_t *disp);
