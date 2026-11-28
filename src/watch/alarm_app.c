// Будильник. Список карточек (время, дни, переключатель), редактор с барабанами часов/минут и днями недели,
// окно звонка на lv_layer_sys (выше экрана блокировки: будильник можно выключить, не вводя PIN).
#include "alarm_app.h"
#include "cJSON.h"
#include "clock.h"
#include "fs.h"
#include "haptic.h"
#include "messages.h"
#include "power.h"
#include "sys_ui.h"
#include "watch_fonts.h"
#include "ws_style.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W            410
#define ORANGE       0xFF8800
#define ALARM_MAX    8
#define SNOOZE_MIN   5
#define RING_TICKS   20                  // 20 * 3 с = минута звонка, потом "пропущен"
#define MSG_ID_ALARM 0x414C524Du         // "ALRM"

typedef struct { uint8_t h, m, days; bool on; int64_t fired; } alarm_t;   // days: бит0 = Пн ... бит6 = Вс; 0 = один раз

static alarm_t A[ALARM_MAX];
static int     NA;
static char    g_path[96] = "data/alarms.json";
static bool    g_loaded;
static lv_timer_t *g_chk;

static const char *DAY[7] = { "Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Вс" };

// ---------- файл ----------
static void save(void)
{
    cJSON *r = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(r, "alarms");
    for (int i = 0; i < NA; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "h", A[i].h);
        cJSON_AddNumberToObject(o, "m", A[i].m);
        cJSON_AddNumberToObject(o, "d", A[i].days);
        cJSON_AddBoolToObject(o, "on", A[i].on);
        cJSON_AddItemToArray(arr, o);
    }
    char *t = cJSON_PrintUnformatted(r);
    if (t) { fs_write_file(g_path, t, strlen(t)); free(t); }
    cJSON_Delete(r);
}

static int num(const cJSON *o, const char *k, int def, int lo, int hi)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
    if (!cJSON_IsNumber(v)) return def;
    int x = (int)v->valuedouble;
    return x < lo ? lo : x > hi ? hi : x;
}

static void load(void)
{
    NA = 0;
    char *txt = fs_read_file(g_path, 4096);
    if (!txt) return;
    cJSON *r = cJSON_Parse(txt);
    free(txt);
    const cJSON *arr = r ? cJSON_GetObjectItemCaseSensitive(r, "alarms") : NULL;
    const cJSON *o;
    cJSON_ArrayForEach(o, arr) {
        if (NA >= ALARM_MAX) break;
        const cJSON *on = cJSON_GetObjectItemCaseSensitive(o, "on");
        A[NA] = (alarm_t){ (uint8_t)num(o, "h", 7, 0, 23), (uint8_t)num(o, "m", 0, 0, 59), (uint8_t)num(o, "d", 0, 0, 127),
                           cJSON_IsTrue(on), 0 };
        NA++;
    }
    cJSON_Delete(r);
}

// ---------- время ----------
// key - минуты от эпохи по местному времени; wd - день недели 0=Пн..6=Вс
static bool now_local(int64_t *key, int *wd)
{
    int64_t u;
    if (!clock_utc(&u)) return false;
    int64_t t = u + (int64_t)clock_tz_min() * 60;
    *key = t / 60;
    *wd = (int)((t / 86400 + 3) % 7);          // 1970-01-01 - четверг (индекс 3)
    return true;
}

static void fmt_days(char *b, size_t n, uint8_t d)
{
    if (d == 0)    { snprintf(b, n, "Один раз"); return; }
    if (d == 127)  { snprintf(b, n, "Каждый день"); return; }
    if (d == 31)   { snprintf(b, n, "По будням"); return; }
    if (d == 96)   { snprintf(b, n, "По выходным"); return; }
    b[0] = 0;
    for (int i = 0; i < 7; i++)
        if (d >> i & 1) { size_t l = strlen(b); snprintf(b + l, n - l, "%s%s", l ? " " : "", DAY[i]); }
}

// ---------- звонок ----------
static lv_obj_t   *ring;
static lv_timer_t *ring_tm;
static int         ring_ticks;
static uint8_t     ring_h, ring_m;
static int64_t     g_snooze_key;                // минута, когда зазвонит отложенный; 0 - нет
static uint8_t     g_snooze_h, g_snooze_m;

static void list_rebuild(void);

static void ring_close(void)
{
    if (ring_tm) { lv_timer_delete(ring_tm); ring_tm = NULL; }
    if (ring) { lv_obj_delete_async(ring); ring = NULL; }
}

static void ring_stop_cb(lv_event_t *e) { LV_UNUSED(e); ring_close(); }

static void ring_snooze_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    int64_t key; int wd;
    if (now_local(&key, &wd)) { g_snooze_key = key + SNOOZE_MIN; g_snooze_h = ring_h; g_snooze_m = ring_m; }
    ring_close();
    sys_toast("Отложено на 5 минут");
}

