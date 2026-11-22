// sync_net.h - TCP-транспорт синхронизации. Слушает порт, пока открыт экран Sync, и подаёт байты в sync_rx.
// Работает на ПК и на ESP32 (lwIP предоставляет те же сокеты). Всё выполняется в потоке LVGL по таймеру.
#pragma once
#include <stdbool.h>
#include <stdint.h>

bool        sync_net_start(uint16_t port);   // port 0 - SYNC_PORT; создаёт новый одноразовый код сопряжения
void        sync_net_stop(void);
bool        sync_net_running(void);
const char *sync_net_code(void);             // 6 цифр, действует до остановки сервера
uint16_t    sync_net_port(void);

// Фоновый приём уведомлений: включает/выключает сервер в соответствии с settings_notify_on() и обновляет код сопряжения.
// Вызывать при старте и после каждого изменения этих настроек. Экран Sync работает независимо (sync_net_start/stop).
bool sync_net_notify_apply(void);
