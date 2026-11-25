#include "app_manager.h"
#include "fs.h"
#include "cJSON.h"
#include "installer.h"
#include "display_app.h"
#include "buttons_test_app.h"
#include "music_app.h"
#include "recorder_app.h"
#include "timer_app.h"
#include "calc_app.h"
#include "security_app.h"
#include "gallery_app.h"
#include "messages_app.h"
#include "settings_app.h"
#include "sync_app.h"
#include "lvgl_inc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MANIFEST_MAX 2048

static app_info_t apps[APPS_MAX];
static size_t     napps;
static char       g_root[64] = "apps";

static const struct { const char *key; const char *sym; } icon_tbl[] = {
    {"mic", "\xEF\x84\xB0"}, {"timer", "\xEF\x8B\xB2"},   // секундомер (FontAwesome f2f2)     // микрофон (FontAwesome f130, есть в шрифтах проекта)
    {"calc", LV_SYMBOL_KEYBOARD}, {"list", LV_SYMBOL_LIST},   {"edit", LV_SYMBOL_EDIT},
    {"bell", LV_SYMBOL_BELL},     {"gps", LV_SYMBOL_GPS},     {"settings", LV_SYMBOL_SETTINGS},
    {"mail", LV_SYMBOL_ENVELOPE}, {"audio", LV_SYMBOL_AUDIO}, {"image", LV_SYMBOL_IMAGE},
    {"call", LV_SYMBOL_CALL},     {"plus", LV_SYMBOL_PLUS},   {"ok", LV_SYMBOL_OK},
    {"home", LV_SYMBOL_HOME},     {"file", LV_SYMBOL_FILE},   {"folder", LV_SYMBOL_DIRECTORY},
    {"video", LV_SYMBOL_VIDEO},   {"wifi", LV_SYMBOL_WIFI},   {"battery", LV_SYMBOL_BATTERY_FULL},
    {"charge", LV_SYMBOL_CHARGE}, {"refresh", LV_SYMBOL_REFRESH}, {"play", LV_SYMBOL_PLAY},
    {"trash", LV_SYMBOL_TRASH},   {"download", LV_SYMBOL_DOWNLOAD}, {"eye", LV_SYMBOL_EYE_CLOSE},
};

const char *app_icon_symbol(const char *key)
{
    for (size_t i = 0; i < sizeof(icon_tbl) / sizeof(icon_tbl[0]); i++)
        if (key && strcmp(icon_tbl[i].key, key) == 0) return icon_tbl[i].sym;
    return LV_SYMBOL_FILE;
}

// Если имя обрезано посреди русской буквы, отбрасываем её остаток, иначе на экране появится мусор.
static void utf8_trim(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && ((unsigned char)s[n - 1] & 0xC0) == 0x80) n--;            // хвост из продолжений
    if (n > 0 && (unsigned char)s[n - 1] >= 0xC0) {                           // начало символа без продолжения
        unsigned char c = (unsigned char)s[n - 1];
        size_t need = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
        if (strlen(s) - (n - 1) < need) n--;
        else n = strlen(s);
    } else if (n > 0) {
        n = strlen(s);
    }
    s[n] = '\0';
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
    utf8_trim(a->name);
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
        snprintf(a->id, sizeof a->id, "sys_music");
        snprintf(a->name, sizeof a->name, "Музыка");
        snprintf(a->icon, sizeof a->icon, "audio");
        a->color = 0xFF4D6D;
        a->builtin = music_open;
    }
    if (napps < APPS_MAX) {
        app_info_t *a = &apps[napps++];
        memset(a, 0, sizeof *a);
        snprintf(a->id, sizeof a->id, "sys_timer");
        snprintf(a->name, sizeof a->name, "Таймер");
        snprintf(a->icon, sizeof a->icon, "timer");
        a->color = 0xFF8800;
        a->builtin = timer_open;
    }
    if (napps < APPS_MAX) {
        app_info_t *a = &apps[napps++];
        memset(a, 0, sizeof *a);
        snprintf(a->id, sizeof a->id, "sys_calc");
        snprintf(a->name, sizeof a->name, "Калькулятор");
        snprintf(a->icon, sizeof a->icon, "calc");
        a->color = 0xFF9F0A;
        a->builtin = calc_open;
    }
    if (napps < APPS_MAX) {
        app_info_t *a = &apps[napps++];
        memset(a, 0, sizeof *a);
        snprintf(a->id, sizeof a->id, "sys_recorder");
        snprintf(a->name, sizeof a->name, "Диктофон");
        snprintf(a->icon, sizeof a->icon, "mic");
        a->color = 0xFF3B30;
        a->builtin = recorder_open;
    }
    if (napps < APPS_MAX) {
        app_info_t *a = &apps[napps++];
        memset(a, 0, sizeof *a);
        snprintf(a->id, sizeof a->id, "sys_display");
        snprintf(a->name, sizeof a->name, "Display");
        snprintf(a->icon, sizeof a->icon, "video");
        a->color = 0xD946EF;
        a->builtin = display_open;
    }
    if (napps < APPS_MAX) {
        app_info_t *a = &apps[napps++];
        memset(a, 0, sizeof *a);
        snprintf(a->id, sizeof a->id, "sys_gallery");
        snprintf(a->name, sizeof a->name, "Gallery");
        snprintf(a->icon, sizeof a->icon, "image");
        a->color = 0xF97316;
        a->builtin = gallery_open;
    }
    if (napps < APPS_MAX) {
        app_info_t *a = &apps[napps++];
        memset(a, 0, sizeof *a);
        snprintf(a->id, sizeof a->id, "sys_sync");
        snprintf(a->name, sizeof a->name, "Sync");
        snprintf(a->icon, sizeof a->icon, "refresh");
        a->color = 0x14B8A6;
        a->builtin = sync_app_open;
    }
    if (napps < APPS_MAX) {
        app_info_t *a = &apps[napps++];
        memset(a, 0, sizeof *a);
        snprintf(a->id, sizeof a->id, "sys_messages");
        snprintf(a->name, sizeof a->name, "Messages");
        snprintf(a->icon, sizeof a->icon, "mail");
        a->color = 0x22C55E;
        a->builtin = messages_open;
    }
    if (napps < APPS_MAX) {
        app_info_t *a = &apps[napps++];
        memset(a, 0, sizeof *a);
        snprintf(a->id, sizeof a->id, "sys_settings");
        snprintf(a->name, sizeof a->name, "Settings");
        snprintf(a->icon, sizeof a->icon, "settings");
        a->color = 0x3B82F6;
        a->builtin = settings_open;
    }
    if (napps < APPS_MAX) {
        app_info_t *a = &apps[napps++];
        memset(a, 0, sizeof *a);
        snprintf(a->id, sizeof a->id, "sys_buttons");
        snprintf(a->name, sizeof a->name, "Кнопки");
        snprintf(a->icon, sizeof a->icon, "ok");
        a->color = 0x8B5CF6;
        a->builtin = buttons_test_open;
    }
    if (napps < APPS_MAX) {
        app_info_t *a = &apps[napps++];
        memset(a, 0, sizeof *a);
        snprintf(a->id, sizeof a->id, "sys_security");
        snprintf(a->name, sizeof a->name, "Security");
        snprintf(a->icon, sizeof a->icon, "eye");
        a->color = 0x6B7280;
        a->builtin = security_open;
    }
    return napps;
}

const char *app_manager_root(void) { return g_root; }

size_t app_manager_count(void) { return napps; }

const app_info_t *app_manager_get(size_t i) { return i < napps ? &apps[i] : NULL; }
