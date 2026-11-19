#include "power.h"
#include "cJSON.h"
#include "fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "sdkconfig.h"      // CONFIG_WATCH_BRIGHTNESS из menuconfig
#endif
#ifndef CONFIG_WATCH_BRIGHTNESS
#define CONFIG_WATCH_BRIGHTNESS 70
#endif

typedef enum { ST_ON, ST_DIM, ST_OFF } state_t;

static int     g_dim_s = 30, g_off_s = 60, g_bright = CONFIG_WATCH_BRIGHTNESS;
static state_t g_state = ST_ON;
static lv_obj_t   *g_overlay;
static lv_timer_t *g_timer;
static void      (*g_backend)(int);
static char        g_path[96] = "data/power.json";

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

// off должен наступать позже затемнения
static void normalize(void)
{
    g_bright = clamp(g_bright, 5, 100);
    g_dim_s  = clamp(g_dim_s, 0, 3600);
    g_off_s  = clamp(g_off_s, 0, 3600);
    if (g_dim_s && g_off_s && g_off_s <= g_dim_s) g_off_s = g_dim_s + 15;
}

static void apply_brightness(void)
{
    if (!g_backend) return;
    int pct = g_state == ST_ON ? g_bright : g_state == ST_DIM ? clamp(g_bright / 4, 3, 100) : 0;
    g_backend(pct);
}

static void set_state(state_t s)
{
    if (s == g_state) return;
    g_state = s;
    if (g_overlay) {
        if (s == ST_OFF) lv_obj_remove_flag(g_overlay, LV_OBJ_FLAG_HIDDEN);
        else             lv_obj_add_flag(g_overlay, LV_OBJ_FLAG_HIDDEN);
    }
    apply_brightness();
}

static void tick_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    uint32_t idle = lv_display_get_inactive_time(NULL) / 1000;
    state_t want = ST_ON;
    if (g_off_s && idle >= (uint32_t)g_off_s)      want = ST_OFF;
    else if (g_dim_s && idle >= (uint32_t)g_dim_s) want = ST_DIM;
    set_state(want);
}

static void overlay_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    power_wake();    // нажатие пришло на накладку: будим экран; под ней ничего не нажимается
}

static void save(void)
{
    cJSON *o = cJSON_CreateObject();
    if (!o) return;
    cJSON_AddNumberToObject(o, "dim", g_dim_s);
    cJSON_AddNumberToObject(o, "off", g_off_s);
    cJSON_AddNumberToObject(o, "brightness", g_bright);
    char *txt = cJSON_PrintUnformatted(o);
    if (txt) { fs_write_file(g_path, txt, strlen(txt)); free(txt); }
    cJSON_Delete(o);
}

static void load(void)
{
    char *txt = fs_read_file(g_path, 512);
    if (!txt) return;
    cJSON *o = cJSON_ParseWithLength(txt, strlen(txt));
    free(txt);
    if (!cJSON_IsObject(o)) { cJSON_Delete(o); return; }
    const cJSON *v;
    if (cJSON_IsNumber(v = cJSON_GetObjectItemCaseSensitive(o, "dim")))        g_dim_s  = (int)v->valuedouble;
    if (cJSON_IsNumber(v = cJSON_GetObjectItemCaseSensitive(o, "off")))        g_off_s  = (int)v->valuedouble;
    if (cJSON_IsNumber(v = cJSON_GetObjectItemCaseSensitive(o, "brightness"))) g_bright = (int)v->valuedouble;
    cJSON_Delete(o);
}

void power_init(const char *data_root)
{
    snprintf(g_path, sizeof g_path, "%s/power.json", data_root);
    load();
    normalize();

    if (!g_overlay) {
        g_overlay = lv_obj_create(lv_layer_sys());    // выше всех слоёв, включая блокировку и клавиатуру
        lv_obj_set_size(g_overlay, lv_pct(100), lv_pct(100));
        lv_obj_set_pos(g_overlay, 0, 0);
        lv_obj_set_style_bg_color(g_overlay, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(g_overlay, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(g_overlay, 0, 0);
        lv_obj_set_style_radius(g_overlay, 0, 0);
        lv_obj_set_style_pad_all(g_overlay, 0, 0);
        lv_obj_remove_flag(g_overlay, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(g_overlay, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(g_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_event_cb(g_overlay, overlay_cb, LV_EVENT_PRESSED, NULL);
    }
    if (!g_timer) g_timer = lv_timer_create(tick_cb, 500, NULL);
    g_state = ST_ON;
    apply_brightness();
}

void power_set_backend(void (*set_brightness_pct)(int pct))
{
    g_backend = set_brightness_pct;
    apply_brightness();
}

void power_get_settings(int *dim_s, int *off_s, int *brightness_pct)
{
    if (dim_s) *dim_s = g_dim_s;
    if (off_s) *off_s = g_off_s;
    if (brightness_pct) *brightness_pct = g_bright;
}

void power_set_settings(int dim_s, int off_s, int brightness_pct)
{
    g_dim_s = dim_s; g_off_s = off_s; g_bright = brightness_pct;
    normalize();
    save();
    apply_brightness();
}

void power_preview_brightness(int brightness_pct)
{
    g_bright = clamp(brightness_pct, 5, 100);
    apply_brightness();
}

bool power_dimmed(void)     { return g_state == ST_DIM; }
bool power_screen_off(void) { return g_state == ST_OFF; }

void power_wake(void)
{
    lv_display_trigger_activity(NULL);
    set_state(ST_ON);
}
