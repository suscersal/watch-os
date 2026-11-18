#include "auth.h"
#include "cJSON.h"
#include "fs.h"
#include "plat.h"
#include "sha256.h"
#include "lvgl/lvgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AUTH_ROUNDS 10000

static char     g_path[128];
static bool     g_has;
static uint8_t  g_salt[16], g_hash[32];
static int      g_len, g_fails, g_timeout = 30;
static uint32_t g_lock_until;     // lv_tick_get()-время окончания блокировки
static bool     g_locked;

static void to_hex(const uint8_t *b, size_t n, char *out)
{
    for (size_t i = 0; i < n; i++) sprintf(out + 2 * i, "%02x", b[i]);
}

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

static void derive(const uint8_t salt[16], const char *pin, uint8_t out[32])
{
    sha256_ctx c;
    uint8_t h[32];
    sha256_init(&c); sha256_update(&c, salt, 16); sha256_update(&c, pin, strlen(pin)); sha256_final(&c, h);
    for (int i = 0; i < AUTH_ROUNDS; i++) {
        sha256_init(&c); sha256_update(&c, h, 32); sha256_update(&c, salt, 16); sha256_update(&c, pin, strlen(pin));
        sha256_final(&c, h);
    }
    memcpy(out, h, 32);
}

static bool valid_pin(const char *pin)
{
    size_t n = pin ? strlen(pin) : 0;
    if (n < AUTH_PIN_MIN || n > AUTH_PIN_MAX) return false;
    for (size_t i = 0; i < n; i++) if (pin[i] < '0' || pin[i] > '9') return false;
    return true;
}

static uint32_t delay_s(int fails)
{
    if (fails < AUTH_FREE_ATTEMPTS) return 0;
    uint32_t d = 30;
    for (int i = AUTH_FREE_ATTEMPTS; i < fails && d < 900; i++) d *= 2;
    return d > 900 ? 900 : d;
}

static void start_lock(void)
{
    uint32_t d = delay_s(g_fails);
    g_locked = d > 0;
    g_lock_until = lv_tick_get() + d * 1000u;
}

static void save(void)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "v", 1);
    if (g_has) {
        char sh[33], hh[65];
        to_hex(g_salt, 16, sh); to_hex(g_hash, 32, hh);
        cJSON_AddStringToObject(o, "salt", sh);
        cJSON_AddStringToObject(o, "hash", hh);
        cJSON_AddNumberToObject(o, "len", g_len);
    }
    cJSON_AddNumberToObject(o, "fails", g_fails);
    cJSON_AddNumberToObject(o, "timeout", g_timeout);
    char *txt = cJSON_PrintUnformatted(o);
    if (txt) fs_write_file(g_path, txt, strlen(txt));
    free(txt);
    cJSON_Delete(o);
}

void auth_init(const char *dir)
{
    g_has = false; g_len = 0; g_fails = 0; g_timeout = 30; g_locked = false;
    fs_mkdir(dir);
    snprintf(g_path, sizeof g_path, "%s/auth.json", dir);
    char *txt = fs_read_file(g_path, 1024);
    if (!txt) return;
    cJSON *o = cJSON_ParseWithLength(txt, strlen(txt));
    free(txt);
    if (!o) return;
    const cJSON *salt = cJSON_GetObjectItemCaseSensitive(o, "salt");
    const cJSON *hash = cJSON_GetObjectItemCaseSensitive(o, "hash");
    const cJSON *len = cJSON_GetObjectItemCaseSensitive(o, "len");
    const cJSON *fails = cJSON_GetObjectItemCaseSensitive(o, "fails");
    const cJSON *to = cJSON_GetObjectItemCaseSensitive(o, "timeout");
    if (cJSON_IsString(salt) && cJSON_IsString(hash) && cJSON_IsNumber(len) &&
        from_hex(salt->valuestring, g_salt, 16) && from_hex(hash->valuestring, g_hash, 32) &&
        len->valueint >= AUTH_PIN_MIN && len->valueint <= AUTH_PIN_MAX) {
        g_has = true;
        g_len = len->valueint;
    }
    if (cJSON_IsNumber(fails) && fails->valueint > 0) g_fails = fails->valueint;
    if (cJSON_IsNumber(to) && to->valueint >= 0 && to->valueint <= 3600) g_timeout = to->valueint;
    cJSON_Delete(o);
    if (g_has) start_lock();   // перезапуск не сбрасывает задержку после серии неудач
}

bool auth_has_pin(void) { return g_has; }
int  auth_pin_len(void) { return g_has ? g_len : 0; }

bool auth_set_pin(const char *pin)
{
    if (!valid_pin(pin)) return false;
    uint8_t salt[16];
    if (!plat_random(salt, sizeof salt)) return false;
    memcpy(g_salt, salt, sizeof salt);
    derive(g_salt, pin, g_hash);
    g_has = true; g_len = (int)strlen(pin); g_fails = 0; g_locked = false;
    save();
    return true;
}

bool auth_remove_pin(void)
{
    g_has = false; g_len = 0; g_fails = 0; g_locked = false;
    memset(g_hash, 0, sizeof g_hash);
    save();
    return true;
}

uint32_t auth_lock_remaining_s(void)
{
    if (!g_locked) return 0;
    uint32_t left = g_lock_until - lv_tick_get();
    if (left > 0x80000000u || left == 0) { g_locked = false; return 0; }   // время вышло
    return (left + 999) / 1000;
}

int auth_attempts_left(void) { return g_fails >= AUTH_FREE_ATTEMPTS ? 0 : AUTH_FREE_ATTEMPTS - g_fails; }

auth_result_t auth_verify(const char *pin)
{
    if (!g_has) return AUTH_NO_PIN;
    if (auth_lock_remaining_s() > 0) return AUTH_LOCKED;
    uint8_t h[32];
    derive(g_salt, valid_pin(pin) ? pin : "x", h);
    uint8_t diff = 0;
    for (int i = 0; i < 32; i++) diff |= (uint8_t)(h[i] ^ g_hash[i]);   // сравнение за постоянное время
    memset(h, 0, sizeof h);
    if (diff == 0 && valid_pin(pin)) {
        g_fails = 0; g_locked = false;
        save();
        return AUTH_OK;
    }
    g_fails++;
    start_lock();
    save();
    return AUTH_WRONG;
}

int  auth_timeout_s(void) { return g_timeout; }

bool auth_set_timeout_s(int s)
{
    if (s < 0 || s > 3600) return false;
    g_timeout = s;
    save();
    return true;
}
