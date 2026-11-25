// files_app.h - системное приложение "Files": просмотр папок apps/, data/, packages/, сведения о файле,
// предпросмотр текста, удаление файлов и пустых папок.
#pragma once
#include "lvgl_inc.h"

void files_open(lv_obj_t *launcher);

// Для тестов:
const char *files_cwd(void);                 // текущая папка ("" - корень со списком apps/data/packages)
void        files_enter(const char *rel);    // перейти в папку (путь от корня, например "data/photos")
