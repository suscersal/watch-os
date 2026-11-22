// watchface.h - циферблат: большое время, дата, значки состояния. Лежит поверх сетки приложений в лаунчере.
// Свайп вверх или касание открывают сетку; свайп вниз на сетке (когда она не прокручивается) возвращает циферблат.
// Показывается заново, когда экран включается после выключения (если открыта сетка, а не приложение).
#pragma once
#include <stdbool.h>
#include "lvgl_inc.h"

void watchface_show(lv_obj_t *launcher);     // launcher - экран с сеткой; вызывать из потока LVGL
void watchface_hide(bool animate);
bool watchface_visible(void);
