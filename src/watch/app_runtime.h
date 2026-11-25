// app_runtime.h - запуск приложения: разбор app.json, навигация между экранами.
#pragma once
#include "lvgl_inc.h"
#include "cJSON.h"
#include "app_manager.h"

#define APP_JSON_MAX 16384   // лимит размера app.json
#define MAX_HIST     8       // глубина стека экранов
#define MAX_BIND     48      // подписанных на переменные меток на экран
#define BIND_TMPL    160

enum { BIND_LABEL = 0, BIND_BAR = 1, BIND_MATH = 2 };
// kind == BIND_LABEL: tmpl - шаблон текста с @переменными; BIND_BAR: tmpl - имя переменной со значением индикатора;
// BIND_MATH: как BIND_LABEL, но блок пересобирается (дроби стопкой).
typedef struct { lv_obj_t *obj; char tmpl[BIND_TMPL]; uint8_t kind; } bind_t;

typedef struct app_run {
    const app_info_t *app;
    cJSON            *root;      // разобранный app.json
    cJSON            *screens;   // root.screens
    char              hist[MAX_HIST][24];
    int               depth;
    bind_t            binds[MAX_BIND];
    int               nbind;
} app_run_t;

void       app_runtime_launch(const app_info_t *a, lv_obj_t *launcher);
app_run_t *app_runtime_current(void);   // NULL, если ничего не запущено
void       app_nav_goto(app_run_t *r, const char *screen);
void       app_nav_back(app_run_t *r);  // назад по стеку, с первого экрана - в лаунчер
void       app_refresh_bindings(app_run_t *r);