static void ring_tick_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (++ring_ticks >= RING_TICKS) {                  // никто не нажал: оставляем запись в уведомлениях
        int64_t now;
        char txt[16];
        snprintf(txt, sizeof txt, "%02u:%02u", ring_h, ring_m);
        messages_add(MSG_ID_ALARM, clock_utc(&now) ? (uint32_t)now : 0, "Будильник", "Пропущен будильник", txt);
        ring_close();
        return;
    }
    haptic_alarm();
}

static lv_obj_t *ring_btn(lv_obj_t *p, int y, const char *text, uint32_t bg, uint32_t fg, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(p);
    lv_obj_set_size(b, 330, 65);
    lv_obj_set_pos(b, 40, y);
    lv_obj_set_style_radius(b, 33, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg == ORANGE ? 0xC66A00 : WS_FILL2), LV_STATE_PRESSED);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, WATCH_FONT_BOLD_S, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(fg), 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    return b;
}

static void ring_start(uint8_t h, uint8_t m)
{
    if (ring) return;
    ring_h = h; ring_m = m; ring_ticks = 0;
    power_wake();

    ring = lv_obj_create(lv_layer_sys());
    lv_obj_remove_style_all(ring);
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ring, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(ring, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_COVER, 0);
    lv_obj_add_flag(ring, LV_OBJ_FLAG_CLICKABLE);                 // перехватывает касания

    lv_obj_t *cap = lv_label_create(ring);
    lv_label_set_text(cap, "БУДИЛЬНИК");
    lv_obj_set_style_text_font(cap, WATCH_FONT_BODY, 0);
    lv_obj_set_style_text_color(cap, lv_color_hex(ORANGE), 0);
    lv_obj_set_style_text_letter_space(cap, 2, 0);
    lv_obj_align(cap, LV_ALIGN_TOP_MID, 0, 90);

    char b[8];
    snprintf(b, sizeof b, "%02u:%02u", h, m);
    lv_obj_t *t = lv_label_create(ring);
    lv_label_set_text(t, b);
    lv_obj_set_style_text_font(t, WATCH_FONT_HUGE, 0);
    lv_obj_set_style_text_color(t, lv_color_white(), 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 130);

    ring_btn(ring, 250, "Отложить 5 мин", WS_FILL, 0xFFFFFF, ring_snooze_cb);
    ring_btn(ring, 330, "Стоп", ORANGE, 0x000000, ring_stop_cb);

    haptic_alarm();
    ring_tm = lv_timer_create(ring_tick_cb, 3000, NULL);
}

static void check_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    int64_t key; int wd;
    if (ring || !now_local(&key, &wd)) return;
    int mod = (int)(key % 1440);

    if (g_snooze_key && key >= g_snooze_key) {
        g_snooze_key = 0;
        ring_start(g_snooze_h, g_snooze_m);
        return;
    }
    for (int i = 0; i < NA; i++) {
        alarm_t *a = &A[i];
        if (!a->on || a->h * 60 + a->m != mod || a->fired == key) continue;
        if (a->days && !(a->days >> wd & 1)) continue;
        a->fired = key;
        if (!a->days) { a->on = false; save(); list_rebuild(); }   // одноразовый выключается сам
        ring_start(a->h, a->m);
        return;
    }
}

void alarm_init(const char *data_root)
{
    snprintf(g_path, sizeof g_path, "%s/alarms.json", data_root);
    load();
    g_loaded = true;
    if (!g_chk) g_chk = lv_timer_create(check_cb, 1000, NULL);
}

// ---------- список ----------
static lv_obj_t *g_launcher, *scr, *list_body;
static void editor_open(int idx);

static void card_cb(lv_event_t *e) { editor_open((int)(uintptr_t)lv_event_get_user_data(e)); }

static void switch_cb(lv_event_t *e)
{
    int i = (int)(uintptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= NA) return;
    A[i].on = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
    A[i].fired = 0;
    save();
}

static void add_cb(lv_event_t *e) { LV_UNUSED(e); editor_open(-1); }

