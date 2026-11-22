#include "settings_app.h"
#include "clock.h"
#include "colorgrid_ui.h"
#include "pairing.h"
#include "plat.h"
#include "settings.h"
#include "sync_net.h"
#include "sync_proto.h"
#include "sys_ui.h"
#include "watch_fonts.h"
#include "wifi_ctl.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *g_launcher;

// ---------- общие мелочи ----------
static lv_obj_t *label(lv_obj_t *p, const char *t, uint32_t color, const lv_font_t *font)
{
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_text(l, t);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    if (font) lv_obj_set_style_text_font(l, font, 0);
    return l;
}

static lv_obj_t *button(lv_obj_t *p, const char *t, uint32_t color, lv_event_cb_t cb, void *ud)
{
    lv_obj_t *b = lv_button_create(p);
    lv_obj_set_size(b, lv_pct(100), 52);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, t);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    return b;
}

// Стек экранов: каждый подэкран открывается поверх родителя (родитель не удаляется), "назад" удаляет подэкран.
#define STACK_MAX 4
static lv_obj_t *g_main_scr;
static lv_obj_t *g_stack[STACK_MAX];
static int       g_depth;

static void push(lv_obj_t *parent, lv_obj_t *scr)
{
    if (g_depth < STACK_MAX) g_stack[g_depth++] = parent;
    lv_screen_load_anim(scr, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
}

static void pop_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    sys_keyboard_hide();
    if (g_depth == 0) return;
    lv_screen_load_anim(g_stack[--g_depth], LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, 200, 0, true);
}

static void main_back_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    g_main_scr = NULL;
    g_depth = 0;
    lv_screen_load_anim(g_launcher, sys_exit_anim(), 200, 0, true);
}

// ---------- Wi-Fi ----------
static lv_obj_t   *w_scr, *w_status, *w_list;
static lv_timer_t *w_timer;
static bool        w_listed;          // список сетей уже построен по последнему поиску
static char        w_pick[33];        // выбранная сеть для ввода пароля

static void w_deleted_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (w_timer) { lv_timer_delete(w_timer); w_timer = NULL; }
    w_scr = w_status = w_list = NULL;
}

static void w_net_cb(lv_event_t *e);

static void w_fill_list(void)
{
    lv_obj_clean(w_list);
    wifi_net_t nets[WIFI_NETS_MAX];
    int n = wifi_ctl_results(nets, WIFI_NETS_MAX);
    if (n == 0) { label(w_list, "No networks found", 0x9CA3AF, NULL); return; }
    for (int i = 0; i < n; i++) {
        lv_obj_t *b = lv_button_create(w_list);
        lv_obj_set_size(b, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_style_radius(b, 14, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x1F2937), 0);
        lv_obj_set_style_pad_all(b, 10, 0);
        lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(b, 2, 0);
        lv_obj_t *name = label(b, nets[i].ssid, 0xFFFFFF, NULL);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_height(name, 26);
        char sub[40];
        snprintf(sub, sizeof sub, "%s  %d dBm", nets[i].secure ? "Secured" : "Open", nets[i].rssi);
        label(b, sub, 0x9CA3AF, WATCH_FONT_SMALL);
        lv_obj_set_user_data(b, (void *)(intptr_t)(i + 1));
        lv_obj_add_event_cb(b, w_net_cb, LV_EVENT_CLICKED, NULL);
    }
}

static void w_tick(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (!w_scr) return;
    char b[96];
    switch (wifi_ctl_state()) {
    case WIFI_ST_CONNECTED:  snprintf(b, sizeof b, "Connected: %s", wifi_ctl_ssid()); lv_obj_set_style_text_color(w_status, lv_color_hex(0x22C55E), 0); break;
    case WIFI_ST_CONNECTING: snprintf(b, sizeof b, "Connecting to %s...", wifi_ctl_ssid()); lv_obj_set_style_text_color(w_status, lv_color_hex(0xF59E0B), 0); break;
    case WIFI_ST_FAILED:     snprintf(b, sizeof b, "Could not connect to %s. Check the password.", wifi_ctl_ssid()); lv_obj_set_style_text_color(w_status, lv_color_hex(0xEF4444), 0); break;
    default:                 snprintf(b, sizeof b, "Not connected"); lv_obj_set_style_text_color(w_status, lv_color_hex(0x9CA3AF), 0); break;
    }
    if (strcmp(lv_label_get_text(w_status), b) != 0) lv_label_set_text(w_status, b);

    if (!wifi_ctl_scanning() && !w_listed) { w_listed = true; w_fill_list(); }   // поиск закончился
}

