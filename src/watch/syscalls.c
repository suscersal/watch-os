#include "syscalls.h"
#include "sys_ui.h"
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
    if (!vars_valid_name(var)) { sys_toast("set: bad var name"); return; }
    char val[VAR_VAL];
    arg_text(a, "value", val, sizeof val);
    vars_set(var, val);
}

static void sc_add(app_run_t *r, const cJSON *a)
{
    LV_UNUSED(r);
    const char *var = jstr(a, "var");
    if (!vars_valid_name(var)) { sys_toast("add: bad var name"); return; }
    char val[VAR_VAL], res[VAR_VAL];
    arg_text(a, "value", val, sizeof val);
    double sum = strtod(vars_get(var), NULL) + (val[0] ? strtod(val, NULL) : 1.0);
    snprintf(res, sizeof res, "%.10g", sum);
    vars_set(var, res);
}

static void sc_goto(app_run_t *r, const cJSON *a)
{
    const char *s = jstr(a, "screen");
    if (!s) { sys_toast("goto: no screen"); return; }
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

// Заглушка. Позже здесь будет запрос к API через HAL-сеть.
static void sc_ai_ask(app_run_t *r, const cJSON *a)
{
    LV_UNUSED(r);
    char q[VAR_VAL - 16], res[VAR_VAL];
    arg_text(a, "q", q, sizeof q);
    snprintf(res, sizeof res, "(stub) %s", q);
    const char *out = jstr(a, "out");
    vars_set(vars_valid_name(out) ? out : "result", res);
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
    if (!cJSON_IsArray(yes)) { sys_toast("confirm: no on_yes"); return; }
    pend_confirm.r = r;
    pend_confirm.yes = yes;
    sys_confirm(jstr(a, "title") ? jstr(a, "title") : "Confirm", t, confirm_done, NULL);
}

static const sys_entry_t table[] = {
    {"confirm", 0,       true,  sc_confirm},
    {"set",    0,        false, sc_set},
    {"add",    0,        false, sc_add},
    {"toast",  0,        false, sc_toast},
    {"goto",   0,        true,  sc_goto},
    {"back",   0,        true,  sc_back},
    {"ai.ask", PERM_NET, false, sc_ai_ask},
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

        if (!e) { sys_toast("Unknown call"); break; }
        if (e->perm & ~r->app->perms) {
            char msg[64];
            snprintf(msg, sizeof msg, "No permission: %s", e->name);
            sys_toast(msg);
            break;
        }
        e->fn(r, a);
        if (e->ends) return;   // после перехода остальные действия не выполняются
    }
    app_refresh_bindings(r);
}