static void list_rebuild(void)
{
    if (!list_body) return;
    lv_obj_clean(list_body);
    for (int i = 0; i < NA; i++) {
        lv_obj_t *c = ws_card(list_body);
        lv_obj_set_height(c, 88);
        lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(c, card_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);

        char b[8];
        snprintf(b, sizeof b, "%02u:%02u", A[i].h, A[i].m);
        lv_obj_t *t = lv_label_create(c);
        lv_label_set_text(t, b);
        lv_obj_set_style_text_font(t, WATCH_FONT_MID, 0);
        lv_obj_set_style_text_color(t, A[i].on ? lv_color_white() : lv_color_hex(WS_TEXT2), 0);
        lv_obj_align(t, LV_ALIGN_TOP_LEFT, 4, 0);

        char d[40];
        fmt_days(d, sizeof d, A[i].days);
        lv_obj_t *s = lv_label_create(c);
        lv_label_set_text(s, d);
        lv_obj_set_style_text_font(s, WATCH_FONT_SMALL, 0);
        lv_obj_set_style_text_color(s, lv_color_hex(WS_TEXT2), 0);
        lv_obj_align(s, LV_ALIGN_BOTTOM_LEFT, 4, 0);

        lv_obj_t *sw = lv_switch_create(c);
        lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
        if (A[i].on) lv_obj_add_state(sw, LV_STATE_CHECKED);
        lv_obj_add_event_cb(sw, switch_cb, LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)i);
    }
    if (NA < ALARM_MAX) {
        lv_obj_t *b = lv_button_create(list_body);
        lv_obj_set_size(b, lv_pct(100), 65);
        lv_obj_set_style_radius(b, 33, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(ORANGE), 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(0xC66A00), LV_STATE_PRESSED);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, "+ Новый будильник");
        lv_obj_set_style_text_font(l, WATCH_FONT_BOLD_S, 0);
        lv_obj_set_style_text_color(l, lv_color_black(), 0);
        lv_obj_center(l);
        lv_obj_add_event_cb(b, add_cb, LV_EVENT_CLICKED, NULL);
    }
}

static void exit_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    scr = list_body = NULL;
    lv_screen_load_anim(g_launcher, sys_exit_anim(), 200, 0, true);
}

// ---------- редактор ----------
static lv_obj_t *ed_scr, *ed_h, *ed_m, *ed_hint;
static lv_obj_t *ed_chip[7];
static int       ed_idx;
static uint8_t   ed_days;

static void editor_close(void)
{
    lv_screen_load_anim(scr, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, 200, 0, true);   // экран редактора удаляется
    ed_scr = ed_h = ed_m = ed_hint = NULL;
    list_rebuild();
}

static void ed_back_cb(lv_event_t *e) { LV_UNUSED(e); editor_close(); }

static void chip_style(int i)
{
    bool on = ed_days >> i & 1;
    lv_obj_set_style_bg_color(ed_chip[i], lv_color_hex(on ? ORANGE : WS_FILL), 0);
    lv_obj_t *l = lv_obj_get_child(ed_chip[i], 0);
    lv_obj_set_style_text_color(l, on ? lv_color_black() : lv_color_white(), 0);
}

static void hint_update(void)
{
    char b[48], d[40];
    fmt_days(d, sizeof d, ed_days);
    snprintf(b, sizeof b, "%s", d);
    lv_label_set_text(ed_hint, b);
}

static void chip_cb(lv_event_t *e)
{
    int i = (int)(uintptr_t)lv_event_get_user_data(e);
    ed_days ^= (uint8_t)(1u << i);
    chip_style(i);
    hint_update();
}

static void del_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (ed_idx < 0 || ed_idx >= NA) return;
    memmove(&A[ed_idx], &A[ed_idx + 1], (size_t)(NA - ed_idx - 1) * sizeof A[0]);
    NA--;
    save();
    editor_close();
}

static void toast_until(const alarm_t *a)
{
    int64_t key; int wd;
    if (!now_local(&key, &wd)) return;
    int mod = (int)(key % 1440), target = a->h * 60 + a->m;
    for (int d = 0; d <= 7; d++) {
        int delta = d * 1440 + target - mod;
        if (delta <= 0) continue;
        if (a->days && !(a->days >> ((wd + d) % 7) & 1)) continue;
        char b[48];
        if (delta >= 60) snprintf(b, sizeof b, "Через %d ч %d мин", delta / 60, delta % 60);
        else             snprintf(b, sizeof b, "Через %d мин", delta);
        sys_toast(b);
        return;
    }
}

static void done_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    uint8_t h = (uint8_t)lv_roller_get_selected(ed_h), m = (uint8_t)lv_roller_get_selected(ed_m);
    alarm_t *a;
    if (ed_idx >= 0 && ed_idx < NA) a = &A[ed_idx];
    else if (NA < ALARM_MAX) a = &A[NA++];
    else return;
    *a = (alarm_t){ h, m, ed_days, true, 0 };
    save();
    toast_until(a);
    editor_close();
}

