// Экраны по референсам watchOS: список с большой кнопкой записи, экран записи с таймером, плеер с волной и перемоткой на 15 с.
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
#define BLUE         0x0A84FF
#define REC_RED      0xFF3B30

typedef struct { unsigned seq; uint32_t dur_ms; uint8_t hh, mm; } rec_t;

static char     g_dir[96] = "data/rec";
static rec_t    recs[MAX_RECS];
static int      nrec;
static lv_obj_t *g_launcher;

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

static unsigned g_max_seq;

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

static void fmt_clock(char *b, size_t n, const rec_t *r)
{
    if (r->hh == 255) snprintf(b, n, "--:--");
    else snprintf(b, n, "%02u:%02u", r->hh, r->mm);
}

static void fmt_dur(char *b, size_t n, uint32_t ms)
{
    uint32_t s = ms / 1000;
    snprintf(b, n, "%02u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
}

// ---------- общие помощники ----------
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

static lv_obj_t *circle(lv_obj_t *parent, int d, int cx, int cy, uint32_t fill, int border_w, uint32_t border)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(o, d, d);
    lv_obj_set_pos(o, cx - d / 2, cy - d / 2);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    if (fill != 0xFFFFFFFF) {
        lv_obj_set_style_bg_color(o, lv_color_hex(fill), 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    }
    if (border_w) {
        lv_obj_set_style_border_width(o, border_w, 0);
        lv_obj_set_style_border_color(o, lv_color_hex(border), 0);
    }
    return o;
}

static void show_list(lv_screen_load_anim_t anim);
static void open_player(unsigned seq);

// ---------- экран записи ----------
static lv_obj_t   *scr_rec, *rec_time_lbl, *rec_glow;
static lv_timer_t *rec_timer;
static uint8_t     peaks[MAX_PEAKS];
static unsigned    rec_seq;
static int         npeaks;
static uint8_t     rec_hh = 255, rec_mm;

static void rec_finish(bool keep_screen_anim)
{
    LV_UNUSED(keep_screen_anim);
    if (rec_timer) { lv_timer_delete(rec_timer); rec_timer = NULL; }
    uint32_t dur = audio_rec_ms();
    audio_rec_stop();
    char wp[128], pp[128];
    path_for(wp, sizeof wp, rec_seq, "wav");
    path_for(pp, sizeof pp, rec_seq, "pk");
    if (dur < 400) {                              // случайное нажатие: пустую запись не оставляем
        fs_remove(wp);
        return;
    }
    uint8_t buf[10 + MAX_PEAKS];
    memcpy(buf, "PK1", 4);
    buf[4] = dur; buf[5] = dur >> 8; buf[6] = dur >> 16; buf[7] = dur >> 24;
    buf[8] = rec_hh; buf[9] = rec_mm;
    memcpy(buf + 10, peaks, (size_t)npeaks);
    fs_write_file(pp, (const char *)buf, 10 + (size_t)npeaks);
}

static void rec_tick(lv_timer_t *t)
{
    LV_UNUSED(t);
    uint32_t ms = audio_rec_ms();
    while ((uint32_t)npeaks * PEAK_MS < ms && npeaks < MAX_PEAKS) peaks[npeaks++] = (uint8_t)audio_rec_level();
    char b[16];
    snprintf(b, sizeof b, "%02u:%02u.%u", (unsigned)(ms / 60000), (unsigned)(ms / 1000 % 60), (unsigned)(ms / 100 % 10));
    lv_label_set_text(rec_time_lbl, b);
    int d = 138 + audio_rec_level() * 5 / 10;     // голубое кольцо пульсирует с уровнем звука
    lv_obj_set_size(rec_glow, d, d);
    lv_obj_set_pos(rec_glow, 205 - d / 2, 100 - d / 2);
    if (ms >= MAX_SECONDS * 1000u) {              // лимит одной записи
        rec_finish(true);
        scr_rec = NULL;
        show_list(LV_SCREEN_LOAD_ANIM_MOVE_RIGHT);
    }
}

static void stop_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    rec_finish(true);
    scr_rec = NULL;
    show_list(LV_SCREEN_LOAD_ANIM_MOVE_RIGHT);
}

