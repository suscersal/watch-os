// battery_app.c - экран «Батарея». Данные берутся из plat_battery_info(); оценка времени считается по истории заряда,
// которую battery_init() пишет раз в SAMPLE_MS (в памяти, после перезагрузки история пуста).
#include "battery_app.h"
#include "plat.h"
#include "sys_ui.h"
#include "watch_fonts.h"
#include "ws_style.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HIST_MAX      48                  // 48 точек по 5 минут = 4 часа
#define SAMPLE_MS     (5 * 60 * 1000)
#define MIN_SAMPLE_MS (60 * 1000)         // при открытии экрана новую точку пишем, если прошла хотя бы минута
#define EST_WINDOW_MIN 120                // оценку считаем по последним двум часам: она должна отражать нынешний расход
#define EST_MIN_SPAN   10                 // минимум минут и процентов, чтобы оценке можно было верить
#define EST_MIN_DELTA  2

enum { MODE_DISCHARGE = 0, MODE_CHARGE = 1, MODE_OTHER = 2 };   // OTHER: питание от USB, но не заряжаем (полная или пауза)

typedef struct { uint32_t t; uint8_t pct; uint8_t mode; } sample_t;

static sample_t g_hist[HIST_MAX];
static int      g_n;                      // сколько точек записано (самые старые вытесняются)
static lv_timer_t *g_sampler;

static lv_obj_t *g_launcher, *scr, *body;
static struct {
    lv_obj_t *pct, *bar, *state, *rows[5], *est_title, *chart_hint, *chart;
    lv_chart_series_t *ser;
    lv_timer_t *tick;
} U;

static int mode_of(const plat_batt_info_t *b)
{
    if (b->charging) return MODE_CHARGE;
    if (!b->vbus)    return MODE_DISCHARGE;
    return MODE_OTHER;
}

static void push_sample(const plat_batt_info_t *b)
{
    if (g_n == HIST_MAX) { memmove(g_hist, g_hist + 1, (HIST_MAX - 1) * sizeof g_hist[0]); g_n--; }
    g_hist[g_n].t = lv_tick_get();
    g_hist[g_n].pct = (uint8_t)b->pct;
    g_hist[g_n].mode = (uint8_t)mode_of(b);
    g_n++;
}

static void sample_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    plat_batt_info_t b;
    if (plat_battery_info(&b)) push_sample(&b);
}

void battery_init(void)
{
    sample_cb(NULL);
    if (!g_sampler) g_sampler = lv_timer_create(sample_cb, SAMPLE_MS, NULL);
}

// Сколько минут осталось до разрядки (на батарее) или до полного заряда (на зарядке); -1, если данных пока мало.
static int estimate_min(const plat_batt_info_t *b)
{
    int mode = mode_of(b);
    if (mode == MODE_OTHER) return -1;
    uint32_t now = lv_tick_get();
    int first = -1;
    for (int i = g_n - 1; i >= 0; i--) {                                     // хвост истории с тем же режимом, не старше окна
        if (g_hist[i].mode != mode) break;
        if ((now - g_hist[i].t) / 60000u > (uint32_t)EST_WINDOW_MIN) break;
        first = i;
    }
    if (first < 0) return -1;
    int span = (int)((now - g_hist[first].t) / 60000u);
    int delta = mode == MODE_CHARGE ? b->pct - g_hist[first].pct : g_hist[first].pct - b->pct;
    if (span < EST_MIN_SPAN || delta < EST_MIN_DELTA) return -1;
    int left = mode == MODE_CHARGE ? 100 - b->pct : b->pct;
    long min = (long)left * span / delta;                                    // скорость = delta / span процентов в минуту
    if (min < 1) min = 1;
    return min > 99 * 60 ? -1 : (int)min;
}

static void fmt_duration(char *out, size_t n, int min)
{
    if (min >= 60) snprintf(out, n, "~%d ч %02d мин", min / 60, min % 60);
    else           snprintf(out, n, "~%d мин", min);
}

