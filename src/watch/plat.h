// plat.h - платформенные мелочи (источник случайных чисел). Для ESP32 будет esp_fill_random().
#pragma once
#include <stdbool.h>
#include <stddef.h>

bool plat_random(void *buf, size_t n);   // криптостойкие случайные байты
