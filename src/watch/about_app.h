// about_app.h - системное приложение «О часах»: версия и дата сборки прошивки, платформа, время работы,
// память, хранилище, заряд, microSD и сетевой адрес. Данные берутся из plat_sysinfo() и других plat_*().
#pragma once
#include "lvgl_inc.h"

void about_open(lv_obj_t *launcher);
