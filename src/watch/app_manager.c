#include "app_manager.h"
#include "fs.h"
#include "cJSON.h"
#include "installer.h"
#include "security_app.h"
#include "lvgl_inc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MANIFEST_MAX 2048

static app_info_t apps[APPS_MAX];
static size_t     napps;
static char       g_root[64] = "apps";

static const struct { const char *key; const char *sym; } icon_tbl[] = {
    {"calc", LV_SYMBOL_KEYBOARD}, {"list", LV_SYMBOL_LIST},   {"edit", LV_SYMBOL_EDIT},
    {"bell", LV_SYMBOL_BELL},     {"gps", LV_SYMBOL_GPS},     {"settings", LV_SYMBOL_SETTINGS},
    {"mail", LV_SYMBOL_ENVELOPE}, {"audio", LV_SYMBOL_AUDIO}, {"image", LV_SYMBOL_IMAGE},
    {"call", LV_SYMBOL_CALL},     {"plus", LV_SYMBOL_PLUS},   {"ok", LV_SYMBOL_OK},
    {"home", LV_SYMBOL_HOME},     {"file", LV_SYMBOL_FILE},   {"folder", LV_SYMBOL_DIRECTORY},
    {"video", LV_SYMBOL_VIDEO},   {"wifi", LV_SYMBOL_WIFI},   {"battery", LV_SYMBOL_BATTERY_FULL},
    {"charge", LV_SYMBOL_CHARGE}, {"refresh", LV_SYMBOL_REFRESH}, {"play", LV_SYMBOL_PLAY},
    {"trash", LV_SYMBOL_TRASH},   {"download", LV_SYMBOL_DOWNLOAD},
};

const char *app_icon_symbol(const char *key)
{
    for (size_t i = 0; i < sizeof(icon_tbl) / sizeof(icon_tbl[0]); i++)
        if (key && strcmp(icon_tbl[i].key, key) == 0) return icon_tbl[i].sym;
    return LV_SYMBOL_FILE;
}

static const char *jstr(const cJSON *o, const char *k)
{
    const cJSON *i = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsString(i) ? i->valuestring : NULL;
}

static uint32_t parse_perms(const cJSON *arr)
{
    uint32_t p = 0;
    const cJSON *it;
    cJSON_ArrayForEach(it, arr) {
        if (!cJSON_IsString(it)) continue;
        if (strcmp(it->valuestring, "net") == 0)          p |= PERM_NET;
        else if (strcmp(it->valuestring, "storage") == 0) p |= PERM_STORAGE;
        // неизвестные разрешения игнорируются
    }
    return p;
}

static void dir_cb(const char *folder, void *ud)
{
    const char *root = ud;
    if (napps >= APPS_MAX || strlen(folder) >= sizeof(apps[0].id)) return;

    char path[160];
    snprintf(path, sizeof path, "%s/%s/manifest.json", root, folder);
    char *txt = fs_read_file(path, MANIFEST_MAX);
    if (!txt) return;
    cJSON *m = cJSON_ParseWithLength(txt, strlen(txt));
    free(txt);
    if (!m) return;

    app_info_t *a = &apps[napps];
    memset(a, 0, sizeof *a);
    snprintf(a->id, sizeof a->id, "%s", folder);
    snprintf(a->name, sizeof a->name, "%s", jstr(m, "name") ? jstr(m, "name") : folder);
    snprintf(a->icon, sizeof a->icon, "%s", jstr(m, "icon") ? jstr(m, "icon") : "file");
    snprintf(a->dir, sizeof a->dir, "%s/%s", root, folder);

    const char *entry = jstr(m, "entry");
    if (!entry || strchr(entry, '/') || strstr(entry, "..")) entry = "app.json";
    snprintf(a->entry, sizeof a->entry, "%s", entry);

    const char *col = jstr(m, "color");
    a->color = (col && col[0] == '#' && strlen(col) == 7) ? (uint32_t)strtoul(col + 1, NULL, 16) : 0x6B7280;
    a->perms = parse_perms(cJSON_GetObjectItemCaseSensitive(m, "permissions"));

    cJSON_Delete(m);
    napps++;
}

static int cmp_apps(const void *x, const void *y)
{
    return strcmp(((const app_info_t *)x)->id, ((const app_info_t *)y)->id);
}

size_t app_manager_scan(const char *root)
{
    napps = 0;
    if (root != g_root) snprintf(g_root, sizeof g_root, "%s", root);   // root может быть уже g_root
    fs_list_dir(g_root, dir_cb, g_root);
    qsort(apps, napps, sizeof apps[0], cmp_apps);

    // системные приложения всегда в конце
    if (napps < APPS_MAX) {
        app_info_t *a = &apps[napps++];
        memset(a, 0, sizeof *a);
        snprintf(a->id, sizeof a->id, "sys_install");
        snprintf(a->name, sizeof a->name, "Install");
        snprintf(a->icon, sizeof a->icon, "download");
        a->color = 0x0EA5E9;
        a->builtin = installer_open;
    }
    if (napps < APPS_MAX) {
        app_info_t *a = &apps[napps++];
        memset(a, 0, sizeof *a);
        snprintf(a->id, sizeof a->id, "sys_security");
        snprintf(a->name, sizeof a->name, "Security");
        snprintf(a->icon, sizeof a->icon, "settings");
        a->color = 0x6B7280;
        a->builtin = security_open;
    }
    return napps;
}

const char *app_manager_root(void) { return g_root; }

size_t app_manager_count(void) { return napps; }

const app_info_t *app_manager_get(size_t i) { return i < napps ? &apps[i] : NULL; }
