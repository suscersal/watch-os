// wifi_ctl.h - управление Wi-Fi для экрана Settings. Реализации: ПК - wifi_ctl_posix.c (имитация сетей, чтобы
// проверять интерфейс без железа), ESP32 - esp32/main/wifi.c. Все вызовы идут из потока LVGL и не блокируют его.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define WIFI_NETS_MAX 16

typedef struct {
    char   ssid[33];
    int8_t rssi;      // дБм, например -55
    bool   secure;    // нужен пароль
} wifi_net_t;

typedef enum {
    WIFI_ST_IDLE,        // не подключены, пробовать нечего
    WIFI_ST_CONNECTING,
    WIFI_ST_CONNECTED,   // есть IP-адрес
    WIFI_ST_FAILED,      // последняя попытка пользователя не удалась (чаще всего неверный пароль)
} wifi_state_t;

void         wifi_ctl_scan(void);                              // запустить поиск сетей в фоне
bool         wifi_ctl_scanning(void);
int          wifi_ctl_results(wifi_net_t *out, int max);       // найденные сети, самые сильные первыми
void         wifi_ctl_connect(const char *ssid, const char *pass);   // pass может быть "" для открытой сети; при успехе сеть сохраняется в settings
void         wifi_ctl_forget(void);                            // отключиться и удалить сохранённую сеть
wifi_state_t wifi_ctl_state(void);
const char  *wifi_ctl_ssid(void);                              // имя сети, к которой подключены или подключаемся ("" если нет)
