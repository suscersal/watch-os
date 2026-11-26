// gyro_app.c - тест гироскопа и акселерометра. Шесть строк (ускорение X/Y/Z в g, скорость вращения X/Y/Z в °/с) с полосками.
// Внизу вердикт: если часы лежат неподвижно, |a| должно быть около 1 g, а гироскоп около 0 (смещение нуля до GYRO_BIAS_MAX °/с).
// Для экономии энергии: гироскоп включён только пока приложение открыто, опрос 10 раз в секунду, а пока часы двигают,
// экран не гаснет по бездействию. Выход - свайп вправо.
#include "gyro_app.h"
#include "imu.h"
#include "sys_ui.h"
#include "watch_fonts.h"
#include "ws_style.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define POLL_MS        100
#define GYRO_RANGE     250      // полоска гироскопа: ±250 °/с
#define ACC_RANGE      2000     // полоска акселерометра: ±2000 мg
#define GYRO_BIAS_MAX  6.0f     // в покое по каждой оси не больше, °/с
#define ACC_NORM_TOL   0.12f    // отклонение |a| от 1 g в покое
#define MOVING_DPS     25.0f    // быстрее - часы двигают: не гасить экран
#define STILL_NEEDED   8        // столько опросов подряд в покое (0.8 с), чтобы вынести вердикт

typedef struct { lv_obj_t *val, *bar; } row_t;

static lv_obj_t *g_launcher, *scr, *verdict, *info;
static row_t rows[6];
static lv_timer_t *tick;
static int still_n;
static bool gyro_on;

static void make_row(lv_obj_t *body, int i, const char *name, int range)
{
    lv_obj_t *r = lv_obj_create(body);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(r, lv_pct(100), 54);
    lv_obj_set_style_radius(r, 16, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_bg_color(r, lv_color_hex(WS_CARD), 0);
    lv_obj_set_style_pad_all(r, 8, 0);

    lv_obj_t *n = lv_label_create(r);
    lv_label_set_text(n, name);
    lv_obj_set_style_text_font(n, WATCH_FONT_SMALL, 0);
    lv_obj_set_style_text_color(n, lv_color_hex(WS_TEXT2), 0);
    lv_obj_align(n, LV_ALIGN_TOP_LEFT, 0, 0);

    rows[i].val = lv_label_create(r);
    lv_label_set_text(rows[i].val, "-");
    lv_obj_set_style_text_font(rows[i].val, WATCH_FONT_SMALL, 0);
    lv_obj_set_style_text_color(rows[i].val, lv_color_white(), 0);
    lv_obj_align(rows[i].val, LV_ALIGN_TOP_RIGHT, 0, 0);

    rows[i].bar = lv_bar_create(r);
    lv_bar_set_mode(rows[i].bar, LV_BAR_MODE_SYMMETRICAL);
    lv_bar_set_range(rows[i].bar, -range, range);
    lv_obj_set_size(rows[i].bar, lv_pct(100), 8);
    lv_obj_align(rows[i].bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(rows[i].bar, lv_color_hex(0x2A2A2E), 0);
    lv_obj_set_style_bg_color(rows[i].bar, lv_color_hex(WS_GREEN), LV_PART_INDICATOR);
}

static void set_row(int i, float v, float scale, int range, const char *fmt)
{
    char s[24];
    snprintf(s, sizeof s, fmt, v);
    lv_label_set_text(rows[i].val, s);
    int b = (int)lroundf(v * scale);
    lv_bar_set_value(rows[i].bar, b < -range ? -range : b > range ? range : b, LV_ANIM_OFF);
}

static void tick_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (!scr) return;
    imu_sample_t s;
    if (!imu_read(&s)) {
        lv_label_set_text(verdict, "Датчик не отвечает");
        lv_obj_set_style_text_color(verdict, lv_color_hex(WS_RED), 0);
        return;
    }
    set_row(0, s.ax, 1000, ACC_RANGE, "%+.2f g");
    set_row(1, s.ay, 1000, ACC_RANGE, "%+.2f g");
    set_row(2, s.az, 1000, ACC_RANGE, "%+.2f g");
    set_row(3, s.gx, 1, GYRO_RANGE, "%+.0f °/с");
    set_row(4, s.gy, 1, GYRO_RANGE, "%+.0f °/с");
    set_row(5, s.gz, 1, GYRO_RANGE, "%+.0f °/с");

    float w = fmaxf(fabsf(s.gx), fmaxf(fabsf(s.gy), fabsf(s.gz)));
    if (w > MOVING_DPS) lv_display_trigger_activity(NULL);          // часы двигают: экран не гасим
    float am = sqrtf(s.ax * s.ax + s.ay * s.ay + s.az * s.az);
    bool still = w < GYRO_BIAS_MAX * 3 && fabsf(am - 1.0f) < 0.3f;   // «лежат», иначе вердикт по покою не имеет смысла
    still_n = still ? still_n + 1 : 0;

    if (still_n >= STILL_NEEDED) {
        bool ok = w <= GYRO_BIAS_MAX && fabsf(am - 1.0f) <= ACC_NORM_TOL;
        lv_label_set_text(verdict, ok ? "Покой: датчик в норме" : "Покой: сдвиг нуля или |a| не 1 g");
        lv_obj_set_style_text_color(verdict, lv_color_hex(ok ? WS_GREEN : WS_RED), 0);
    } else if (w > MOVING_DPS) {
        lv_label_set_text(verdict, "Вращение видно: гироскоп работает");
        lv_obj_set_style_text_color(verdict, lv_color_hex(WS_GREEN), 0);
    } else {
        lv_label_set_text(verdict, "Положите часы неподвижно или покрутите");
        lv_obj_set_style_text_color(verdict, lv_color_hex(WS_TEXT2), 0);
    }
}

static void deleted_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (tick) { lv_timer_delete(tick); tick = NULL; }
    if (gyro_on) { imu_gyro_enable(false); gyro_on = false; }      // гироскоп выключаем сразу: он потребляет
    scr = NULL;
    memset(rows, 0, sizeof rows);
}

