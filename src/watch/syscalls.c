#include "syscalls.h"
#include "ai.h"
#include "storage.h"
#include "sys_ui.h"
#include "tables.h"
#include "vars.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void (*sys_fn)(app_run_t *r, const cJSON *a);
typedef struct { const char *name; uint32_t perm; bool ends; sys_fn fn; } sys_entry_t;

static const char *jstr(const cJSON *o, const char *k)
{
    const cJSON *i = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsString(i) ? i->valuestring : NULL;
}

// Аргумент как текст: строка (с подстановкой @vars), число или bool.
static void arg_text(const cJSON *a, const char *key, char *out, size_t n)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(a, key);
    out[0] = '\0';
    if (cJSON_IsString(v))      vars_expand(v->valuestring, out, n);
    else if (cJSON_IsNumber(v)) snprintf(out, n, "%.10g", v->valuedouble);
    else if (cJSON_IsBool(v))   snprintf(out, n, "%s", cJSON_IsTrue(v) ? "1" : "0");
}

static void sc_set(app_run_t *r, const cJSON *a)
{
    LV_UNUSED(r);
    const char *var = jstr(a, "var");
    if (!vars_valid_name(var)) { sys_toast("set: неверное имя переменной"); return; }
    char val[VAR_VAL];
    arg_text(a, "value", val, sizeof val);
    vars_set(var, val);
}

static void sc_add(app_run_t *r, const cJSON *a)
{
    LV_UNUSED(r);
    const char *var = jstr(a, "var");
    if (!vars_valid_name(var)) { sys_toast("add: неверное имя переменной"); return; }
    char val[VAR_VAL], res[VAR_VAL];
    arg_text(a, "value", val, sizeof val);
    double sum = strtod(vars_get(var), NULL) + (val[0] ? strtod(val, NULL) : 1.0);
    snprintf(res, sizeof res, "%.10g", sum);
    vars_set(var, res);
}

static void sc_goto(app_run_t *r, const cJSON *a)
{
    const char *s = jstr(a, "screen");
    if (!s) { sys_toast("goto: нет экрана"); return; }
    app_nav_goto(r, s);
}

static void sc_back(app_run_t *r, const cJSON *a) { LV_UNUSED(a); app_nav_back(r); }

static void sc_toast(app_run_t *r, const cJSON *a)
{
    LV_UNUSED(r);
    char t[VAR_VAL];
    arg_text(a, "text", t, sizeof t);
    sys_toast(t);
}

// Вопрос ИИ через телефон (приложение Watch AI, docs/ai-bridge.md). Вызов блокирующий; ответ обрезается до VAR_VAL.
static void sc_ai_ask(app_run_t *r, const cJSON *a)
{
    LV_UNUSED(r);
    char q[VAR_VAL - 16], res[VAR_VAL], err[96];
    arg_text(a, "q", q, sizeof q);
    sys_toast("Спрашиваю ИИ...");
    lv_refr_now(NULL);                      // запрос блокирующий: сначала рисуем статус
    if (!ai_ask(q, res, sizeof res, err, sizeof err)) {
        snprintf(res, sizeof res, "Ошибка: %s", err);
        sys_toast(err);
    }
    const char *out = jstr(a, "out");
    vars_set(vars_valid_name(out) ? out : "result", res);
}

// Таблица от ИИ (phone: режим "table", docs/ai-bridge.md). Вызов блокирующий. Таблица кладётся под именем out;
// при ошибке в ней одна строка с текстом ошибки, чтобы экран с виджетом table не был пустым.
static void sc_ai_ask_table(app_run_t *r, const cJSON *a)
{
    LV_UNUSED(r);
    char q[VAR_VAL - 16], err[96];
    arg_text(a, "q", q, sizeof q);
    const char *out = jstr(a, "out");
    const char *name = vars_valid_name(out) ? out : "result";
    sys_toast("Спрашиваю ИИ...");
    lv_refr_now(NULL);
    if (!ai_ask_table(q, name, err, sizeof err)) {
        const char *cells[] = { "Ошибка", err };
        tables_set_cells(name, 1, 2, cells);
        sys_toast(err);
    }
}

