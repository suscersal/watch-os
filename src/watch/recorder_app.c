// Диктофон по макету Figma (410x502): один экран с состояниями
//   готов -> запись <-> пауза записи -> сохранено -> воспроизведение <-> пауза -> прослушано.
// Прогресс воспроизведения рисуется красной линией по рамке экрана, волна показывает всю запись целиком.
// Список записей (если их больше одной) открывается по нажатию на «Последняя запись · мм:сс»; удаление - долгое нажатие на запись.
#include "recorder_app.h"
#include "audio.h"
#include "fs.h"
#include "sys_ui.h"
#include "watch_fonts.h"
#include "ws_style.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_RECS     20
#define MAX_SECONDS  120
#define PEAK_MS      100
#define MAX_PEAKS    (MAX_SECONDS * 1000 / PEAK_MS + 8)

// цвета макета
#define REC_RED      0xFF3B30
#define C_TEXT       0xF5F5F7
#define C_CARD2      0x1C1C1F   // тёмные круглые кнопки
#define C_TRACK      0x252527   // дорожка рамки
#define C_CAP        0x919196   // подписи
#define C_DATE       0x929296
#define C_WAVE_OFF   0x49494D   // не проиграно
#define C_WAVE_IDLE  0x404045   // пустая волна
#define C_WAVE_PAUSE 0x919196   // запись на паузе

#define NB           57         // столбиков волны: шаг 6 px, ширина 3 px, всего 342 px
#define WAVE_X       34
#define WAVE_CY      188        // центр волны по вертикали (координаты тела экрана; тело начинается на y=62)
#define WAVE_W       340        // путь указателя воспроизведения

typedef struct { unsigned seq; uint32_t dur_ms; uint8_t hh, mm; } rec_t;

typedef enum { S_IDLE, S_REC, S_RECPAUSE, S_SAVED, S_PLAY, S_PPAUSE, S_DONE } rstate_t;
typedef enum { IC_NONE, IC_PLAY, IC_PAUSE, IC_STOP, IC_STOP_SQ, IC_X, IC_REC_RED, IC_REC_WHITE } icon_t;

static char     g_dir[96] = "data/rec";
static rec_t    recs[MAX_RECS];
static int      nrec;
static lv_obj_t *g_launcher;
static unsigned g_max_seq;

// ---------- файлы ----------
static void path_for(char *out, size_t n, unsigned seq, const char *ext)
{
    snprintf(out, n, "%s/r%u.%s", g_dir, seq, ext);
}

void recorder_init(const char *root)
{
    snprintf(g_dir, sizeof g_dir, "%s/rec", root);
    fs_mkdir(root);
    fs_mkdir(g_dir);
}

static void scan_cb(const char *name, void *ud)
{
    LV_UNUSED(ud);
    unsigned seq;
    size_t n = strlen(name);
    if (n < 4 || strcmp(name + n - 3, ".pk") != 0 || sscanf(name, "r%u.pk", &seq) != 1) return;
    if (seq > g_max_seq) g_max_seq = seq;
    if (nrec >= MAX_RECS) {                       // оставляем самые новые
        int mi = 0;
        for (int i = 1; i < nrec; i++) if (recs[i].seq < recs[mi].seq) mi = i;
        if (recs[mi].seq > seq) return;
        nrec--; recs[mi] = recs[nrec];
    }
    char p[128];
    path_for(p, sizeof p, seq, "pk");
    uint8_t h[10];
    if (fs_read_range(p, 0, h, sizeof h) < 10 || memcmp(h, "PK1", 4) != 0) return;
    rec_t *r = &recs[nrec++];
    r->seq = seq;
    r->dur_ms = (uint32_t)h[4] | ((uint32_t)h[5] << 8) | ((uint32_t)h[6] << 16) | ((uint32_t)h[7] << 24);
    r->hh = h[8]; r->mm = h[9];
}

static int cmp_new_first(const void *a, const void *b)
{
    const rec_t *x = a, *y = b;
    return (x->seq < y->seq) - (x->seq > y->seq);
}

static void load_recs(void)
{
    nrec = 0; g_max_seq = 0;
    fs_list_dir(g_dir, scan_cb, NULL);
    qsort(recs, (size_t)nrec, sizeof recs[0], cmp_new_first);
}