static void back_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_screen_load_anim(g_launcher, sys_exit_anim(), 200, 0, true);
}

void gyro_test_open(lv_obj_t *launcher)
{
    g_launcher = launcher;
    lv_obj_t *body;
    scr = sys_screen_create_root("Гироскоп", back_cb, &body);
    lv_obj_add_event_cb(scr, deleted_cb, LV_EVENT_DELETE, NULL);

    static const char *N[6] = { "Ускорение X", "Ускорение Y", "Ускорение Z", "Вращение X", "Вращение Y", "Вращение Z" };
    for (int i = 0; i < 6; i++) make_row(body, i, N[i], i < 3 ? ACC_RANGE : GYRO_RANGE);

    verdict = lv_label_create(body);
    lv_label_set_long_mode(verdict, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(verdict, lv_pct(100));
    lv_obj_set_style_text_font(verdict, WATCH_FONT_SMALL, 0);
    lv_label_set_text(verdict, "...");

    info = lv_label_create(body);
    lv_label_set_long_mode(info, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(info, lv_pct(100));
    lv_obj_set_style_text_font(info, WATCH_FONT_SMALL, 0);
    lv_obj_set_style_text_color(info, lv_color_hex(WS_TEXT2), 0);
    lv_label_set_text(info, imu_is_simulated() ? "Эмуляция: платы нет, значения синтетические. Выход: свайп вправо."
                                               : "Гироскоп включён только пока открыт этот экран. Выход: свайп вправо.");

    still_n = 0;
    imu_gyro_enable(true);
    gyro_on = true;
    tick = lv_timer_create(tick_cb, POLL_MS, NULL);
    lv_screen_load_anim(scr, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
}
