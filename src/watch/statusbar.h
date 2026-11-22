// statusbar.h - строка состояния: время, значок Wi-Fi, заряд батареи. Обновляется сама, пока жив родитель.
#pragma once
#include <stdbool.h>
#include "lvgl_inc.h"

// Создаёт строку вверху по центру parent. Не принимает нажатия (они проходят сквозь неё).
lv_obj_t *statusbar_create(lv_obj_t *parent, bool show_time);
