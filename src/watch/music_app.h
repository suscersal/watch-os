// music_app.h - встроенное приложение "Музыка": плеер, громкость, очередь (экраны по макету пользователя).
#pragma once
#include "lvgl_inc.h"

void music_init(const char *root);   // папка с музыкой: <root>/music (файлы .wav)
void music_open(lv_obj_t *launcher);
