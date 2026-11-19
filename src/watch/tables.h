// tables.h - небольшие таблицы для приложений (ответы ИИ, справочники). Живут, пока запущено приложение.
// Заполняются системными вызовами (ai.ask_table), показываются виджетом "table" ("src": "<имя>").
#pragma once
#include <stdbool.h>
#include <stddef.h>

#define TBL_MAX_TABLES 4
#define TBL_NAME       24
#define TBL_COLS       4
#define TBL_ROWS       16
#define TBL_CELL       48   // байт на ячейку (русская буква = 2 байта)

typedef struct {
    char name[TBL_NAME];
    int  cols, rows;
    char cell[TBL_ROWS][TBL_COLS][TBL_CELL];
} wtable_t;

void            tables_clear(void);
const wtable_t *tables_get(const char *name);   // NULL, если нет
// Разбирает JSON {"rows": [["a","b"],["c","d"]]} (или просто массив строк). Лишнее обрезает по лимитам.
bool            tables_set_json(const char *name, const char *json);
// Заполняет готовую структуру (без регистрации под именем) - для таблиц, описанных прямо в app.json.
bool            tables_fill_json(wtable_t *t, const char *json);
// Для заглушек и тестов: таблица из готовых строк (rows x cols).
bool            tables_set_cells(const char *name, int rows, int cols, const char *const *cells);
