#include "storage.h"
#include "cJSON.h"
#include "fs.h"
#include "vars.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_root[64] = "data";

static bool valid_id(const char *id)
{
    if (!id || !id[0] || strlen(id) >= 32) return false;
    for (const char *p = id; *p; p++)
        if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-') return false;
    return true;
}

static bool path_for(const char *id, char *out, size_t n)
{
    if (!valid_id(id)) return false;
    return snprintf(out, n, "%s/apps/%s.json", g_root, id) < (int)n;
}

void storage_init(const char *data_root)
{
    snprintf(g_root, sizeof g_root, "%s", data_root);
    char p[96];
    fs_mkdir(g_root);
    snprintf(p, sizeof p, "%s/apps", g_root);
    fs_mkdir(p);
}

// Читает файл приложения; если его нет или он испорчен - пустой объект.
static cJSON *load(const char *id)
{
    char path[96];
    if (!path_for(id, path, sizeof path)) return NULL;
    char *txt = fs_read_file(path, STORE_FILE_MAX);
    cJSON *o = txt ? cJSON_ParseWithLength(txt, strlen(txt)) : NULL;
    free(txt);
    if (!cJSON_IsObject(o)) { cJSON_Delete(o); o = cJSON_CreateObject(); }
    return o;
}

static bool save(const char *id, cJSON *o)
{
    char path[96];
    if (!path_for(id, path, sizeof path)) return false;
    char *txt = cJSON_PrintUnformatted(o);
    if (!txt) return false;
    size_t n = strlen(txt);
    bool ok = n <= STORE_FILE_MAX && fs_write_file(path, txt, n);
    free(txt);
    return ok;
}

bool storage_get(const char *id, const char *key, char *out, size_t n)
{
    if (!vars_valid_name(key) || n == 0) return false;
    cJSON *o = load(id);
    if (!o) return false;
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    bool ok = cJSON_IsString(v);
    if (ok) snprintf(out, n, "%s", v->valuestring);
    cJSON_Delete(o);
    return ok;
}

bool storage_set(const char *id, const char *key, const char *val)
{
    if (!vars_valid_name(key) || !val || strlen(val) >= STORE_VAL) return false;
    cJSON *o = load(id);
    if (!o) return false;
    bool exists = cJSON_GetObjectItemCaseSensitive(o, key) != NULL;
    if (!exists && cJSON_GetArraySize(o) >= STORE_MAX_KEYS) { cJSON_Delete(o); return false; }
    if (exists) cJSON_ReplaceItemInObjectCaseSensitive(o, key, cJSON_CreateString(val));
    else        cJSON_AddStringToObject(o, key, val);
    bool ok = save(id, o);
    cJSON_Delete(o);
    return ok;
}

bool storage_remove(const char *id, const char *key)
{
    if (!vars_valid_name(key)) return false;
    cJSON *o = load(id);
    if (!o) return false;
    cJSON_DeleteItemFromObjectCaseSensitive(o, key);
    bool ok = save(id, o);
    cJSON_Delete(o);
    return ok;
}
