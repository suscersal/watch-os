#include "app_manager.h"
#include "fs.h"
#include "cJSON.h"
#include "installer.h"
#include "display_app.h"
#include "buttons_test_app.h"
#include "gyro_app.h"
#include "battery_app.h"
#include "music_app.h"
#include "recorder_app.h"
#include "timer_app.h"
#include "calc_app.h"
#include "security_app.h"
#include "gallery_app.h"
#include "files_app.h"
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
    {"back", LV_SYMBOL_LEFT},      {"trash", LV_SYMBOL_TRASH},   {"download", LV_SYMBOL_DOWNLOAD}, {"eye", LV_SYMBOL_EYE_CLOSE},
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

// ---------- системные приложения и папки ----------
// Главный экран: Ask, Messages, Gallery, Музыка, папки «Инструменты» и «Система», Батарея. Остальное лежит в папках.
// Порядок регистрации = порядок на экране (внутри папки тоже). Приложения, которые уже не нужны (тесты кнопок и гироскопа),
// в сетку не попадают; вернуть: собрать с -DWATCH_DIAG_APPS, они появятся в папке «Система».
#define GRP_TOOLS  "grp_tools"
#define GRP_SYSTEM "grp_system"

static void reg_app(const char *id, const char *name, const char *icon, uint32_t color, const char *group,
                    void (*open)(lv_obj_t *launcher))
{
    if (napps >= APPS_MAX) return;
    app_info_t *a = &apps[napps++];
    memset(a, 0, sizeof *a);
    snprintf(a->id, sizeof a->id, "%s", id);
    snprintf(a->name, sizeof a->name, "%s", name);
    snprintf(a->icon, sizeof a->icon, "%s", icon);
    a->color = color;
    if (group) snprintf(a->group, sizeof a->group, "%s", group);
    a->builtin = open;
}

static void reg_folder(const char *id, const char *name, uint32_t color)
{
    reg_app(id, name, "folder", color, NULL, NULL);
    if (napps && strcmp(apps[napps - 1].id, id) == 0) apps[napps - 1].is_folder = true;
}

static void register_system_apps(void)
{
    reg_app("sys_messages", "Messages",    "mail",    0x22C55E, NULL, messages_open);
    reg_app("sys_gallery",  "Gallery",     "image",   0xF97316, NULL, gallery_open);
    reg_app("sys_music",    "Музыка",      "audio",   0xFF4D6D, NULL, music_open);

    reg_folder(GRP_TOOLS, "Инструменты", 0xFF9F0A);
    reg_app("sys_timer",    "Таймер",      "timer",   0xFF8800, GRP_TOOLS, timer_open);
    reg_app("sys_calc",     "Калькулятор", "calc",    0xFF9F0A, GRP_TOOLS, calc_open);
    reg_app("sys_recorder", "Диктофон",    "mic",     0xFF3B30, GRP_TOOLS, recorder_open);

    reg_app("sys_battery",  "Батарея",     "battery", 0x30D158, NULL, battery_open);

    reg_folder(GRP_SYSTEM, "Система", 0x6B7280);
    reg_app("sys_settings", "Settings",    "settings", 0x3B82F6, GRP_SYSTEM, settings_open);
    reg_app("sys_display",  "Display",     "video",   0xD946EF, GRP_SYSTEM, display_open);
    reg_app("sys_sync",     "Sync",        "refresh", 0x14B8A6, GRP_SYSTEM, sync_app_open);
    reg_app("sys_files",    "Files",       "folder",  0xFACC15, GRP_SYSTEM, files_open);
    reg_app("sys_security", "Security",    "eye",     0x6B7280, GRP_SYSTEM, security_open);
    reg_app("sys_install",  "Install",     "download", 0x0EA5E9, GRP_SYSTEM, installer_open);
#ifdef WATCH_DIAG_APPS
    reg_app("sys_buttons",  "Кнопки",      "ok",      0x8B5CF6, GRP_SYSTEM, buttons_test_open);
    reg_app("sys_gyro",     "Гироскоп",    "refresh", 0x06B6D4, GRP_SYSTEM, gyro_test_open);
#endif
}

size_t app_manager_scan(const char *root)
{
    napps = 0;
    if (root != g_root) snprintf(g_root, sizeof g_root, "%s", root);   // root может быть уже g_root
    fs_list_dir(g_root, dir_cb, g_root);
    qsort(apps, napps, sizeof apps[0], cmp_apps);

    register_system_apps();
    return napps;
}

size_t app_manager_scan_more(const char *root)
{
    size_t before = napps;
    fs_list_dir(root, dir_cb, (void *)root);
    return napps - before;
}

const char *app_manager_root(void) { return g_root; }

size_t app_manager_count(void) { return napps; }

const app_info_t *app_manager_get(size_t i) { return i < napps ? &apps[i] : NULL; }

size_t app_manager_in_group(const char *group, const app_info_t **out, size_t max)
{
    size_t n = 0;
    if (!group) group = "";
    for (size_t i = 0; i < napps && n < max; i++)
        if (strcmp(apps[i].group, group) == 0) out[n++] = &apps[i];
    return n;
}