static lv_obj_t *roller(lv_obj_t *body, int x, int count)
{
    static char opts[64 * 3 + 1];
    int n = 0;
    for (int i = 0; i < count; i++) n += snprintf(opts + n, sizeof opts - (size_t)n, i ? "\n%02d" : "%02d", i);
    lv_obj_t *r = lv_roller_create(body);
    lv_roller_set_options(r, opts, LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(r, 3);
    lv_obj_set_width(r, 140);
    lv_obj_set_height(r, 172);
    lv_obj_set_pos(r, x, 24);
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_text_font(r, WATCH_FONT_LARGE, 0);
    lv_obj_set_style_text_color(r, lv_color_hex(0x5A5A5E), 0);
    lv_obj_set_style_text_line_space(r, 18, 0);
    lv_obj_set_style_bg_color(r, lv_color_hex(WS_CARD), LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_PART_SELECTED);
    lv_obj_set_style_radius(r, 20, LV_PART_SELECTED);
    lv_obj_set_style_text_font(r, WATCH_FONT_BOLD, LV_PART_SELECTED);
    lv_obj_set_style_text_color(r, lv_color_white(), LV_PART_SELECTED);
    return r;
}

static lv_obj_t *ed_btn(lv_obj_t *p, int x, int w, const char *text, uint32_t bg, uint32_t fg, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(p);
    lv_obj_set_size(b, w, 65);
    lv_obj_set_pos(b, x, 300);
    lv_obj_set_style_radius(b, 33, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg == ORANGE ? 0xC66A00 : WS_FILL2), LV_STATE_PRESSED);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, WATCH_FONT_BOLD_S, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(fg), 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    return b;
}

static void editor_open(int idx)
{
    ed_idx = idx;
    unsigned h = 7, m = 0;
    ed_days = 0;
    if (idx >= 0 && idx < NA) {
        h = A[idx].h; m = A[idx].m; ed_days = A[idx].days;
    } else {
        struct tm tmv;
        if (clock_local(&tmv)) { h = (unsigned)(tmv.tm_hour + 1) % 24; m = 0; }    // новый: ближайший час
    }

    lv_obj_t *body;
    ed_scr = sys_screen_create("Будильник", ed_back_cb, &body);
    lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(body, LV_LAYOUT_NONE);
    lv_obj_set_style_pad_all(body, 0, 0);

    lv_obj_t *l1 = lv_label_create(body);
    lv_label_set_text(l1, "ЧАСЫ");
    lv_obj_set_width(l1, 140);
    lv_obj_set_style_text_align(l1, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(l1, WATCH_FONT_SMALL, 0);
    lv_obj_set_style_text_color(l1, lv_color_hex(0x8E8E93), 0);
    lv_obj_set_pos(l1, 45, 0);
    lv_obj_t *l2 = lv_label_create(body);
    lv_label_set_text(l2, "МИНУТЫ");
    lv_obj_set_width(l2, 140);
    lv_obj_set_style_text_align(l2, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(l2, WATCH_FONT_SMALL, 0);
    lv_obj_set_style_text_color(l2, lv_color_hex(0x8E8E93), 0);
    lv_obj_set_pos(l2, 225, 0);

    ed_h = roller(body, 45, 24);
    ed_m = roller(body, 225, 60);
    lv_roller_set_selected(ed_h, h, LV_ANIM_OFF);
    lv_roller_set_selected(ed_m, m, LV_ANIM_OFF);

    for (int i = 0; i < 7; i++) {
        lv_obj_t *c = lv_button_create(body);
        ed_chip[i] = c;
        lv_obj_set_size(c, 46, 46);
        lv_obj_set_pos(c, 20 + i * 54, 206);
        lv_obj_set_style_radius(c, 23, 0);
        lv_obj_set_style_shadow_width(c, 0, 0);
        lv_obj_set_style_pad_all(c, 0, 0);
        lv_obj_t *t = lv_label_create(c);
        lv_label_set_text(t, DAY[i]);
        lv_obj_set_style_text_font(t, WATCH_FONT_SMALL, 0);
        lv_obj_center(t);
        lv_obj_add_event_cb(c, chip_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        chip_style(i);
    }

    ed_hint = lv_label_create(body);
    lv_obj_set_width(ed_hint, W);
    lv_obj_set_style_text_align(ed_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(ed_hint, WATCH_FONT_SMALL, 0);
    lv_obj_set_style_text_color(ed_hint, lv_color_hex(WS_TEXT2), 0);
    lv_obj_set_pos(ed_hint, 0, 262);
    hint_update();

    if (idx >= 0) {
        ed_btn(body, 20, 130, "Удалить", WS_FILL, WS_RED, del_cb);
        ed_btn(body, 160, 230, "Готово", ORANGE, 0x000000, done_cb);
    } else {
        ed_btn(body, 40, 330, "Готово", ORANGE, 0x000000, done_cb);
    }

    lv_screen_load_anim(ed_scr, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 200, 0, false);   // экран списка остаётся
}

void alarm_open(lv_obj_t *launcher)
{
    if (!g_loaded) alarm_init("data");
    g_launcher = launcher;
    lv_obj_t *body;
    scr = sys_screen_create_root("Будильник", exit_cb, &body);
    list_body = body;
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(body, 10, 0);
    list_rebuild();
    lv_screen_load_anim(scr, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
}