static void fmt_clock(char *b, size_t n, uint8_t hh, uint8_t mm)
{
    if (hh == 255) snprintf(b, n, "--:--");
    else snprintf(b, n, "%02u:%02u", hh, mm);
}

static void fmt_dur(char *b, size_t n, uint32_t ms)
{
    uint32_t s = ms / 1000;
    snprintf(b, n, "%02u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
}

// ---------- текущая запись (та, что слушаем или только что записали) ----------
static unsigned cur_seq;            // 0 - записей нет
static uint32_t cur_dur;
static uint8_t  cur_hh = 255, cur_mm;
static bool     cur_today;          // записана в этом сеансе: в подписи "Сегодня"
static uint8_t  peaks[MAX_PEAKS];   // уровни 0..100 раз в PEAK_MS: пишутся при записи, читаются из .pk при прослушивании
static int      npeaks;
static unsigned peaks_seq;          // чьи это уровни (0 - ничьи)

static void select_rec(const rec_t *r, bool today)
{
    cur_seq = r->seq; cur_dur = r->dur_ms; cur_hh = r->hh; cur_mm = r->mm; cur_today = today;
}

static void select_latest(void)
{
    if (nrec > 0) select_rec(&recs[0], false);
    else { cur_seq = 0; cur_dur = 0; }
}

static void load_peaks(void)
{
    if (peaks_seq == cur_seq) return;
    char pp[128];
    uint8_t buf[10 + MAX_PEAKS];
    path_for(pp, sizeof pp, cur_seq, "pk");
    long n = fs_read_range(pp, 0, buf, sizeof buf);
    npeaks = (n > 10) ? (int)(n - 10) : 0;
    if (npeaks > 0) memcpy(peaks, buf + 10, (size_t)npeaks);
    peaks_seq = cur_seq;
}

// ---------- рамка: дорожка и красный прогресс ----------
typedef struct { bool arc; float x1, y1, x2, y2, cx, cy, r, a0, len; } seg_t;
static seg_t S[9];
static float S_total;
static float g_prog;                // 0..1, сколько рамки закрашено
static bool  g_prog_on;

static void build_path(void)        // осевая линия: прямоугольник (10,10) 390x482, радиус 48, обводка 3 px внутрь
{
    const float L = 11.5f, T = 11.5f, R = 398.5f, B = 490.5f, r = 46.5f, cx = 205.0f, pi2 = 1.5707963f;
    seg_t *s = S;
    *s++ = (seg_t){ false, cx, T, R - r, T, 0, 0, 0, 0, R - r - cx };
    *s++ = (seg_t){ true, 0, 0, 0, 0, R - r, T + r, r, 270, pi2 * r };
    *s++ = (seg_t){ false, R, T + r, R, B - r, 0, 0, 0, 0, B - T - 2 * r };
    *s++ = (seg_t){ true, 0, 0, 0, 0, R - r, B - r, r, 0, pi2 * r };
    *s++ = (seg_t){ false, R - r, B, L + r, B, 0, 0, 0, 0, R - L - 2 * r };
    *s++ = (seg_t){ true, 0, 0, 0, 0, L + r, B - r, r, 90, pi2 * r };
    *s++ = (seg_t){ false, L, B - r, L, T + r, 0, 0, 0, 0, B - T - 2 * r };
    *s++ = (seg_t){ true, 0, 0, 0, 0, L + r, T + r, r, 180, pi2 * r };
    *s++ = (seg_t){ false, L + r, T, cx, T, 0, 0, 0, 0, cx - L - r };
    S_total = 0;
    for (int i = 0; i < 9; i++) S_total += S[i].len;
}

static void prog_draw_cb(lv_event_t *e)
{
    if (!g_prog_on) return;
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    float left = g_prog * S_total;
    for (int i = 0; i < 9 && left > 0.01f; i++) {
        const seg_t *s = &S[i];
        float take = left < s->len ? left : s->len;
        left -= take;
        float f = take / s->len;
        if (!s->arc) {
            lv_draw_line_dsc_t ld;
            lv_draw_line_dsc_init(&ld);
            ld.color = lv_color_hex(REC_RED);
            ld.width = 3;
            ld.round_start = ld.round_end = 1;
            ld.p1.x = a.x1 + s->x1;                 ld.p1.y = a.y1 + s->y1;
            ld.p2.x = a.x1 + s->x1 + (s->x2 - s->x1) * f;
            ld.p2.y = a.y1 + s->y1 + (s->y2 - s->y1) * f;
            lv_draw_line(layer, &ld);
        } else {
            lv_draw_arc_dsc_t ad;
            lv_draw_arc_dsc_init(&ad);
            ad.color = lv_color_hex(REC_RED);
            ad.width = 3;
            ad.rounded = 1;
            ad.center.x = (int32_t)(a.x1 + s->cx);
            ad.center.y = (int32_t)(a.y1 + s->cy);
            ad.radius = (uint16_t)s->r;
            ad.start_angle = (lv_value_precise_t)s->a0;
            ad.end_angle = (lv_value_precise_t)(s->a0 + 90.0f * f);
            lv_draw_arc(layer, &ad);
        }
    }
}

// ---------- помощники ----------
static void abs_body(lv_obj_t *body)
{
    lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(body, LV_LAYOUT_NONE);
    lv_obj_set_style_pad_all(body, 0, 0);
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color, int x, int y, int w, lv_text_align_t al)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_width(l, w);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(l, al, 0);
    lv_obj_set_pos(l, x, y);
    return l;
}