static void w_scan_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    wifi_ctl_scan();
    w_listed = false;
    lv_obj_clean(w_list);
    label(w_list, "Scanning...", 0x9CA3AF, NULL);
}

static void w_forget_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    wifi_ctl_forget();
    sys_toast("Network forgotten");
}

// экран пароля
static lv_obj_t *p_ta;

static void p_connect_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    wifi_ctl_connect(w_pick, lv_textarea_get_text(p_ta));
    pop_cb(NULL);
}

static void p_eye_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_textarea_set_password_mode(p_ta, !lv_textarea_get_password_mode(p_ta));
}

static void p_ta_cb(lv_event_t *e)
{
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_FOCUSED || c == LV_EVENT_CLICKED) sys_keyboard_attach(lv_event_get_target_obj(e));
}

static void p_deleted_cb(lv_event_t *e) { LV_UNUSED(e); p_ta = NULL; }

static void open_password(void)
{
    lv_obj_t *body;
    char title[48];
    snprintf(title, sizeof title, "%s", w_pick);
    lv_obj_t *scr = sys_screen_create(title, pop_cb, &body);
    lv_obj_add_event_cb(scr, p_deleted_cb, LV_EVENT_DELETE, NULL);
    label(body, "Password", 0x9CA3AF, NULL);
    p_ta = lv_textarea_create(body);
    lv_obj_set_width(p_ta, lv_pct(100));
    lv_textarea_set_one_line(p_ta, true);
    lv_textarea_set_password_mode(p_ta, true);
    lv_textarea_set_max_length(p_ta, SETTINGS_PASS_MAX - 1);
    lv_obj_add_event_cb(p_ta, p_ta_cb, LV_EVENT_ALL, NULL);
    button(body, "Show / hide", 0x374151, p_eye_cb, NULL);
    button(body, "Connect", 0x3B82F6, p_connect_cb, NULL);
    push(w_scr, scr);
}

static void w_net_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_target_obj(e)) - 1;
    wifi_net_t nets[WIFI_NETS_MAX];
    int n = wifi_ctl_results(nets, WIFI_NETS_MAX);
    if (idx < 0 || idx >= n) return;
    snprintf(w_pick, sizeof w_pick, "%s", nets[idx].ssid);
    if (nets[idx].secure) { open_password(); return; }
    wifi_ctl_connect(w_pick, "");
}

// цветной код: пароль Wi-Fi вводится сеткой цветов, без клавиатуры (формат: colorcode.h, tools/colorcode.py)
static void cg_done(const char *ssid, const char *pass, void *ud)
{
    LV_UNUSED(ud);
    wifi_ctl_connect(ssid, pass);
    pop_cb(NULL);
}

static void open_colorcode_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_obj_t *body;
    lv_obj_t *scr = sys_screen_create("Color code", pop_cb, &body);
    colorgrid_create(body, cg_done, NULL);
    push(w_scr, scr);
}

static void open_wifi_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_obj_t *body;
    w_scr = sys_screen_create("Wi-Fi", pop_cb, &body);
    lv_obj_add_event_cb(w_scr, w_deleted_cb, LV_EVENT_DELETE, NULL);
    w_status = label(body, "", 0x9CA3AF, NULL);
    button(body, "Scan", 0x3B82F6, w_scan_cb, NULL);
    button(body, "Enter color code", 0x8B5CF6, open_colorcode_cb, NULL);
    w_list = lv_obj_create(body);
    lv_obj_remove_style_all(w_list);
    lv_obj_set_width(w_list, lv_pct(100));
    lv_obj_set_height(w_list, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(w_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(w_list, 8, 0);
    char s[SETTINGS_SSID_MAX];
    if (settings_wifi_get(s, sizeof s, NULL, 0)) button(body, "Forget saved network", 0x6B7280, w_forget_cb, NULL);
    w_timer = lv_timer_create(w_tick, 300, NULL);
    w_scan_cb(NULL);
    w_tick(NULL);
    push(g_main_scr, w_scr);
}

// ---------- часовой пояс ----------
static const int16_t tz_list[] = {   // минуты от UTC, по возрастанию
    -720, -660, -600, -540, -480, -420, -360, -300, -240, -210, -180, -120, -60, 0, 60, 120, 180, 210, 240, 270, 300,
    330, 345, 360, 390, 420, 480, 540, 570, 600, 660, 720, 780, 840,
};
#define TZ_N ((int)(sizeof tz_list / sizeof tz_list[0]))

static lv_obj_t   *t_scr, *t_now;
static lv_timer_t *t_timer;

static void t_deleted_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (t_timer) { lv_timer_delete(t_timer); t_timer = NULL; }
    t_scr = t_now = NULL;
}