static void start_recording(void)
{
    load_recs();
    rec_seq = g_max_seq + 1;
    char wp[128];
    path_for(wp, sizeof wp, rec_seq, "wav");
    if (!audio_rec_start(wp)) { sys_toast("Не удалось начать запись"); return; }
    npeaks = 0;
    struct tm tm;
    if (ws_now(&tm)) { rec_hh = (uint8_t)tm.tm_hour; rec_mm = (uint8_t)tm.tm_min; } else { rec_hh = 255; rec_mm = 0; }

    lv_obj_t *body;
    scr_rec = sys_screen_create_root("Запись", stop_cb, &body);
    abs_body(body);
    // y в системе тела: центр кнопки 100 (экран 162)
    rec_glow = circle(body, 138, 205, 100, 0xFFFFFFFF, 4, BLUE);
    lv_obj_set_style_border_opa(rec_glow, LV_OPA_50, 0);
    circle(body, 124, 205, 100, 0xFFFFFFFF, 4, 0xFFFFFF);
    lv_obj_t *sq = lv_obj_create(body);
    lv_obj_remove_style_all(sq);
    lv_obj_set_size(sq, 50, 50);
    lv_obj_set_pos(sq, 180, 75);
    lv_obj_set_style_radius(sq, 12, 0);
    lv_obj_set_style_bg_color(sq, lv_color_hex(REC_RED), 0);
    lv_obj_set_style_bg_opa(sq, LV_OPA_COVER, 0);
    lv_obj_t *hit = lv_button_create(body);                    // прозрачная область нажатия «стоп»
    lv_obj_set_size(hit, 130, 130);
    lv_obj_set_pos(hit, 140, 35);
    lv_obj_set_style_bg_opa(hit, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(hit, 0, 0);
    lv_obj_set_style_radius(hit, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_event_cb(hit, stop_cb, LV_EVENT_CLICKED, NULL);

    rec_time_lbl = label(body, "00:00.0", WATCH_FONT_HUGE, 0xFFFFFF, 0, 204, 410, LV_TEXT_ALIGN_CENTER);
    label(body, "Запись", WATCH_FONT_BODY, WS_TEXT2, 0, 284, 410, LV_TEXT_ALIGN_CENTER);

    rec_timer = lv_timer_create(rec_tick, 100, NULL);
    lv_screen_load_anim(scr_rec, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 200, 0, true);
}

// ---------- список ----------
static void exit_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_screen_load_anim(g_launcher, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, 200, 0, true);
}

static void record_cb(lv_event_t *e) { LV_UNUSED(e); start_recording(); }
static void card_cb(lv_event_t *e) { open_player((unsigned)(uintptr_t)lv_event_get_user_data(e)); }

static void show_list(lv_screen_load_anim_t anim)
{
    load_recs();
    lv_obj_t *body;
    lv_obj_t *scr = sys_screen_create_root("Диктофон", exit_cb, &body);
    abs_body(body);

    circle(body, 150, 205, 80, 0xFFFFFFFF, 4, 0x4A4A4E);                 // кольцо
    lv_obj_t *rb = lv_button_create(body);                                 // красная кнопка записи
    lv_obj_set_size(rb, 122, 122);
    lv_obj_set_pos(rb, 144, 19);
    lv_obj_set_style_radius(rb, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(rb, 0, 0);
    lv_obj_set_style_bg_color(rb, lv_color_hex(REC_RED), 0);
    lv_obj_set_style_bg_color(rb, lv_color_hex(0xB3231B), LV_STATE_PRESSED);
    lv_obj_add_event_cb(rb, record_cb, LV_EVENT_CLICKED, NULL);

    if (nrec == 0) {
        label(body, "Нет записей", WATCH_FONT_BODY, WS_TEXT2, 0, 200, 410, LV_TEXT_ALIGN_CENTER);
    } else {
        lv_obj_t *list = lv_obj_create(body);
        lv_obj_remove_style_all(list);
        lv_obj_set_size(list, 410, 440 - 170);
        lv_obj_set_pos(list, 0, 170);
        lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_left(list, 20, 0);
        lv_obj_set_style_pad_right(list, 20, 0);
        lv_obj_set_style_pad_row(list, 10, 0);
        lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
        for (int i = 0; i < nrec; i++) {
            lv_obj_t *c = lv_button_create(list);
            lv_obj_set_size(c, 370, 84);
            lv_obj_set_style_radius(c, 26, 0);
            lv_obj_set_style_shadow_width(c, 0, 0);
            lv_obj_set_style_pad_all(c, 0, 0);
            lv_obj_set_style_bg_color(c, lv_color_hex(WS_CARD), 0);
            lv_obj_set_style_bg_color(c, lv_color_hex(WS_FILL2), LV_STATE_PRESSED);
            lv_obj_add_event_cb(c, card_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)recs[i].seq);
            char t[12], d[12];
            fmt_clock(t, sizeof t, &recs[i]);
            fmt_dur(d, sizeof d, recs[i].dur_ms);
            label(c, "Запись", WATCH_FONT_BOLD_S, 0xFFFFFF, 20, 12, 300, LV_TEXT_ALIGN_LEFT);
            label(c, t, WATCH_FONT_BODY, WS_TEXT2, 20, 44, 120, LV_TEXT_ALIGN_LEFT);
            label(c, d, WATCH_FONT_BODY, WS_TEXT2, 230, 44, 120, LV_TEXT_ALIGN_RIGHT);
        }
    }
    lv_screen_load_anim(scr, anim, 200, 0, true);
}

// ---------- плеер ----------
#define NB 51   // столбиков волны
static lv_obj_t   *scr_play, *pl_bars[NB], *pl_play_lbl, *pl_left, *pl_right;
static lv_timer_t *pl_timer;
static unsigned    pl_seq;
static uint8_t     pl_peaks[MAX_PEAKS];
static int         pl_npeaks;

static void pl_refresh(void)
{
    if (!scr_play) return;
    uint32_t pos = audio_pos_ms(), dur = audio_duration_ms();
    int idx = (int)(pos / PEAK_MS);
    for (int i = 0; i < NB; i++) {
        int k = idx + i - NB / 2;
        int v = (k >= 0 && k < pl_npeaks) ? pl_peaks[k] : 0;
        int h = 6 + v * 100 / 100;                         // 6..106
        lv_obj_set_height(pl_bars[i], h);
        lv_obj_set_y(pl_bars[i], 75 - h / 2);
        lv_obj_set_style_bg_color(pl_bars[i], lv_color_hex(i < NB / 2 ? 0xFFFFFF : 0x6E6E73), 0);
    }
    char a[12], b[12];
    fmt_dur(a, sizeof a, pos);
    fmt_dur(b, sizeof b, dur);
    lv_label_set_text(pl_left, a);
    lv_label_set_text(pl_right, b);
    lv_label_set_text(pl_play_lbl, audio_is_playing() ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
}

static void pl_timer_cb(lv_timer_t *t) { LV_UNUSED(t); pl_refresh(); }

static void pl_leave(void)
{
    if (pl_timer) { lv_timer_delete(pl_timer); pl_timer = NULL; }
    audio_stop();
    scr_play = NULL;
}

static void pl_back_cb(lv_event_t *e) { LV_UNUSED(e); pl_leave(); show_list(LV_SCREEN_LOAD_ANIM_MOVE_RIGHT); }

static void pl_play_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (audio_finished() || !audio_is_loaded()) { char p[128]; path_for(p, sizeof p, pl_seq, "wav"); audio_play(p, 0); }
    else if (audio_is_playing()) audio_pause();
    else audio_resume();
    pl_refresh();
}

static void pl_back15_cb(lv_event_t *e) { LV_UNUSED(e); uint32_t p = audio_pos_ms(); audio_seek(p > 15000 ? p - 15000 : 0); pl_refresh(); }
static void pl_fwd15_cb(lv_event_t *e)  { LV_UNUSED(e); audio_seek(audio_pos_ms() + 15000); pl_refresh(); }

static void del_confirm_cb(bool ok, void *ud)
{
    LV_UNUSED(ud);
    if (!ok) return;
    char p[128];
    pl_leave();
    path_for(p, sizeof p, pl_seq, "wav"); fs_remove(p);
    path_for(p, sizeof p, pl_seq, "pk");  fs_remove(p);
    show_list(LV_SCREEN_LOAD_ANIM_MOVE_RIGHT);
}

static void pl_delete_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    sys_confirm("Удалить запись?", "Её нельзя будет вернуть.", del_confirm_cb, NULL);
}

