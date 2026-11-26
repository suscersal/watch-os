// ota.h - обновление прошивки "по воздуху": ядро принимает образ (sync_rx.c), запись в неактивный слот делает платформа.
// ESP32: esp32/main/ota_esp.c. На ПК платформы нет: ota_available() == false, и прошивку часы не принимают.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { OTA_OK = 0, OTA_E_FORMAT, OTA_E_STORAGE };     // FORMAT: это не образ этой прошивки; STORAGE: запись не удалась

typedef struct {
    bool (*begin)(uint32_t size);                      // подготовить неактивный слот под образ размером size
    int  (*write)(const uint8_t *d, size_t n);         // OTA_OK или OTA_E_*; куски могут быть любой длины
    int  (*end)(void);                                 // проверить образ целиком и сделать слот загрузочным
    void (*abort)(void);                               // бросить приём
    void (*restart_soon)(void);                        // перезагрузка через пару секунд (ответ успеет уйти на телефон)
} ota_hw_t;

void ota_set_hw(const ota_hw_t *hw);
bool ota_available(void);
bool ota_begin(uint32_t size);
int  ota_write(const uint8_t *d, size_t n);
int  ota_end(void);
void ota_abort(void);
void ota_restart_soon(void);