static void t_tick(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (!t_now) return;
    struct tm tm;
    char b[48] = "Time not set";
    if (clock_local(&tm)) snprintf(b, sizeof b, "Now: %02d:%02d", tm.tm_hour, tm.tm_min);
    lv_label_set_text(t_now, b);
}

static void t_dd_cb(lv_event_t *e)
{
    uint32_t sel = lv_dropdown_get_selected(lv_event_get_target_obj(e));
    if (sel == 0) clock_set_tz(true, settings_tz_min());          // auto: пояс обновится при следующей синхронизации
    else if ((int)sel <= TZ_N) clock_set_tz(false, tz_list[sel - 1]);
    t_tick(NULL);
}

static void open_tz_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_obj_t *body;
    t_scr = sys_screen_create("Time zone", pop_cb, &body);
    lv_obj_add_event_cb(t_scr, t_deleted_cb, LV_EVENT_DELETE, NULL);
    t_now = label(body, "", 0xFFFFFF, WATCH_FONT_LARGE);

    static char opts[64 * 36];
    size_t off = (size_t)snprintf(opts, sizeof opts, "Auto (from phone)");
    for (int i = 0; i < TZ_N; i++) {
        int m = tz_list[i], a = m < 0 ? -m : m;
        off += (size_t)snprintf(opts + off, sizeof opts - off, "\nUTC%c%02d:%02d", m < 0 ? '-' : '+', a / 60, a % 60);
    }
    lv_obj_t *dd = lv_dropdown_create(body);
    lv_obj_set_width(dd, lv_pct(100));
    lv_dropdown_set_options(dd, opts);
    uint32_t sel = 0;
    if (!settings_tz_auto()) for (int i = 0; i < TZ_N; i++) if (tz_list[i] == settings_tz_min()) sel = (uint32_t)i + 1;
    lv_dropdown_set_selected(dd, sel);
    lv_obj_add_event_cb(dd, t_dd_cb, LV_EVENT_VALUE_CHANGED, NULL);
    label(body, "Auto uses the zone the phone app sends with the time.", 0x9CA3AF, WATCH_FONT_SMALL);
    t_timer = lv_timer_create(t_tick, 1000, NULL);
    t_tick(NULL);
    push(g_main_scr, t_scr);
}

// ---------- ключ API ----------
static lv_obj_t *k_ta, *k_status;

static void k_deleted_cb(lv_event_t *e) { LV_UNUSED(e); k_ta = k_status = NULL; }

static void k_show(void)
{
    const char *k = settings_api_key();
    char b[48];
    if (!k[0]) snprintf(b, sizeof b, "No key saved");
    else {
        size_t n = strlen(k);
        snprintf(b, sizeof b, "Saved: ..%s", n > 4 ? k + n - 4 : k);
    }
    lv_label_set_text(k_status, b);
}

static void k_save_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    const char *t = lv_textarea_get_text(k_ta);
    if (!t[0]) { sys_toast("Enter a key first"); return; }
    if (!settings_set_api_key(t)) { sys_toast("Key is too long"); return; }
    lv_textarea_set_text(k_ta, "");
    sys_keyboard_hide();
    k_show();
    sys_toast("Key saved");
}

static void k_clear_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    settings_set_api_key("");
    k_show();
}

static void k_eye_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_textarea_set_password_mode(k_ta, !lv_textarea_get_password_mode(k_ta));
}

static void open_key_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_obj_t *body;
    lv_obj_t *scr = sys_screen_create("API key", pop_cb, &body);
    lv_obj_add_event_cb(scr, k_deleted_cb, LV_EVENT_DELETE, NULL);
    k_status = label(body, "", 0x9CA3AF, NULL);
    k_ta = lv_textarea_create(body);
    lv_obj_set_width(k_ta, lv_pct(100));
    lv_textarea_set_one_line(k_ta, true);
    lv_textarea_set_password_mode(k_ta, true);
    lv_textarea_set_placeholder_text(k_ta, "Paste the key");
    lv_textarea_set_max_length(k_ta, SETTINGS_API_KEY_MAX - 1);
    lv_obj_add_event_cb(k_ta, p_ta_cb, LV_EVENT_ALL, NULL);
    button(body, "Show / hide", 0x374151, k_eye_cb, NULL);
    button(body, "Save", 0x3B82F6, k_save_cb, NULL);
    button(body, "Remove key", 0xEF4444, k_clear_cb, NULL);
    k_show();
    push(g_main_scr, scr);
}

