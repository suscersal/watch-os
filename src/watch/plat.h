// plat.h - платформенные мелочи (источник случайных чисел). Для ESP32 будет esp_fill_random().
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool plat_random(void *buf, size_t n);   // криптостойкие случайные байты

// Локальный IPv4-адрес в Wi-Fi/LAN для показа на экране синхронизации ("192.168.0.5"). false, если сети нет.
bool plat_local_ip(char *buf, size_t n);

// Аппаратные часы (RTC). ESP32: PCF85063 на плате Waveshare; ПК: системное время (запись игнорируется).
// read - UTC epoch; false, если часы не заведены (например, у RTC сброшен флаг остановки генератора).
bool plat_rtc_read(int64_t *epoch_utc);
void plat_rtc_write(int64_t epoch_utc);

// Заряд батареи, 0..100. false, если батареи нет или её не удалось прочитать (значок в строке состояния скрывается).
// ПК: переменная окружения WATCH_BATTERY="73" или "73+" (с зарядкой) для проверки интерфейса.
bool plat_battery(int *pct, bool *charging);
