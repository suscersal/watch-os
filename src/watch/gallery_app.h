// gallery_app.h - системное приложение "Gallery": сетка миниатюр и полноэкранный просмотр фото, полученных с телефона.
#pragma once
#include "lvgl_inc.h"

void gallery_open(lv_obj_t *launcher);

// Для тестов:
int  gallery_viewer_index(void);        // номер открытого фото или -1
void gallery_viewer_step(int dir);      // +1 следующее (старее), -1 предыдущее