// ---------- уведомления с телефона ----------
static lv_obj_t *n_body;

static void n_build(void);
static void n_build_async(void *ud) { LV_UNUSED(ud); n_build(); }

static void n_deleted_cb(lv_event_t *e) { LV_UNUSED(e); n_body = NULL; }

static void n_switch_cb(lv_event_t *e)
{
    bool on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    settings_set_notify(on);
    if (!sync_net_notify_apply() && on) { settings_set_notify(false); sync_net_notify_apply(); sys_toast("Cannot open the port"); }
    lv_async_call(n_build_async, NULL);          // перестроить после выхода из обработчика переключателя
}

static void n_regen_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    settings_notify_regen();
    sync_net_notify_apply();
    sys_toast("New code. Pair the phone again");
    lv_async_call(n_build_async, NULL);
}

static void n_build(void)
{
    if (!n_body) return;
    lv_obj_clean(n_body);
    lv_obj_t *row = lv_obj_create(n_body);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), 44);
    lv_obj_t *l = lv_label_create(row);
    lv_label_set_text(l, "Phone notifications");
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
    if (settings_notify_on()) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, n_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    if (!settings_notify_on()) {
        label(n_body, "Off. When on, the watch keeps Wi-Fi listening for notifications from the phone app (uses more battery).", 0x9CA3AF, WATCH_FONT_SMALL);
        return;
    }
    char ip[40], line[64], url[128];
    if (!plat_local_ip(ip, sizeof ip)) snprintf(ip, sizeof ip, "no network");
    snprintf(line, sizeof line, "%s : %u", ip, (unsigned)SYNC_PORT);
    label(n_body, "Address", 0x9CA3AF, WATCH_FONT_SMALL);
    label(n_body, line, 0xFFFFFF, WATCH_FONT_LARGE);
    const char *c = settings_notify_code();
    char code[16];
    snprintf(code, sizeof code, "%c%c%c %c%c%c", c[0], c[1], c[2], c[3], c[4], c[5]);
    label(n_body, "Pairing code", 0x9CA3AF, WATCH_FONT_SMALL);
    label(n_body, code, 0x38BDF8, WATCH_FONT_LARGE);
    if (strcmp(ip, "no network") != 0) {
        pairing_url(url, sizeof url, ip, SYNC_PORT, c, true);
        lv_obj_t *wrap = lv_obj_create(n_body);          // обёртка, чтобы QR стоял по центру
        lv_obj_remove_style_all(wrap);
        lv_obj_set_size(wrap, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(wrap, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        pairing_qr_create(wrap, url, 200);
        label(n_body, "Scan with the phone, or type the address and code in the app.", 0x9CA3AF, WATCH_FONT_SMALL);
    }
    button(n_body, "New code", 0x6B7280, n_regen_cb, NULL);
}

static void open_notify_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_obj_t *scr = sys_screen_create("Notifications", pop_cb, &n_body);
    lv_obj_add_event_cb(scr, n_deleted_cb, LV_EVENT_DELETE, NULL);
    n_build();
    push(g_main_scr, scr);
}

// ---------- главный экран ----------
void settings_open(lv_obj_t *launcher)
{
    g_launcher = launcher;
    g_depth = 0;
    lv_obj_t *body;
    g_main_scr = sys_screen_create_root("Settings", main_back_cb, &body);
    button(body, LV_SYMBOL_WIFI "  Wi-Fi", 0x3B82F6, open_wifi_cb, NULL);
    button(body, LV_SYMBOL_REFRESH "  Time zone", 0x14B8A6, open_tz_cb, NULL);
    button(body, LV_SYMBOL_EDIT "  API key", 0x8B5CF6, open_key_cb, NULL);
    button(body, LV_SYMBOL_BELL "  Notifications", 0x22C55E, open_notify_cb, NULL);
    lv_screen_load_anim(g_main_scr, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 200, 0, false);   // лаунчер не удаляем
}
