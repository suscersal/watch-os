// paired.h - постоянные сопряжения с телефонами: токен вместо кода на каждый раз.
// Токен (16 случайных байт) выдаётся один раз после входа по одноразовому коду с экрана Sync. Телефон хранит токен,
// часы хранят только SHA-256 от него и имя устройства (data/paired.json). Токен можно забыть на экране Sync.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PAIRED_MAX       8       // больше - вытесняется самое старое
#define PAIRED_TOKEN_LEN 16
#define PAIRED_NAME_MAX  32

void paired_init(const char *data_dir);                  // загрузить список; без вызова список пустой и не сохраняется
int  paired_count(void);
bool paired_info(int i, char *name, size_t name_n);      // имя i-го устройства (0 - самое старое)
bool paired_add(const char *name, uint8_t token_out[PAIRED_TOKEN_LEN]);   // новый токен; то же имя заменяет прежнюю запись
bool paired_verify(const uint8_t token[PAIRED_TOKEN_LEN]);                // сравнение за постоянное время
bool paired_remove(int i);
void paired_clear(void);
