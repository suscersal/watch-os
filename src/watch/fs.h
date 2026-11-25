// fs.h - минимальный доступ к файлам. Единственная точка, где ядро трогает файловую систему.
// Для ПК реализация в fs_posix.c, для ESP32 позже будет LittleFS/microSD.
#pragma once
#include <stdbool.h>
#include <stddef.h>

typedef void (*fs_dir_cb)(const char *name, void *ud);

// Вызывает cb для каждой записи каталога (кроме скрытых). false - каталог не открылся.
bool fs_list_dir(const char *path, fs_dir_cb cb, void *ud);

// Читает файл целиком в malloc-буфер с нулём в конце. NULL, если файла нет или он больше max_size.
char *fs_read_file(const char *path, size_t max_size);

bool fs_mkdir(const char *path);                                  // true, если создан или уже есть
bool fs_write_file(const char *path, const char *data, size_t n); // атомарно: tmp + rename
bool fs_remove(const char *path);                                 // файл или пустой каталог

// То же, но возвращает длину (для бинарных файлов, например JPEG).
char *fs_read_file_n(const char *path, size_t max_size, size_t *len_out);

// Работа с большими файлами по частям (запись и воспроизведение звука), без чтения целиком в память.
bool fs_append_file(const char *path, const void *data, size_t n);                 // дописать в конец (создаёт файл)
bool fs_write_at(const char *path, size_t offset, const void *data, size_t n);     // перезаписать кусок существующего файла
long fs_file_size(const char *path);                                               // -1, если файла нет
long fs_read_range(const char *path, size_t offset, void *buf, size_t n);          // прочитано байт, -1 при ошибке

bool fs_is_dir(const char *path);   // true, если path - каталог (для файлового менеджера)
