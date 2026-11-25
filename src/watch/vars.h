// vars.h - переменные приложения (@name в текстах). Живут, пока запущено приложение.
#pragma once
#include <stdbool.h>
#include <stddef.h>

#define VARS_MAX 32
#define VAR_NAME 24
#define VAR_VAL  512            // ответ ИИ с формулами занимает 200-400 байт (кириллица - 2 байта на букву)
#define VAR_EXP  (VAR_VAL + 128)  // буфер подстановки @имён в текст метки

void        vars_clear(void);
bool        vars_valid_name(const char *name);      // [A-Za-z0-9_], 1..VAR_NAME-1
void        vars_set(const char *name, const char *val);
const char *vars_get(const char *name);             // "" если нет
// Подставляет @name значениями. "@@" даёт литерал '@'.
void        vars_expand(const char *in, char *out, size_t cap);
