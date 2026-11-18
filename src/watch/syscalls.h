// syscalls.h - единственный путь приложения к системе. Таблица "имя -> функция + нужное разрешение".
#pragma once
#include "app_runtime.h"

#define MAX_ACTIONS 16   // максимум действий в одном обработчике

void sys_run_actions(app_run_t *r, const cJSON *actions);
