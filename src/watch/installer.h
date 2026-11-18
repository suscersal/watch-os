// installer.h - системное приложение "Install": скачивает пакет приложения по ссылке,
// проверяет, показывает окно подтверждения с разрешениями и только потом записывает в apps/.
//
// Формат пакета (один JSON-файл, до 20 КБ):
//   { "id": "mycounter",
//     "manifest": { "name": "...", "icon": "plus", "color": "#10B981", "permissions": ["net"] },
//     "app":      { "start": "main", "screens": { ... } } }
#pragma once
#include "cJSON.h"
#include "lvgl_inc.h"
#include <stdbool.h>
#include <stddef.h>

#define PKG_MAX 20480

void installer_open(lv_obj_t *launcher);                                   // экран установщика

// Ядро (без UI, используется и в тестах):
bool installer_validate(const cJSON *pkg, char *err, size_t n);            // проверка структуры и лимитов
bool installer_fetch(const char *url, cJSON **pkg_out, char *err, size_t n); // скачать + разобрать + проверить
bool installer_commit(const cJSON *pkg, char *err, size_t n);              // записать в apps/<id>/
bool installer_uninstall(const char *id);                                  // удалить apps/<id>/