static uint32_t level_color(const plat_batt_info_t *b)
{
    if (b->charging || b->full) return WS_GREEN;
    if (b->pct <= 10) return WS_RED;
    if (b->pct <= 20) return WS_ORANGE;
    return WS_GREEN;
}

static const char *state_text(const plat_batt_info_t *b)
{
    if (b->full)     return "Заряжена";
    if (b->charging) return "Заряжается";
    if (b->vbus)     return "Подключена, не заряжается";
    return "Разряжается";
}

static void set_row(int i, const char *value)
{
    if (U.rows[i]) lv_label_set_text(U.rows[i], value);
}

static void update_ui(void)
{
    if (!scr) return;
    plat_batt_info_t b;
    if (!plat_battery_info(&b)) {
        lv_label_set_text(U.pct, "--");
        lv_label_set_text(U.state, "Батарея не найдена");
        lv_bar_set_value(U.bar, 0, LV_ANIM_OFF);
        for (int i = 0; i < 5; i++) set_row(i, "-");
        return;
    }
    char s[48];
    snprintf(s, sizeof s, "%d%%", b.pct);
    lv_label_set_text(U.pct, s);
    lv_obj_set_style_text_color(U.pct, lv_color_hex(level_color(&b)), 0);
    lv_bar_set_value(U.bar, b.pct, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(U.bar, lv_color_hex(level_color(&b)), LV_PART_INDICATOR);
    lv_label_set_text(U.state, state_text(&b));

    set_row(0, b.vbus ? "USB" : "Батарея");
    if (b.mv == PLAT_BATT_UNKNOWN) set_row(1, "-");
    else { snprintf(s, sizeof s, "%d.%02d В", b.mv / 1000, (b.mv % 1000) / 10); set_row(1, s); }
    if (b.temp_c10 == PLAT_BATT_UNKNOWN) set_row(2, "-");
    else { snprintf(s, sizeof s, "%d.%d °C", b.temp_c10 / 10, abs(b.temp_c10 % 10)); set_row(2, s); }

    int est = estimate_min(&b);
    if (mode_of(&b) == MODE_OTHER) { lv_label_set_text(U.est_title, "Оценка"); set_row(3, "-"); }
    else {
        lv_label_set_text(U.est_title, b.charging ? "До полной" : "Хватит на");
        if (est < 0) set_row(3, "считаю...");
        else { fmt_duration(s, sizeof s, est); set_row(3, s); }
    }

    // график: последние точки истории, пока их меньше двух - подсказка
    if (g_n >= 2) {
        lv_obj_add_flag(U.chart_hint, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(U.chart, LV_OBJ_FLAG_HIDDEN);
        lv_chart_set_point_count(U.chart, (uint32_t)g_n);
        for (int i = 0; i < g_n; i++) lv_chart_set_series_value_by_id(U.chart, U.ser, (uint32_t)i, g_hist[i].pct);
        lv_chart_refresh(U.chart);
        int span = (int)((lv_tick_get() - g_hist[0].t) / 60000u);
        if (span >= 60) snprintf(s, sizeof s, "За %d ч %02d мин", span / 60, span % 60);
        else            snprintf(s, sizeof s, "За %d мин", span);
        set_row(4, s);
    } else {
        lv_obj_add_flag(U.chart, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(U.chart_hint, LV_OBJ_FLAG_HIDDEN);
        set_row(4, "-");
    }
}

static void tick_cb(lv_timer_t *t) { LV_UNUSED(t); update_ui(); }

static void deleted_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (U.tick) lv_timer_delete(U.tick);
    memset(&U, 0, sizeof U);
    scr = body = NULL;
}

static void back_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_screen_load_anim(g_launcher, sys_exit_anim(), 200, 0, true);
}

// Строка «подпись слева, значение справа» внутри карточки; возвращает метку значения.
static lv_obj_t *add_row(lv_obj_t *card, const char *title, lv_obj_t **title_out)
{
    lv_obj_t *row = lv_obj_create(card);
    lv_obj_remove_style_all(row);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *l = lv_label_create(row);
    lv_label_set_text(l, title);
    lv_obj_set_style_text_color(l, lv_color_hex(WS_TEXT2), 0);
    if (title_out) *title_out = l;
    lv_obj_t *v = lv_label_create(row);
    lv_label_set_text(v, "-");
    lv_obj_set_style_text_color(v, lv_color_white(), 0);
    return v;
}

void battery_open(lv_obj_t *launcher)
{
    g_launcher = launcher;
    scr = sys_screen_create_root("Батарея", back_cb, &body);
    lv_obj_add_event_cb(scr, deleted_cb, LV_EVENT_DELETE, NULL);
    memset(&U, 0, sizeof U);

    plat_batt_info_t b;
    if (plat_battery_info(&b) && (g_n == 0 || lv_tick_elaps(g_hist[g_n - 1].t) >= MIN_SAMPLE_MS)) push_sample(&b);

    // крупная карточка: процент, полоска, состояние
    lv_obj_t *hero = ws_card(body);
    lv_obj_set_flex_flow(hero, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(hero, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(hero, 8, 0);
    U.pct = lv_label_create(hero);
    lv_obj_set_style_text_font(U.pct, WATCH_FONT_HUGE, 0);
    lv_label_set_text(U.pct, "--");
    U.bar = lv_bar_create(hero);
    lv_obj_set_size(U.bar, lv_pct(100), 14);
    lv_bar_set_range(U.bar, 0, 100);
    lv_obj_set_style_radius(U.bar, 7, 0);
    lv_obj_set_style_radius(U.bar, 7, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(U.bar, lv_color_hex(WS_FILL2), 0);
    lv_obj_set_style_bg_opa(U.bar, LV_OPA_COVER, 0);
    U.state = lv_label_create(hero);
    lv_obj_set_style_text_font(U.state, WATCH_FONT_BOLD_S, 0);
    lv_obj_set_style_text_color(U.state, lv_color_white(), 0);
    lv_label_set_text(U.state, "");

    // подробности
    lv_obj_t *det = ws_card(body);
    lv_obj_set_flex_flow(det, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(det, 10, 0);
    U.rows[0] = add_row(det, "Питание", NULL);
    U.rows[1] = add_row(det, "Напряжение", NULL);
    U.rows[2] = add_row(det, "Температура", NULL);
    U.rows[3] = add_row(det, "Хватит на", &U.est_title);

    // график заряда
    lv_obj_t *gc = ws_card(body);
    lv_obj_set_flex_flow(gc, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(gc, 8, 0);
    U.rows[4] = add_row(gc, "История заряда", NULL);
    U.chart_hint = lv_label_create(gc);
    lv_label_set_long_mode(U.chart_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(U.chart_hint, lv_pct(100));
    lv_obj_set_style_text_font(U.chart_hint, WATCH_FONT_SMALL, 0);
    lv_obj_set_style_text_color(U.chart_hint, lv_color_hex(WS_TEXT2), 0);
    lv_label_set_text(U.chart_hint, "График появится через несколько минут: заряд записывается раз в 5 минут.");
    U.chart = lv_chart_create(gc);
    lv_obj_set_size(U.chart, lv_pct(100), 110);
    lv_chart_set_type(U.chart, LV_CHART_TYPE_LINE);
    lv_chart_set_range(U.chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_div_line_count(U.chart, 3, 0);
    lv_obj_set_style_bg_opa(U.chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(U.chart, 0, 0);
    lv_obj_set_style_pad_all(U.chart, 4, 0);
    lv_obj_set_style_line_color(U.chart, lv_color_hex(WS_FILL2), LV_PART_MAIN);
    lv_obj_set_style_size(U.chart, 0, 0, LV_PART_INDICATOR);              // без кружков на точках
    lv_obj_set_style_line_width(U.chart, 3, LV_PART_ITEMS);
    U.ser = lv_chart_add_series(U.chart, lv_color_hex(WS_GREEN), LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_add_flag(U.chart, LV_OBJ_FLAG_HIDDEN);

    update_ui();
    U.tick = lv_timer_create(tick_cb, 2000, NULL);
    lv_screen_load_anim(scr, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 200, 0, false);   // лаунчер не удаляем
}