static lv_obj_t *skip_button(lv_obj_t *body, int x, const char *sym, lv_event_cb_t cb)
{
    lv_obj_t *b = ws_round_button(body, 64, sym, WS_FILL, 0xFFFFFF, WATCH_FONT_LARGE, cb, NULL);
    lv_obj_set_pos(b, x, 215);
    lv_obj_t *n = lv_label_create(b);                       // "15" внутри стрелки
    lv_label_set_text(n, "15");
    lv_obj_set_style_text_font(n, WATCH_FONT_SMALL, 0);
    lv_obj_set_style_text_color(n, lv_color_white(), 0);
    lv_obj_center(n);
    return b;
}

static void open_player(unsigned seq)
{
    pl_seq = seq;
    char pp[128], wp[128];
    path_for(pp, sizeof pp, seq, "pk");
    path_for(wp, sizeof wp, seq, "wav");
    uint8_t buf[10 + MAX_PEAKS];
    long n = fs_read_range(pp, 0, buf, sizeof buf);
    pl_npeaks = (n > 10) ? (int)(n - 10) : 0;
    if (pl_npeaks > 0) memcpy(pl_peaks, buf + 10, (size_t)pl_npeaks);
    uint32_t dur_hint = (n >= 10) ? (uint32_t)buf[4] | ((uint32_t)buf[5] << 8) | ((uint32_t)buf[6] << 16) | ((uint32_t)buf[7] << 24) : 0;
    if (!audio_play(wp, dur_hint)) { sys_toast("Запись повреждена"); return; }
    audio_pause();                                          // открыли, но не играем, пока не нажмут play

    lv_obj_t *body;
    scr_play = sys_screen_create("Запись", pl_back_cb, &body);
    abs_body(body);

    lv_obj_t *band = lv_obj_create(body);                   // тёмная полоса с волной
    lv_obj_remove_style_all(band);
    lv_obj_remove_flag(band, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(band, 410, 150);
    lv_obj_set_pos(band, 0, 12);
    lv_obj_set_style_bg_color(band, lv_color_hex(WS_CARD), 0);
    lv_obj_set_style_bg_opa(band, LV_OPA_COVER, 0);
    for (int i = 0; i < NB; i++) {
        lv_obj_t *b = lv_obj_create(band);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, 4, 6);
        lv_obj_set_pos(b, i * 8 + 3, 72);
        lv_obj_set_style_radius(b, 2, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        pl_bars[i] = b;
    }
    lv_obj_t *head = lv_obj_create(body);                   // голубая линия воспроизведения с точками
    lv_obj_remove_style_all(head);
    lv_obj_set_size(head, 3, 176);
    lv_obj_set_pos(head, 204, 0);
    lv_obj_set_style_bg_color(head, lv_color_hex(BLUE), 0);
    lv_obj_set_style_bg_opa(head, LV_OPA_COVER, 0);
    circle(body, 12, 205, 6, BLUE, 0, 0);
    circle(body, 12, 205, 170, BLUE, 0, 0);

    pl_left  = label(body, "00:00", WATCH_FONT_SMALL, WS_TEXT2, 20, 184, 100, LV_TEXT_ALIGN_LEFT);
    pl_right = label(body, "00:00", WATCH_FONT_SMALL, WS_TEXT2, 290, 184, 100, LV_TEXT_ALIGN_RIGHT);

    skip_button(body, 36, "\xEF\x8B\xAA", pl_back15_cb);                      // стрелка назад
    circle(body, 92, 205, 247, 0xFFFFFFFF, 3, 0x555559);                        // кольцо вокруг play
    lv_obj_t *pp_btn = ws_round_button(body, 80, LV_SYMBOL_PAUSE, WS_FILL, 0xFFFFFF, WATCH_FONT_LARGE, pl_play_cb, NULL);
    lv_obj_set_pos(pp_btn, 165, 207);
    pl_play_lbl = lv_obj_get_child(pp_btn, 0);
    skip_button(body, 310, "\xEF\x8B\xB9", pl_fwd15_cb);                       // стрелка вперёд

    lv_obj_t *del = ws_round_button(body, 56, LV_SYMBOL_TRASH, WS_FILL, REC_RED, WATCH_FONT_BODY, pl_delete_cb, NULL);
    lv_obj_set_pos(del, 177, 305);

    pl_timer = lv_timer_create(pl_timer_cb, 100, NULL);
    pl_refresh();
    lv_screen_load_anim(scr_play, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 200, 0, true);
}

void recorder_open(lv_obj_t *launcher)
{
    g_launcher = launcher;
    show_list(LV_SCREEN_LOAD_ANIM_MOVE_LEFT);
}
