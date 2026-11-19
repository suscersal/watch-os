// storage.h - постоянное хранилище приложения (ключ -> текст). Разрешение "storage".
// Приложение видит только свои ключи; пути к файлам ему недоступны. Файл: <root>/apps/<id>.json.
#pragma once
#include <stdbool.h>
#include <stddef.h>

#define STORE_MAX_KEYS 32
#define STORE_VAL      256    // макс. длина значения, байт
#define STORE_FILE_MAX 8192   // макс. размер файла приложения

void storage_init(const char *data_root);   // создаёт <root>/apps
bool storage_get(const char *app_id, const char *key, char *out, size_t n);   // false, если ключа нет
bool storage_set(const char *app_id, const char *key, const char *val);       // false: плохое имя, лимит, ошибка записи
bool storage_remove(const char *app_id, const char *key);
