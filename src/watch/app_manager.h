// app_manager.h - сканирует папку apps/, читает manifest.json каждого приложения.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "lvgl_inc.h"

#define APPS_MAX      32
#define PERM_NET      (1u << 0)
#define PERM_STORAGE  (1u << 1)

typedef struct {
    char     id[32];      // имя папки
    char     name[48];
    char     icon[16];    // ключ из таблицы иконок, см. app_icon_symbol()
    uint32_t color;       // 0xRRGGBB
    uint32_t perms;       // PERM_*
    char     group[16];   // id папки, в которой лежит приложение; пусто - на главном экране
    bool     is_folder;   // папка: на главном экране плитка, по нажатию показывает приложения с group == id
    char     dir[96];     // apps/<id>
    char     entry[32];   // файл с описанием экранов, по умолчанию app.json
    void   (*builtin)(lv_obj_t *launcher);   // не NULL - системное приложение (не из JSON)
} app_info_t;

// Сканирует root, добавляет системные приложения (Install). Запоминает root.
size_t            app_manager_scan(const char *root);
// Добавляет JSON-приложения из ещё одной папки (без сброса списка и без системных). Нужно тестам: образцы лежат в tests/apps.
size_t            app_manager_scan_more(const char *root);
const char       *app_manager_root(void);
size_t            app_manager_count(void);
const app_info_t *app_manager_get(size_t i);
// Что показывать на экране: group == "" - главный экран (приложения без папки и сами папки), иначе содержимое папки.
// Порядок - как при регистрации. Возвращает, сколько указателей записано в out (не больше max).
size_t            app_manager_in_group(const char *group, const app_info_t **out, size_t max);
const char       *app_icon_symbol(const char *key);   // символ LVGL по ключу
