// settings_app.h - системное приложение "Settings": Wi-Fi, часовой пояс, ключ API, уведомления с телефона.
#pragma once
#include "lvgl_inc.h"

void settings_open(lv_obj_t *launcher);

// Открыть экран Wi-Fi поверх экрана parent (мастер первой настройки); "назад" вернёт на parent.
void settings_open_wifi_from(lv_obj_t *parent);
