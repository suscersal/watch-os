#include "settings.h"
#include "cJSON.h"
#include "fs.h"
#include "plat.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct {
    bool  tz_auto;
    int   tz_min;
    char  api_key[SETTINGS_API_KEY_MAX];
    char  ssid[SETTINGS_SSID_MAX], pass[SETTINGS_PASS_MAX];
    bool  notify_on;
    char  notify_code[7];
} S = { .tz_auto = true };

static char g_path[96] = "data/settings.json";

static void save(void)
{
    cJSON *o = cJSON_CreateObject();
    if (!o) return;
    cJSON_AddBoolToObject(o, "tz_auto", S.tz_auto);
    cJSON_AddNumberToObject(o, "tz_min", S.tz_min);
    cJSON_AddStringToObject(o, "api_key", S.api_key);
    cJSON_AddStringToObject(o, "wifi_ssid", S.ssid);
    cJSON_AddStringToObject(o, "wifi_pass", S.pass);
    cJSON_AddBoolToObject(o, "notify_on", S.notify_on);
    cJSON_AddStringToObject(o, "notify_code", S.notify_code);
    char *txt = cJSON_PrintUnformatted(o);
    if (txt) { fs_write_file(g_path, txt, strlen(txt)); free(txt); }
    cJSON_Delete(o);
}

static void copy_str(char *dst, size_t n, const cJSON *v)
{
    if (cJSON_IsString(v) && strlen(v->valuestring) < n) snprintf(dst, n, "%s", v->valuestring);
}

static bool code_ok(const char *c)
{
    if (strlen(c) != 6) return false;
    for (int i = 0; i < 6; i++) if (c[i] < '0' || c[i] > '9') return false;
    return true;
}

static char g_root[64] = "data";
const char *settings_data_root(void) { return g_root; }

void settings_init(const char *data_root)
{
    snprintf(g_root, sizeof g_root, "%s", data_root);
    snprintf(g_path, sizeof g_path, "%s/settings.json", data_root);
    memset(&S, 0, sizeof S);
    S.tz_auto = true;
    char *txt = fs_read_file(g_path, 2048);
    if (!txt) return;
    cJSON *o = cJSON_ParseWithLength(txt, strlen(txt));
    free(txt);
    if (!cJSON_IsObject(o)) { cJSON_Delete(o); return; }
    const cJSON *v;
    if (cJSON_IsBool(v = cJSON_GetObjectItemCaseSensitive(o, "tz_auto"))) S.tz_auto = cJSON_IsTrue(v);
    if (cJSON_IsNumber(v = cJSON_GetObjectItemCaseSensitive(o, "tz_min"))) {
        int m = (int)v->valuedouble;
        if (m >= -14 * 60 && m <= 14 * 60) S.tz_min = m;
    }
    copy_str(S.api_key, sizeof S.api_key, cJSON_GetObjectItemCaseSensitive(o, "api_key"));
    copy_str(S.ssid, sizeof S.ssid, cJSON_GetObjectItemCaseSensitive(o, "wifi_ssid"));
    copy_str(S.pass, sizeof S.pass, cJSON_GetObjectItemCaseSensitive(o, "wifi_pass"));
    if (cJSON_IsBool(v = cJSON_GetObjectItemCaseSensitive(o, "notify_on"))) S.notify_on = cJSON_IsTrue(v);
    copy_str(S.notify_code, sizeof S.notify_code, cJSON_GetObjectItemCaseSensitive(o, "notify_code"));
    if (!code_ok(S.notify_code)) { S.notify_code[0] = '\0'; S.notify_on = false; }
    cJSON_Delete(o);
}

bool settings_tz_auto(void) { return S.tz_auto; }
int  settings_tz_min(void)  { return S.tz_min; }

void settings_set_tz(bool is_auto, int tz_min)
{
    if (tz_min < -14 * 60) tz_min = -14 * 60;
    if (tz_min > 14 * 60)  tz_min = 14 * 60;
    if (S.tz_auto == is_auto && S.tz_min == tz_min) return;   // не трогаем флеш без нужды
    S.tz_auto = is_auto;
    S.tz_min = tz_min;
    save();
}

const char *settings_api_key(void) { return S.api_key; }

bool settings_set_api_key(const char *key)
{
    if (!key) key = "";
    if (strlen(key) >= sizeof S.api_key) return false;
    snprintf(S.api_key, sizeof S.api_key, "%s", key);
    save();
    return true;
}

bool settings_wifi_get(char *ssid, size_t ssid_n, char *pass, size_t pass_n)
{
    if (!S.ssid[0]) return false;
    if (ssid && ssid_n) snprintf(ssid, ssid_n, "%s", S.ssid);
    if (pass && pass_n) snprintf(pass, pass_n, "%s", S.pass);
    return true;
}

void settings_wifi_set(const char *ssid, const char *pass)
{
    if (!ssid || !ssid[0] || strlen(ssid) >= sizeof S.ssid) return;
    if (!pass) pass = "";
    if (strlen(pass) >= sizeof S.pass) return;
    if (strcmp(S.ssid, ssid) == 0 && strcmp(S.pass, pass) == 0) return;
    snprintf(S.ssid, sizeof S.ssid, "%s", ssid);
    snprintf(S.pass, sizeof S.pass, "%s", pass);
    save();
}

void settings_wifi_clear(void)
{
    S.ssid[0] = S.pass[0] = '\0';
    save();
}

static void make_code(void)
{
    uint8_t r[6];
    if (!plat_random(r, sizeof r)) { for (int i = 0; i < 6; i++) r[i] = (uint8_t)(i * 37 + 11); }   // запасной вариант, не должен срабатывать
    for (int i = 0; i < 6; i++) S.notify_code[i] = (char)('0' + r[i] % 10);
    S.notify_code[6] = '\0';
}

bool        settings_notify_on(void)   { return S.notify_on; }
const char *settings_notify_code(void) { return S.notify_code; }

void settings_set_notify(bool on)
{
    if (on && !S.notify_code[0]) make_code();
    S.notify_on = on;
    save();
}

void settings_notify_regen(void)
{
    make_code();
    save();
}
