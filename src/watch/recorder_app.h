// recorder_app.h - встроенное приложение "Диктофон": запись с микрофона, список записей, воспроизведение с волной.
#pragma once
#include "lvgl_inc.h"

void recorder_init(const char *root);   // записи лежат в <root>/rec: rN.wav (звук) и rN.pk (длительность, время, волна)
void recorder_open(lv_obj_t *launcher);
