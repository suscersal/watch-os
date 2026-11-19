#include "tables.h"
#include "cJSON.h"
#include "vars.h"
#include <stdio.h>
#include <string.h>

static wtable_t tbl[TBL_MAX_TABLES];
static int      ntbl;

void tables_clear(void) { ntbl = 0; }

const wtable_t *tables_get(const char *name)
{
    if (!name) return NULL;
    for (int i = 0; i < ntbl; i++) if (strcmp(tbl[i].name, name) == 0) return &tbl[i];
    return NULL;
}

static wtable_t *slot(const char *name)
{
    if (!vars_valid_name(name) || strlen(name) >= TBL_NAME) return NULL;
    for (int i = 0; i < ntbl; i++) if (strcmp(tbl[i].name, name) == 0) return &tbl[i];
    if (ntbl >= TBL_MAX_TABLES) return NULL;
    wtable_t *t = &tbl[ntbl++];
    memset(t, 0, sizeof *t);
    snprintf(t->name, sizeof t->name, "%s", name);
    return t;
}

// Обрезаем по границе символа UTF-8, чтобы не оставить половину русской буквы.
static void put_cell(char *dst, const char *src)
{
    size_t n = strlen(src);
    if (n >= TBL_CELL) {
        n = TBL_CELL - 1;
        while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) n--;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

bool tables_fill_json(wtable_t *t, const char *json)
{
    cJSON *root = cJSON_ParseWithLength(json, strlen(json));
    if (!root) return false;
    const cJSON *rows = cJSON_IsArray(root) ? root : cJSON_GetObjectItemCaseSensitive(root, "rows");
    bool ok = false;
    if (cJSON_IsArray(rows)) {
        memset(t->cell, 0, sizeof t->cell);
        t->rows = t->cols = 0;
        const cJSON *row;
        cJSON_ArrayForEach(row, rows) {
            if (!cJSON_IsArray(row) || t->rows >= TBL_ROWS) continue;
            int c = 0;
            const cJSON *cell;
            cJSON_ArrayForEach(cell, row) {
                if (c >= TBL_COLS) break;
                char num[32];
                if (cJSON_IsString(cell))      put_cell(t->cell[t->rows][c], cell->valuestring);
                else if (cJSON_IsNumber(cell)) { snprintf(num, sizeof num, "%.10g", cell->valuedouble); put_cell(t->cell[t->rows][c], num); }
                c++;
            }
            if (c > t->cols) t->cols = c;
            t->rows++;
        }
        ok = t->rows > 0 && t->cols > 0;
    }
    cJSON_Delete(root);
    return ok;
}

bool tables_set_json(const char *name, const char *json)
{
    wtable_t *t = slot(name);
    if (!t) return false;
    wtable_t tmp = { .cols = 0 };
    if (!tables_fill_json(&tmp, json)) return false;
    snprintf(tmp.name, sizeof tmp.name, "%s", t->name);
    *t = tmp;
    return true;
}

bool tables_set_cells(const char *name, int rows, int cols, const char *const *cells)
{
    wtable_t *t = slot(name);
    if (!t || rows < 1 || cols < 1) return false;
    memset(t->cell, 0, sizeof t->cell);
    t->rows = rows > TBL_ROWS ? TBL_ROWS : rows;
    t->cols = cols > TBL_COLS ? TBL_COLS : cols;
    for (int r = 0; r < t->rows; r++)
        for (int c = 0; c < t->cols; c++) put_cell(t->cell[r][c], cells[r * cols + c]);
    return true;
}
