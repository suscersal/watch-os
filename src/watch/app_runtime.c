#include "app_runtime.h"
#include "fs.h"
#include "sys_ui.h"
#include "tables.h"
#include "ui_renderer.h"
#include "vars.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static app_run_t *g_run;
static lv_obj_t  *g_launcher;

app_run_t *app_runtime_current(void) { return g_run; }

// Освобождаем разобранный JSON с задержкой: старый экран ещё доигрывает анимацию
// и его кнопки хранят указатели внутрь этого JSON.
static void free_run_cb(lv_timer_t *t)
{
    app_run_t *r = lv_timer_get_user_data(t);
    cJSON_Delete(r->root);
    free(r);
}

static void load_initial_vars(const cJSON *root)
{
    const cJSON *vs = cJSON_GetObjectItemCaseSensitive(root, "vars");
    const cJSON *it;
    cJSON_ArrayForEach(it, vs) {
        char buf[VAR_VAL];
        if (cJSON_IsString(it))      snprintf(buf, sizeof buf, "%s", it->valuestring);
        else if (cJSON_IsNumber(it)) snprintf(buf, sizeof buf, "%.10g", it->valuedouble);
        else continue;
        vars_set(it->string, buf);
    }
}

void app_runtime_launch(const app_info_t *a, lv_obj_t *launcher)
{
    if (g_run) return;
    g_launcher = launcher;

    char path[160];
    snprintf(path, sizeof path, "%s/%s", a->dir, a->entry);
    char *txt = fs_read_file(path, APP_JSON_MAX);
    if (!txt) { sys_toast("Cannot read app (missing or >16 KB)"); return; }
    cJSON *root = cJSON_ParseWithLength(txt, strlen(txt));
    free(txt);
    if (!root) { sys_toast("app.json: syntax error"); return; }

    cJSON *screens = cJSON_GetObjectItemCaseSensitive(root, "screens");
    const cJSON *start = cJSON_GetObjectItemCaseSensitive(root, "start");
    if (!cJSON_IsObject(screens) || !cJSON_IsString(start) || strlen(start->valuestring) >= sizeof(g_run->hist[0]) ||
        !cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(screens, start->valuestring))) {
        cJSON_Delete(root);
        sys_toast("app.json: bad start/screens");
        return;
    }

    app_run_t *r = calloc(1, sizeof *r);
    if (!r) { cJSON_Delete(root); return; }
    r->app = a;
    r->root = root;
    r->screens = screens;
    snprintf(r->hist[0], sizeof r->hist[0], "%s", start->valuestring);
    r->depth = 1;

    vars_clear();
    tables_clear();
    load_initial_vars(root);

    lv_obj_t *scr = ui_build_screen(r, r->hist[0]);
    g_run = r;
    lv_screen_load_anim(scr, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 200, 0, false);   // лаунчер не удаляем
}

void app_nav_goto(app_run_t *r, const char *name)
{
    if (strlen(name) >= sizeof r->hist[0] ||
        !cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(r->screens, name))) {
        sys_toast("Unknown screen");
        return;
    }
    if (r->depth >= MAX_HIST) { sys_toast("Too many screens"); return; }
    snprintf(r->hist[r->depth], sizeof r->hist[0], "%s", name);
    r->depth++;
    sys_keyboard_hide();
    lv_obj_t *scr = ui_build_screen(r, name);
    lv_screen_load_anim(scr, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 200, 0, true);
}

void app_nav_back(app_run_t *r)
{
    sys_keyboard_hide();
    if (r->depth > 1) {
        r->depth--;
        lv_obj_t *scr = ui_build_screen(r, r->hist[r->depth - 1]);
        lv_screen_load_anim(scr, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, 200, 0, true);
        return;
    }
    // первый экран: выходим в лаунчер, экран приложения удаляется
    sys_confirm_close();
    g_run = NULL;
    lv_screen_load_anim(g_launcher, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, 200, 0, true);
    lv_timer_t *t = lv_timer_create(free_run_cb, 500, r);
    lv_timer_set_repeat_count(t, 1);
}

void app_refresh_bindings(app_run_t *r)
{
    for (int i = 0; i < r->nbind; i++) {
        if (r->binds[i].kind == BIND_BAR) {
            lv_bar_set_value(r->binds[i].obj, (int32_t)strtod(vars_get(r->binds[i].tmpl), NULL), LV_ANIM_OFF);
            continue;
        }
        char buf[BIND_TMPL];
        vars_expand(r->binds[i].tmpl, buf, sizeof buf);
        lv_label_set_text(r->binds[i].obj, buf);
    }
}
