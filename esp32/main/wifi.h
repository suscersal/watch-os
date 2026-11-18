// wifi.h - подключение к Wi-Fi (режим станции). Данные сети пока из menuconfig (CONFIG_WATCH_WIFI_*).
#pragma once
#include <stdbool.h>

bool wifi_start(void);                    // запускает подключение в фоне; false, если сеть не задана или Wi-Fi не стартовал
bool wifi_is_connected(void);             // есть IP-адрес
bool wifi_wait_connected(int timeout_ms); // ждать IP-адрес не дольше timeout_ms
