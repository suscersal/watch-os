// plat.h - платформенные мелочи (источник случайных чисел). Для ESP32 будет esp_fill_random().
#pragma once
#include <stdbool.h>
#include <stddef.h>

bool plat_random(void *buf, size_t n);   // криптостойкие случайные байты

// Локальный IPv4-адрес в Wi-Fi/LAN для показа на экране синхронизации ("192.168.0.5"). false, если сети нет.
bool plat_local_ip(char *buf, size_t n);