// Постоянное хранилище приложения. Ключи и значения - текст; доступ только к своим данным.
static void sc_store_get(app_run_t *r, const cJSON *a)
{
    const char *key = jstr(a, "key");
    if (!vars_valid_name(key)) { sys_toast("store: неверный ключ"); return; }
    const char *out = jstr(a, "out");
    char val[VAR_VAL], def[VAR_VAL];
    arg_text(a, "default", def, sizeof def);
    if (!storage_get(r->app->id, key, val, sizeof val)) snprintf(val, sizeof val, "%s", def);
    vars_set(vars_valid_name(out) ? out : key, val);
}

static void sc_store_set(app_run_t *r, const cJSON *a)
{
    const char *key = jstr(a, "key");
    if (!vars_valid_name(key)) { sys_toast("store: неверный ключ"); return; }
    char val[VAR_VAL];
    arg_text(a, "value", val, sizeof val);
    if (!storage_set(r->app->id, key, val)) sys_toast("Не удалось сохранить");
}

static void sc_store_remove(app_run_t *r, const cJSON *a)
{
    const char *key = jstr(a, "key");
    if (!vars_valid_name(key)) { sys_toast("store: неверный ключ"); return; }
    storage_remove(r->app->id, key);
}

// Подтверждение: показывает системное окно; при OK выполняет on_yes.
static struct { app_run_t *r; const cJSON *yes; } pend_confirm;

static void confirm_done(bool ok, void *ud)
{
    LV_UNUSED(ud);
    if (ok && pend_confirm.r && pend_confirm.r == app_runtime_current())
        sys_run_actions(pend_confirm.r, pend_confirm.yes);
    pend_confirm.r = NULL;
}

static void sc_confirm(app_run_t *r, const cJSON *a)
{
    char t[VAR_VAL];
    arg_text(a, "text", t, sizeof t);
    const cJSON *yes = cJSON_GetObjectItemCaseSensitive(a, "on_yes");
    if (!cJSON_IsArray(yes)) { sys_toast("confirm: нет on_yes"); return; }
    pend_confirm.r = r;
    pend_confirm.yes = yes;
    sys_confirm(jstr(a, "title") ? jstr(a, "title") : "Подтвердите", t, confirm_done, NULL);
}

static const sys_entry_t table[] = {
    {"confirm", 0,       true,  sc_confirm},
    {"set",    0,        false, sc_set},
    {"add",    0,        false, sc_add},
    {"toast",  0,        false, sc_toast},
    {"goto",   0,        true,  sc_goto},
    {"back",   0,        true,  sc_back},
    {"ai.ask",       PERM_NET,     false, sc_ai_ask},
    {"ai.ask_table", PERM_NET,     false, sc_ai_ask_table},
    {"store.get",    PERM_STORAGE, false, sc_store_get},
    {"store.set",    PERM_STORAGE, false, sc_store_set},
    {"store.remove", PERM_STORAGE, false, sc_store_remove},
};

void sys_run_actions(app_run_t *r, const cJSON *actions)
{
    if (!cJSON_IsArray(actions)) return;
    int steps = 0;
    const cJSON *a;
    cJSON_ArrayForEach(a, actions) {
        if (++steps > MAX_ACTIONS) break;
        const char *name = jstr(a, "do");
        const sys_entry_t *e = NULL;
        for (size_t i = 0; name && i < sizeof(table) / sizeof(table[0]); i++)
            if (strcmp(table[i].name, name) == 0) { e = &table[i]; break; }

        if (!e) { sys_toast("Неизвестный вызов"); break; }
        if (e->perm & ~r->app->perms) {
            char msg[64];
            snprintf(msg, sizeof msg, "Нет разрешения: %s", e->name);
            sys_toast(msg);
            break;
        }
        e->fn(r, a);
        if (e->ends) return;   // после перехода остальные действия не выполняются
    }
    app_refresh_bindings(r);
}
