// wifi_ctl_posix.c - имитация Wi-Fi для симулятора на ПК: три «сети», подключение занимает 1.5 с.
// Пароль короче 8 символов считается неверным, чтобы можно было проверить и ошибку.
#include "wifi_ctl.h"
#include "lvgl_inc.h"
#include "settings.h"
#include <stdio.h>
#include <string.h>

static const wifi_net_t fake[] = {
    { "Home-WiFi", -48, true },
    { "Guest", -63, false },
    { "Cafe_5G", -77, true },
};

static uint32_t     g_scan_at;
static bool         g_scanning, g_have_results, g_inited;
static wifi_state_t g_state;
static uint32_t     g_conn_at;
static char         g_ssid[33], g_pass[65];

static void init_once(void)
{
    if (g_inited) return;
    g_inited = true;
    char s[33], p[65];
    if (settings_wifi_get(s, sizeof s, p, sizeof p)) { snprintf(g_ssid, sizeof g_ssid, "%s", s); g_state = WIFI_ST_CONNECTED; }
}

static void tick(void)
{
    init_once();
    if (g_scanning && lv_tick_elaps(g_scan_at) >= 1200) { g_scanning = false; g_have_results = true; }
    if (g_state == WIFI_ST_CONNECTING && lv_tick_elaps(g_conn_at) >= 1500) {
        bool secure = false;
        for (size_t i = 0; i < sizeof fake / sizeof fake[0]; i++) if (strcmp(fake[i].ssid, g_ssid) == 0) secure = fake[i].secure;
        if (!secure || strlen(g_pass) >= 8) { g_state = WIFI_ST_CONNECTED; settings_wifi_set(g_ssid, g_pass); }
        else { g_state = WIFI_ST_FAILED; }
    }
}

void wifi_ctl_scan(void) { init_once(); g_scanning = true; g_scan_at = lv_tick_get(); }
bool wifi_ctl_scanning(void) { tick(); return g_scanning; }

int wifi_ctl_results(wifi_net_t *out, int max)
{
    tick();
    if (!g_have_results) return 0;
    int n = 0;
    for (size_t i = 0; i < sizeof fake / sizeof fake[0] && n < max; i++) out[n++] = fake[i];
    return n;
}

void wifi_ctl_connect(const char *ssid, const char *pass)
{
    init_once();
    snprintf(g_ssid, sizeof g_ssid, "%s", ssid);
    snprintf(g_pass, sizeof g_pass, "%s", pass ? pass : "");
    g_state = WIFI_ST_CONNECTING;
    g_conn_at = lv_tick_get();
}

void wifi_ctl_forget(void)
{
    init_once();
    settings_wifi_clear();
    g_ssid[0] = '\0';
    g_state = WIFI_ST_IDLE;
}

wifi_state_t wifi_ctl_state(void) { tick(); return g_state; }
const char  *wifi_ctl_ssid(void)  { tick(); return g_ssid; }