static lv_obj_t *shape(lv_obj_t *parent, int w, int h, int radius, uint32_t color)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

// ---------- экран ----------
static rstate_t st;
static lv_obj_t *scr_main, *st_lbl, *st_dot, *name_lbl, *date_lbl, *time_lbl, *cap_c, *cap_l, *cap_r, *head;
static lv_obj_t *bars[NB], *slot_btn[3], *slot_cap[3];
static lv_obj_t *track, *prog;
static lv_timer_t *tick;

// запись
static unsigned    rec_seq;
static uint8_t     rec_hh = 255, rec_mm;

static const int SLOT_CX[3] = { 137, 205, 273 };   // центры кнопок (x), y центра = 349 в теле экрана

static void set_icon(lv_obj_t *btn, icon_t ic)
{
    lv_obj_clean(btn);
    switch (ic) {
    case IC_PLAY: {
        lv_obj_t *l = lv_label_create(btn);
        lv_label_set_text(l, LV_SYMBOL_PLAY);
        lv_obj_set_style_text_font(l, WATCH_FONT_BODY, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(C_TEXT), 0);
        lv_obj_align(l, LV_ALIGN_CENTER, 1, 0);
        break; }
    case IC_X: {
        lv_obj_t *l = lv_label_create(btn);
        lv_label_set_text(l, LV_SYMBOL_CLOSE);
        lv_obj_set_style_text_font(l, WATCH_FONT_BODY, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(C_TEXT), 0);
        lv_obj_center(l);
        break; }
    case IC_PAUSE:
        lv_obj_align(shape(btn, 4, 16, 1, C_TEXT), LV_ALIGN_CENTER, -5, 0);
        lv_obj_align(shape(btn, 4, 16, 1, C_TEXT), LV_ALIGN_CENTER, 5, 0);
        break;
    case IC_STOP:    lv_obj_center(shape(btn, 14, 14, 3, C_TEXT)); break;
    case IC_STOP_SQ: lv_obj_center(shape(btn, 16, 16, 3, C_TEXT)); break;
    case IC_REC_RED:   lv_obj_center(shape(btn, 14, 14, LV_RADIUS_CIRCLE, REC_RED)); break;
    case IC_REC_WHITE: lv_obj_center(shape(btn, 18, 18, LV_RADIUS_CIRCLE, C_TEXT)); break;
    case IC_NONE: break;
    }
}

static int bar_h(int v)
{
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    return 4 + v * 58 / 100;                      // 4..62 px
}

