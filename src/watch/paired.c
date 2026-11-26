#include "paired.h"
#include "cJSON.h"
#include "fs.h"
#include "plat.h"
#include "sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char name[PAIRED_NAME_MAX + 1]; uint8_t hash[32]; } dev_t_;

static dev_t_ g_dev[PAIRED_MAX];
static int    g_n;
static char   g_path[128];

static void to_hex(const uint8_t *b, size_t n, char *out) { for (size_t i = 0; i < n; i++) sprintf(out + 2 * i, "%02x", b[i]); }

static bool from_hex(const char *s, uint8_t *out, size_t n)
{
    if (!s || strlen(s) != 2 * n) return false;
    for (size_t i = 0; i < n; i++) {
        unsigned v;
        if (sscanf(s + 2 * i, "%2x", &v) != 1) return false;
        out[i] = (uint8_t)v;
    }
    return true;
}

static void save(void)
{
    if (!g_path[0]) return;
    cJSON *o = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(o, "devs");
    for (int i = 0; i < g_n; i++) {
        char hh[65];
        to_hex(g_dev[i].hash, 32, hh);
        cJSON *d = cJSON_CreateObject();
        cJSON_AddStringToObject(d, "name", g_dev[i].name);
        cJSON_AddStringToObject(d, "hash", hh);
        cJSON_AddItemToArray(arr, d);
    }
    char *txt = cJSON_PrintUnformatted(o);
    if (txt) fs_write_file(g_path, txt, strlen(txt));
    free(txt);
    cJSON_Delete(o);
}

void paired_init(const char *dir)
{
    g_n = 0;
    memset(g_dev, 0, sizeof g_dev);
    fs_mkdir(dir);
    snprintf(g_path, sizeof g_path, "%s/paired.json", dir);
    char *txt = fs_read_file(g_path, 4096);
    if (!txt) return;
    cJSON *o = cJSON_ParseWithLength(txt, strlen(txt));
    free(txt);
    const cJSON *arr = o ? cJSON_GetObjectItemCaseSensitive(o, "devs") : NULL;
    const cJSON *d;
    cJSON_ArrayForEach(d, arr) {
        if (g_n >= PAIRED_MAX) break;
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(d, "name");
        const cJSON *hash = cJSON_GetObjectItemCaseSensitive(d, "hash");
        if (!cJSON_IsString(name) || !cJSON_IsString(hash) || strlen(name->valuestring) > PAIRED_NAME_MAX) continue;
        if (!from_hex(hash->valuestring, g_dev[g_n].hash, 32)) continue;
        snprintf(g_dev[g_n].name, sizeof g_dev[g_n].name, "%s", name->valuestring);
        g_n++;
    }
    cJSON_Delete(o);
}

int paired_count(void) { return g_n; }

bool paired_info(int i, char *name, size_t name_n)
{
    if (i < 0 || i >= g_n) return false;
    if (name && name_n) snprintf(name, name_n, "%s", g_dev[i].name);
    return true;
}

static void hash_token(const uint8_t t[PAIRED_TOKEN_LEN], uint8_t out[32])
{
    sha256_ctx c;
    sha256_init(&c); sha256_update(&c, t, PAIRED_TOKEN_LEN); sha256_final(&c, out);
}

static void drop_at(int i)
{
    for (int k = i; k + 1 < g_n; k++) g_dev[k] = g_dev[k + 1];
    g_n--;
    memset(&g_dev[g_n], 0, sizeof g_dev[g_n]);
}

bool paired_add(const char *name, uint8_t token_out[PAIRED_TOKEN_LEN])
{
    if (!plat_random(token_out, PAIRED_TOKEN_LEN)) return false;
    char nm[PAIRED_NAME_MAX + 1];
    snprintf(nm, sizeof nm, "%s", (name && name[0]) ? name : "phone");
    for (int i = 0; i < g_n; i++) if (strcmp(g_dev[i].name, nm) == 0) { drop_at(i); break; }   // тот же телефон: прежний токен недействителен
    if (g_n >= PAIRED_MAX) drop_at(0);
    snprintf(g_dev[g_n].name, sizeof g_dev[g_n].name, "%s", nm);
    hash_token(token_out, g_dev[g_n].hash);
    g_n++;
    save();
    return true;
}

bool paired_verify(const uint8_t token[PAIRED_TOKEN_LEN])
{
    uint8_t h[32];
    hash_token(token, h);
    bool ok = false;
    for (int i = 0; i < g_n; i++) {          // перебираем все записи без раннего выхода
        uint8_t diff = 0;
        for (int k = 0; k < 32; k++) diff |= (uint8_t)(h[k] ^ g_dev[i].hash[k]);
        if (diff == 0) ok = true;
    }
    memset(h, 0, sizeof h);
    return ok;
}

bool paired_remove(int i)
{
    if (i < 0 || i >= g_n) return false;
    drop_at(i);
    save();
    return true;
}

void paired_clear(void)
{
    g_n = 0;
    memset(g_dev, 0, sizeof g_dev);
    save();
}