// mode: 0 - пустая линия, 1 - живая запись (последние уровни), 2 - обзор всей записи; played_px < 0 - указателя нет
static void wave_update(int mode, uint32_t on, uint32_t off, int played_px)
{
    for (int i = 0; i < NB; i++) {
        int h = 3;
        if (mode == 1) {
            int k = npeaks - NB + i;
            h = (k >= 0 && k < npeaks) ? bar_h(peaks[k]) : 3;
        } else if (mode == 2 && npeaks > 0) {
            int a = i * npeaks / NB, b = (i + 1) * npeaks / NB;
            if (b <= a) b = a + 1;
            int m = 0;
            for (int k = a; k < b && k < npeaks; k++) if (peaks[k] > m) m = peaks[k];
            h = bar_h(m);
        } else if (mode == 2) {
            h = 3;
        }
        lv_obj_set_height(bars[i], h);
        lv_obj_set_y(bars[i], WAVE_CY - h / 2);
        uint32_t c = (mode == 2 && i * 6 < played_px) ? on : (mode == 2 ? off : on);
        lv_obj_set_style_bg_color(bars[i], lv_color_hex(c), 0);
    }
    if (played_px >= 0) {
        lv_obj_remove_flag(head, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_x(head, WAVE_X + played_px);
    } else {
        lv_obj_add_flag(head, LV_OBJ_FLAG_HIDDEN);
    }
}

static void set_time(uint32_t ms)
{
    char b[12];
    fmt_dur(b, sizeof b, ms);
    lv_label_set_text(time_lbl, b);
}

static void set_status(const char *t, bool dot)
{
    lv_label_set_text(st_lbl, t);
    if (!dot) {
        lv_obj_add_flag(st_dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(st_lbl, 34, 51);
        lv_obj_set_width(st_lbl, 342);
        return;
    }
    lv_obj_set_width(st_lbl, LV_SIZE_CONTENT);
    lv_obj_update_layout(st_lbl);
    int w = lv_obj_get_width(st_lbl), total = 7 + 6 + w, x0 = 205 - total / 2;
    lv_obj_remove_flag(st_dot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(st_dot, x0, 55);
    lv_obj_set_pos(st_lbl, x0 + 13, 51);
}

static void play_pos_refresh(void)           // положение, время, рамка, волна при воспроизведении
{
    uint32_t pos = (st == S_DONE) ? cur_dur : audio_pos_ms();
    uint32_t dur = cur_dur ? cur_dur : 1;
    if (pos > dur) pos = dur;
    float f = (float)pos / (float)dur;
    g_prog = f;
    int px = (int)(f * WAVE_W);
    wave_update(2, REC_RED, C_WAVE_OFF, px);
    set_time(pos);
    char b[12];
    fmt_dur(b, sizeof b, pos);
    lv_label_set_text(cap_l, b);
    lv_obj_invalidate(prog);
}

typedef struct { bool show; uint32_t bg; icon_t ic; const char *cap; } slot_t;

static void apply_state(rstate_t ns)
{
    st = ns;
    char b[48], c[12];
    slot_t sl[3] = { { false, C_CARD2, IC_NONE, "" }, { false, C_CARD2, IC_NONE, "" }, { false, C_CARD2, IC_NONE, "" } };
    bool playback = (ns == S_SAVED || ns == S_PLAY || ns == S_PPAUSE || ns == S_DONE);
    bool recording = (ns == S_REC || ns == S_RECPAUSE);
    bool has_last = (cur_seq != 0);

    lv_obj_add_flag(cap_c, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cap_l, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cap_r, LV_OBJ_FLAG_HIDDEN);

    // название и дата
    if (recording || ns == S_IDLE || cur_today) lv_label_set_text(name_lbl, "Новая запись");
    else { snprintf(b, sizeof b, "Запись %u", cur_seq); lv_label_set_text(name_lbl, b); }
    if (ns == S_IDLE) {
        lv_label_set_text(date_lbl, "Нажмите красную кнопку");
    } else {
        uint8_t hh = recording ? rec_hh : cur_hh, mm = recording ? rec_mm : cur_mm;
        fmt_clock(c, sizeof c, hh, mm);
        snprintf(b, sizeof b, (recording || cur_today) ? "Сегодня, %s" : "Записано в %s", c);
        lv_label_set_text(date_lbl, b);
    }

    g_prog_on = playback;
    switch (ns) {
    case S_IDLE:
        set_status("ГОТОВ К ЗАПИСИ", false);
        wave_update(0, C_WAVE_IDLE, C_WAVE_IDLE, -1);
        set_time(0);
        if (has_last) {
            fmt_dur(c, sizeof c, cur_dur);
            snprintf(b, sizeof b, "Последняя запись · %s", c);
            lv_label_set_text(cap_c, b);
            lv_obj_remove_flag(cap_c, LV_OBJ_FLAG_HIDDEN);
            sl[0] = (slot_t){ true, C_CARD2, IC_PLAY, "Слушать" };
        }
        sl[1] = (slot_t){ true, REC_RED, IC_REC_WHITE, "Записать" };
        break;
    case S_REC:
    case S_RECPAUSE:
        set_status(ns == S_REC ? "ИДЁТ ЗАПИСЬ" : "ЗАПИСЬ НА ПАУЗЕ", ns == S_REC);
        wave_update(1, ns == S_REC ? REC_RED : C_WAVE_PAUSE, 0, -1);
        set_time(audio_rec_ms());
        lv_label_set_text(cap_c, "Моно · Высокое качество");
        lv_obj_remove_flag(cap_c, LV_OBJ_FLAG_HIDDEN);
        sl[0] = (slot_t){ true, C_CARD2, IC_X, "Отмена" };
        if (audio_rec_can_pause()) {
            if (ns == S_REC) sl[1] = (slot_t){ true, C_CARD2, IC_PAUSE, "Пауза" };
            else             sl[1] = (slot_t){ true, C_CARD2, IC_PLAY, "Продолжить" };
        }
        sl[2] = (slot_t){ true, REC_RED, IC_STOP_SQ, "Готово" };
        break;
    case S_SAVED:
    case S_PLAY:
    case S_PPAUSE:
    case S_DONE:
        set_status(ns == S_SAVED ? "ЗАПИСЬ СОХРАНЕНА" : ns == S_PLAY ? "ВОСПРОИЗВЕДЕНИЕ" : ns == S_PPAUSE ? "ПАУЗА" : "ПРОСЛУШАНО", false);
        fmt_dur(c, sizeof c, cur_dur);
        lv_label_set_text(cap_r, c);
        lv_obj_remove_flag(cap_l, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(cap_r, LV_OBJ_FLAG_HIDDEN);
        sl[0] = (slot_t){ true, C_CARD2, IC_REC_RED, "Записать" };
        if (ns == S_PLAY)         sl[1] = (slot_t){ true, REC_RED, IC_PAUSE, "Пауза" };
        else if (ns == S_DONE)    sl[1] = (slot_t){ true, REC_RED, IC_PLAY, "Заново" };
        else                      sl[1] = (slot_t){ true, REC_RED, IC_PLAY, "Слушать" };
        sl[2] = (slot_t){ true, C_CARD2, IC_STOP, "Стоп" };
        if (ns == S_SAVED) { load_peaks(); }
        play_pos_refresh();
        break;
    }
    for (int i = 0; i < 3; i++) {
        if (!sl[i].show) {
            lv_obj_add_flag(slot_btn[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(slot_cap[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(slot_btn[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(slot_cap[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(slot_btn[i], lv_color_hex(sl[i].bg), 0);
        lv_obj_set_style_bg_color(slot_btn[i], lv_color_hex(sl[i].bg == REC_RED ? 0xB3231B : 0x303034), LV_STATE_PRESSED);
        set_icon(slot_btn[i], sl[i].ic);
        lv_label_set_text(slot_cap[i], sl[i].cap);
    }
    lv_obj_invalidate(prog);
}

// ---------- действия ----------
static void show_list(lv_screen_load_anim_t anim);
static void build_main(lv_screen_load_anim_t anim, rstate_t first);

static void rec_cleanup_timer(void) { if (tick) { lv_timer_delete(tick); tick = NULL; } }

static bool start_recording(void)
{
    audio_stop();
    load_recs();
    rec_seq = g_max_seq + 1;
    char wp[128];
    path_for(wp, sizeof wp, rec_seq, "wav");
    if (!audio_rec_start(wp)) { sys_toast("Не удалось начать запись"); return false; }
    npeaks = 0;
    peaks_seq = rec_seq;
    struct tm tm;
    if (ws_now(&tm)) { rec_hh = (uint8_t)tm.tm_hour; rec_mm = (uint8_t)tm.tm_min; } else { rec_hh = 255; rec_mm = 0; }
    return true;
}

// Завершает запись. true - запись сохранена и стала текущей, false - выброшена (слишком короткая или отмена).
static bool rec_finish(bool keep)
{
    uint32_t dur = audio_rec_ms();
    audio_rec_stop();
    char wp[128], pp[128];
    path_for(wp, sizeof wp, rec_seq, "wav");
    path_for(pp, sizeof pp, rec_seq, "pk");
    if (!keep || dur < 400) {                     // отмена или случайное нажатие: ничего не оставляем
        fs_remove(wp);
        peaks_seq = 0;
        return false;
    }
    uint8_t buf[10 + MAX_PEAKS];
    memcpy(buf, "PK1", 4);
    buf[4] = dur; buf[5] = dur >> 8; buf[6] = dur >> 16; buf[7] = dur >> 24;
    buf[8] = rec_hh; buf[9] = rec_mm;
    memcpy(buf + 10, peaks, (size_t)npeaks);
    fs_write_file(pp, (const char *)buf, 10 + (size_t)npeaks);
    cur_seq = rec_seq; cur_dur = dur; cur_hh = rec_hh; cur_mm = rec_mm; cur_today = true;
    peaks_seq = rec_seq;
    return true;
}

static void go_idle(void)
{
    audio_stop();
    load_recs();
    select_latest();
    apply_state(S_IDLE);
}

static void save_and_show(void)
{
    if (rec_finish(true)) apply_state(S_SAVED);
    else go_idle();
}

static bool play_from_start(void)
{
    if (!cur_seq) return false;
    load_peaks();
    char wp[128];
    path_for(wp, sizeof wp, cur_seq, "wav");
    if (!audio_play(wp, cur_dur)) { sys_toast("Запись повреждена"); return false; }
    return true;
}

static void cancel_confirm_cb(bool ok, void *ud)
{
    LV_UNUSED(ud);
    if (!ok || !scr_main) return;
    rec_finish(false);
    go_idle();
}

static void slot_cb(lv_event_t *e)
{
    int slot = (int)(uintptr_t)lv_event_get_user_data(e);
    switch (st) {
    case S_IDLE:
        if (slot == 0 && cur_seq) { if (play_from_start()) apply_state(S_PLAY); }
        else if (slot == 1) { if (start_recording()) apply_state(S_REC); }
        break;
    case S_REC:
    case S_RECPAUSE:
        if (slot == 0) {
            if (audio_rec_ms() >= 3000) sys_confirm("Отменить запись?", "Записанное будет удалено.", cancel_confirm_cb, NULL);
            else { rec_finish(false); go_idle(); }
        } else if (slot == 1) {
            if (st == S_REC) { audio_rec_pause(); apply_state(S_RECPAUSE); }
            else { audio_rec_resume(); apply_state(S_REC); }
        } else if (slot == 2) {
            save_and_show();
        }
        break;
    case S_SAVED:
    case S_PLAY:
    case S_PPAUSE:
    case S_DONE:
        if (slot == 0) { audio_stop(); if (start_recording()) apply_state(S_REC); else go_idle(); }
        else if (slot == 1) {
            if (st == S_PLAY) { audio_pause(); play_pos_refresh(); apply_state(S_PPAUSE); }
            else if (st == S_PPAUSE) { audio_resume(); apply_state(S_PLAY); }
            else if (play_from_start()) apply_state(S_PLAY);          // сохранено или прослушано: с начала
        } else if (slot == 2) {
            go_idle();
        }
        break;
    }
}

static void tick_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (!scr_main) return;
    if (st == S_REC) {
        uint32_t ms = audio_rec_ms();
        while ((uint32_t)npeaks * PEAK_MS < ms && npeaks < MAX_PEAKS) peaks[npeaks++] = (uint8_t)audio_rec_level();
        set_time(ms);
        wave_update(1, REC_RED, 0, -1);
        if (ms >= MAX_SECONDS * 1000u) save_and_show();           // лимит одной записи
    } else if (st == S_PLAY) {
        if (audio_finished()) apply_state(S_DONE);
        else play_pos_refresh();
    }
}

static void leave_main(void)
{
    rec_cleanup_timer();
    if (st == S_REC || st == S_RECPAUSE) rec_finish(true);      // выходим во время записи: сохраняем, как раньше
    audio_stop();
    scr_main = NULL;
}

static void exit_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    leave_main();
    lv_screen_load_anim(g_launcher, sys_exit_anim(), 200, 0, true);
}

static void list_open_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (st != S_IDLE) return;
    leave_main();
    show_list(LV_SCREEN_LOAD_ANIM_MOVE_LEFT);
}

// ---------- построение экрана ----------
static void build_main(lv_screen_load_anim_t anim, rstate_t first)
{
    build_path();
    lv_obj_t *body;
    scr_main = sys_screen_create_root("Диктофон", exit_cb, &body);
    abs_body(body);

    st_lbl   = label(body, "", WATCH_FONT_SMALL, REC_RED, 34, 51, 342, LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_letter_space(st_lbl, 1, 0);
    st_dot   = shape(body, 7, 7, LV_RADIUS_CIRCLE, REC_RED);
    name_lbl = label(body, "", WATCH_FONT_BOLD, C_TEXT, 34, 72, 342, LV_TEXT_ALIGN_CENTER);
    date_lbl = label(body, "", WATCH_FONT_SMALL, C_DATE, 34, 110, 342, LV_TEXT_ALIGN_CENTER);

    for (int i = 0; i < NB; i++) {
        bars[i] = shape(body, 3, 3, 2, C_WAVE_IDLE);
        lv_obj_set_pos(bars[i], WAVE_X + i * 6, WAVE_CY - 1);
    }
    head = shape(body, 1, 68, 0, C_TEXT);                       // указатель воспроизведения
    lv_obj_set_pos(head, WAVE_X, WAVE_CY - 34);

    time_lbl = label(body, "00:00", WATCH_FONT_MID, C_TEXT, 0, 238, 410, LV_TEXT_ALIGN_CENTER);

    cap_c = label(body, "", WATCH_FONT_SMALL, C_CAP, 34, 291, 342, LV_TEXT_ALIGN_CENTER);
    lv_obj_add_flag(cap_c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(cap_c, 12);
    lv_obj_add_event_cb(cap_c, list_open_cb, LV_EVENT_CLICKED, NULL);
    cap_l = label(body, "00:00", WATCH_FONT_SMALL, C_CAP, 34, 291, 80, LV_TEXT_ALIGN_LEFT);
    cap_r = label(body, "00:00", WATCH_FONT_SMALL, C_CAP, 296, 291, 80, LV_TEXT_ALIGN_RIGHT);

    for (int i = 0; i < 3; i++) {
        lv_obj_t *b = lv_button_create(body);
        lv_obj_set_size(b, 40, 40);
        lv_obj_set_pos(b, SLOT_CX[i] - 20, 329);
        lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_pad_all(b, 0, 0);
        lv_obj_set_ext_click_area(b, 12);
        lv_obj_add_event_cb(b, slot_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        slot_btn[i] = b;
        slot_cap[i] = label(body, "", WATCH_FONT_SMALL, C_CAP, SLOT_CX[i] - 50, 379, 100, LV_TEXT_ALIGN_CENTER);
        lv_label_set_long_mode(slot_cap[i], LV_LABEL_LONG_CLIP);
    }

    // рамка поверх всего: серая дорожка и красный прогресс
    track = lv_obj_create(scr_main);
    lv_obj_remove_style_all(track);
    lv_obj_remove_flag(track, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(track, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(track, 390, 482);
    lv_obj_set_pos(track, 10, 10);
    lv_obj_set_style_radius(track, 48, 0);
    lv_obj_set_style_border_width(track, 3, 0);
    lv_obj_set_style_border_color(track, lv_color_hex(C_TRACK), 0);
    lv_obj_set_style_border_opa(track, LV_OPA_COVER, 0);
    prog = lv_obj_create(scr_main);
    lv_obj_remove_style_all(prog);
    lv_obj_remove_flag(prog, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(prog, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(prog, 410, 502);
    lv_obj_set_pos(prog, 0, 0);
    lv_obj_add_event_cb(prog, prog_draw_cb, LV_EVENT_DRAW_POST, NULL);

    tick = lv_timer_create(tick_cb, 100, NULL);
    g_prog = 0;
    apply_state(first);
    // Лаунчер удалять нельзя: в него возвращает exit_cb (g_launcher). Свои прежние экраны (список записей) удаляем.
    lv_screen_load_anim(scr_main, anim, 200, 0, lv_screen_active() != g_launcher);
}

// ---------- список записей ----------
static void card_cb(lv_event_t *e)
{
    unsigned seq = (unsigned)(uintptr_t)lv_event_get_user_data(e);
    for (int i = 0; i < nrec; i++) if (recs[i].seq == seq) { select_rec(&recs[i], false); break; }
    build_main(LV_SCREEN_LOAD_ANIM_MOVE_LEFT, S_SAVED);
    if (play_from_start()) apply_state(S_PLAY);
}

static unsigned del_seq;
static void del_confirm_cb(bool ok, void *ud)
{
    LV_UNUSED(ud);
    if (!ok) return;
    char p[128];
    path_for(p, sizeof p, del_seq, "wav"); fs_remove(p);
    path_for(p, sizeof p, del_seq, "pk");  fs_remove(p);
    if (peaks_seq == del_seq) peaks_seq = 0;
    show_list(LV_SCREEN_LOAD_ANIM_NONE);
}

static void card_long_cb(lv_event_t *e)
{
    del_seq = (unsigned)(uintptr_t)lv_event_get_user_data(e);
    sys_confirm("Удалить запись?", "Её нельзя будет вернуть.", del_confirm_cb, NULL);
}

static void list_back_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    load_recs();
    select_latest();
    build_main(LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, S_IDLE);
}

static void show_list(lv_screen_load_anim_t anim)
{
    load_recs();
    lv_obj_t *body;
    lv_obj_t *scr = sys_screen_create("Записи", list_back_cb, &body);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_OFF);
    if (nrec == 0) {
        lv_obj_t *l = lv_label_create(body);
        lv_label_set_text(l, "Нет записей");
        lv_obj_set_style_text_color(l, lv_color_hex(WS_TEXT2), 0);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(l, lv_pct(100));
    }
    for (int i = 0; i < nrec; i++) {
        lv_obj_t *c = lv_button_create(body);
        lv_obj_set_size(c, 370, 84);
        lv_obj_set_style_radius(c, 26, 0);
        lv_obj_set_style_shadow_width(c, 0, 0);
        lv_obj_set_style_pad_all(c, 0, 0);
        lv_obj_set_style_bg_color(c, lv_color_hex(WS_CARD), 0);
        lv_obj_set_style_bg_color(c, lv_color_hex(WS_FILL2), LV_STATE_PRESSED);
        lv_obj_add_event_cb(c, card_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)recs[i].seq);
        lv_obj_add_event_cb(c, card_long_cb, LV_EVENT_LONG_PRESSED, (void *)(uintptr_t)recs[i].seq);
        char t[12], d[12], n[24];
        fmt_clock(t, sizeof t, recs[i].hh, recs[i].mm);
        fmt_dur(d, sizeof d, recs[i].dur_ms);
        snprintf(n, sizeof n, "Запись %u", recs[i].seq);
        label(c, n, WATCH_FONT_BOLD_S, 0xFFFFFF, 20, 12, 300, LV_TEXT_ALIGN_LEFT);
        label(c, t, WATCH_FONT_BODY, WS_TEXT2, 20, 44, 120, LV_TEXT_ALIGN_LEFT);
        label(c, d, WATCH_FONT_BODY, WS_TEXT2, 230, 44, 120, LV_TEXT_ALIGN_RIGHT);
    }
    if (nrec > 0) {
        lv_obj_t *h = lv_label_create(body);
        lv_label_set_text(h, "Удерживайте запись, чтобы удалить");
        lv_obj_set_style_text_font(h, WATCH_FONT_SMALL, 0);
        lv_obj_set_style_text_color(h, lv_color_hex(WS_TEXT2), 0);
    }
    lv_screen_load_anim(scr, anim, 200, 0, true);
}

void recorder_open(lv_obj_t *launcher)
{
    g_launcher = launcher;
    load_recs();
    select_latest();
    peaks_seq = 0;
    build_main(LV_SCREEN_LOAD_ANIM_MOVE_LEFT, S_IDLE);
}
