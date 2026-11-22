// Экраны: главный (плеер), громкость, очередь. Размеры и цвета сняты с макета 410x502.
#include "music_app.h"
#include "art.h"
#include "audio.h"
#include "fs.h"
#include "sys_ui.h"
#include "watch_fonts.h"
#include "ws_style.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define MAX_TRACKS 24
typedef struct {
    char title[48], artist[40], album[40], path[112];
    uint32_t dur_ms;
} track_t;

static char    g_dir[96] = "data/music";
static track_t tr[MAX_TRACKS];
static int     ntr, cur = -1;
static bool    shuffle, demo_queue;

static lv_obj_t *g_launcher;
static lv_obj_t *scr_player, *scr_volume, *scr_queue;

// ---------- список треков ----------
static void add_cb(const char *name, void *ud)
{
    LV_UNUSED(ud);
    size_t n = strlen(name);
    if (n < 5 || strcasecmp(name + n - 4, ".wav") != 0 || ntr >= MAX_TRACKS) return;
    track_t *t = &tr[ntr];
    memset(t, 0, sizeof *t);
    snprintf(t->path, sizeof t->path, "%s/%s", g_dir, name);
    wav_info_t w;
    if (!audio_wav_info(t->path, &w)) return;
    t->dur_ms = w.duration_ms;
    char base[96];
    snprintf(base, sizeof base, "%.*s", (int)(n - 4), name);
    char *sep = strstr(base, " - ");       // "Исполнитель - Название.wav"
    if (sep) {
        *sep = '\0';
        snprintf(t->artist, sizeof t->artist, "%s", base);
        snprintf(t->title, sizeof t->title, "%s", sep + 3);
    } else {
        snprintf(t->title, sizeof t->title, "%s", base);
    }
    ntr++;
}

static void load_tracks(void)
{
    ntr = 0; demo_queue = false;
    fs_list_dir(g_dir, add_cb, NULL);
    if (ntr == 0) {                        // файлов нет: демо-очередь из макета, звука нет
        demo_queue = true;
        const char *T[][3] = { { "Midnight City", "M83", "Hurry Up, We\xE2\x80\x99re Dreaming" }, { "Wait", "M83", "Hurry Up, We\xE2\x80\x99re Dreaming" } };
        uint32_t D[] = { 243000, 343000 };
        for (int i = 0; i < 2; i++) {
            snprintf(tr[i].title, sizeof tr[i].title, "%s", T[i][0]);
            snprintf(tr[i].artist, sizeof tr[i].artist, "%s", T[i][1]);
            snprintf(tr[i].album, sizeof tr[i].album, "%s", T[i][2]);
            tr[i].dur_ms = D[i];
        }
        ntr = 2;
    }
    if (cur >= ntr) cur = -1;
}

void music_init(const char *root)
{
    snprintf(g_dir, sizeof g_dir, "%s/music", root);
    fs_mkdir(root);
    fs_mkdir(g_dir);
}

static void fmt_time(char *b, size_t n, uint32_t ms, bool neg)
{
    uint32_t s = ms / 1000;
    snprintf(b, n, "%s%u:%02u", neg ? "\xE2\x88\x92" : "", (unsigned)(s / 60), (unsigned)(s % 60));
}

static void play_track(int i)
{
    if (i < 0 || i >= ntr) return;
    cur = i;
    audio_play(tr[i].path, tr[i].dur_ms);
}

static void next_track(void)
{
    if (ntr == 0) return;
    int n = cur + 1;
    if (shuffle && ntr > 1) { do n = rand() % ntr; while (n == cur); }
    if (n >= ntr) n = 0;
    play_track(n);
}

static void prev_track(void)
{
    if (ntr == 0) return;
    if (audio_pos_ms() > 3000) { audio_seek(0); return; }    // как в обычных плеерах: сначала в начало трека
    play_track(cur > 0 ? cur - 1 : ntr - 1);
}

// ---------- общее ----------
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

static lv_obj_t *pill(lv_obj_t *parent, const char *text, int x, int y, int w, lv_event_cb_t cb)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, w, 56);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_radius(b, 28, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(WS_FILL), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(WS_FILL2), LV_STATE_PRESSED);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, WATCH_FONT_BOLD_S, 0);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    return b;
}

static lv_obj_t *art_view(lv_obj_t *parent, int size, int radius, int x, int y)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, size, size);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_style_radius(box, radius, 0);
    lv_obj_set_style_clip_corner(box, true, 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *img = lv_image_create(box);
    lv_image_set_src(img, &art_demo);
    lv_image_set_pivot(img, 0, 0);
    lv_image_set_scale(img, (uint32_t)(256 * size / 144));
    lv_obj_set_pos(img, 0, 0);
    return box;
}

static void load_to(lv_obj_t *scr, lv_screen_load_anim_t anim, bool del_old)
{
    lv_screen_load_anim(scr, anim, 200, 0, del_old);
}

// ---------- громкость ----------
static lv_obj_t *vol_label, *vol_bar;

static void vol_show(void)
{
    char b[8];
    snprintf(b, sizeof b, "%d%%", audio_get_volume());
    lv_label_set_text(vol_label, b);
    lv_bar_set_value(vol_bar, audio_get_volume(), LV_ANIM_OFF);
}
static void vol_minus_cb(lv_event_t *e) { LV_UNUSED(e); audio_set_volume(audio_get_volume() - 10); vol_show(); }
static void vol_plus_cb(lv_event_t *e)  { LV_UNUSED(e); audio_set_volume(audio_get_volume() + 10); vol_show(); }
static void to_player_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    scr_volume = scr_queue = NULL;
    load_to(scr_player, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, true);
}

static void open_volume_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_obj_t *body;
    scr_volume = sys_screen_create("\xD0\x93\xD1\x80\xD0\xBE\xD0\xBC\xD0\xBA\xD0\xBE\xD1\x81\xD1\x82\xD1\x8C", to_player_cb, &body);   // Громкость
    abs_body(body);
    vol_label = label(body, "", WATCH_FONT_HUGE, 0xFFFFFF, 0, 28, 410, LV_TEXT_ALIGN_CENTER);
    label(body, "\xD0\xA3\xD1\x80\xD0\xBE\xD0\xB2\xD0\xB5\xD0\xBD\xD1\x8C \xD0\xB7\xD0\xB2\xD1\x83\xD0\xBA\xD0\xB0", WATCH_FONT_BODY, WS_TEXT2, 0, 121, 410, LV_TEXT_ALIGN_CENTER);   // Уровень звука
    vol_bar = lv_bar_create(body);
    lv_obj_set_size(vol_bar, 300, 12);
    lv_obj_set_pos(vol_bar, 55, 170);
    lv_bar_set_range(vol_bar, 0, 100);
    lv_obj_set_style_bg_color(vol_bar, lv_color_hex(WS_FILL2), 0);
    lv_obj_set_style_bg_color(vol_bar, lv_color_hex(0xFF4D6D), LV_PART_INDICATOR);
    lv_obj_t *m = ws_round_button(body, 72, LV_SYMBOL_MINUS, WS_FILL, 0xFFFFFF, WATCH_FONT_LARGE, vol_minus_cb, NULL);
    lv_obj_set_pos(m, 85, 217);
    lv_obj_t *p = ws_round_button(body, 72, LV_SYMBOL_PLUS, WS_FILL, 0xFFFFFF, WATCH_FONT_LARGE, vol_plus_cb, NULL);
    lv_obj_set_pos(p, 253, 217);
    pill(body, "\xD0\x93\xD0\xBE\xD1\x82\xD0\xBE\xD0\xB2\xD0\xBE", 99, 319, 212, to_player_cb);                   // Готово
    vol_show();
    load_to(scr_volume, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, false);
}

// ---------- очередь ----------
static void queue_item_cb(lv_event_t *e)
{
    intptr_t i = (intptr_t)lv_event_get_user_data(e);
    play_track((int)i);
    to_player_cb(NULL);
}

static void open_queue_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_obj_t *body;
    scr_queue = sys_screen_create("\xD0\x9E\xD1\x87\xD0\xB5\xD1\x80\xD0\xB5\xD0\xB4\xD1\x8C", to_player_cb, &body);   // Очередь
    abs_body(body);
    const char *album = (cur >= 0 && tr[cur].album[0]) ? tr[cur].album : "";
    label(body, album, WATCH_FONT_BODY, WS_TEXT2, 20, 6, 370, LV_TEXT_ALIGN_CENTER);

    lv_obj_t *list = lv_obj_create(body);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, 410, 220);
    lv_obj_set_pos(list, 0, 47);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_left(list, 32, 0);
    lv_obj_set_style_pad_right(list, 32, 0);
    lv_obj_set_style_pad_row(list, 16, 0);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    for (int i = 0; i < ntr; i++) {
        lv_obj_t *c = lv_button_create(list);
        lv_obj_set_size(c, 345, 96);
        lv_obj_set_style_radius(c, 28, 0);
        lv_obj_set_style_shadow_width(c, 0, 0);
        lv_obj_set_style_pad_all(c, 0, 0);
        lv_obj_set_style_bg_color(c, lv_color_hex(WS_CARD), 0);
        lv_obj_set_style_bg_color(c, lv_color_hex(WS_FILL2), LV_STATE_PRESSED);
        lv_obj_add_event_cb(c, queue_item_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        art_view(c, 56, 12, 16, 20);
        label(c, tr[i].title, WATCH_FONT_BOLD_S, 0xFFFFFF, 88, 22, 210, LV_TEXT_ALIGN_LEFT);
        char sub[96], t[12];
        fmt_time(t, sizeof t, tr[i].dur_ms, false);
        snprintf(sub, sizeof sub, "%s%s%s", tr[i].artist, tr[i].artist[0] ? " \xC2\xB7 " : "", t);   // «исполнитель · 4:03»
        label(c, sub, WATCH_FONT_SMALL, WS_TEXT2, 88, 52, 210, LV_TEXT_ALIGN_LEFT);
        if (i == cur) {                                          // розовая точка у текущего трека
            lv_obj_t *dot = lv_obj_create(c);
            lv_obj_remove_style_all(dot);
            lv_obj_set_size(dot, 9, 9);
            lv_obj_set_pos(dot, 312, 43);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(dot, lv_color_hex(0xFF4D6D), 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        }
    }
    pill(body, "\xD0\x93\xD0\xBE\xD1\x82\xD0\xBE\xD0\xB2\xD0\xBE", 99, 319, 212, to_player_cb);                   // Готово
    load_to(scr_queue, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, false);
}

// ---------- плеер ----------
static lv_obj_t *p_title, *p_sub, *p_bar, *p_left, *p_right, *p_play_lbl, *p_shuffle_btn;
static lv_timer_t *p_timer;

static void player_refresh(void)
{
    if (!scr_player) return;
    if (audio_finished()) next_track();
    const track_t *t = cur >= 0 ? &tr[cur] : NULL;
    if (t) {
        lv_label_set_text(p_title, t->title);
        char sub[96];
        snprintf(sub, sizeof sub, "%s%s%s", t->artist, (t->artist[0] && t->album[0]) ? " \xC2\xB7 " : "", t->album);
        lv_label_set_text(p_sub, sub);
    }
    uint32_t dur = audio_is_loaded() ? audio_duration_ms() : (t ? t->dur_ms : 0), pos = audio_pos_ms();
    lv_bar_set_value(p_bar, dur ? (int32_t)((uint64_t)pos * 1000 / dur) : 0, LV_ANIM_OFF);
    char a[12], b[12];
    fmt_time(a, sizeof a, pos, false);
    fmt_time(b, sizeof b, dur > pos ? dur - pos : 0, true);
    lv_label_set_text(p_left, a);
    lv_label_set_text(p_right, b);
    lv_label_set_text(p_play_lbl, audio_is_playing() ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    lv_obj_set_style_bg_color(p_shuffle_btn, lv_color_hex(shuffle ? 0xFF4D6D : WS_FILL), 0);
    lv_obj_t *sl = lv_obj_get_child(p_shuffle_btn, 0);
    if (sl) lv_obj_set_style_text_color(sl, lv_color_hex(shuffle ? 0x000000 : 0xFFFFFF), 0);
}

static void timer_cb(lv_timer_t *t) { LV_UNUSED(t); player_refresh(); }

static void play_pause_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (!audio_is_loaded() || audio_finished()) play_track(cur >= 0 ? cur : 0);
    else if (audio_is_playing()) audio_pause();
    else audio_resume();
    player_refresh();
}
static void prev_cb(lv_event_t *e) { LV_UNUSED(e); prev_track(); player_refresh(); }
static void next_cb(lv_event_t *e) { LV_UNUSED(e); next_track(); player_refresh(); }
static void shuffle_cb(lv_event_t *e) { LV_UNUSED(e); shuffle = !shuffle; player_refresh(); }

static void bar_cb(lv_event_t *e)      // касание полосы - перемотка
{
    lv_obj_t *bar = lv_event_get_target_obj(e);
    lv_indev_t *in = lv_indev_active();
    if (!in) return;
    lv_point_t p;
    lv_indev_get_point(in, &p);
    lv_area_t a;
    lv_obj_get_coords(bar, &a);
    int32_t w = a.x2 - a.x1 + 1;
    int32_t x = p.x - a.x1;
    if (x < 0) x = 0;
    if (x > w) x = w;
    audio_seek((uint32_t)((uint64_t)audio_duration_ms() * (uint32_t)x / (uint32_t)w));
    player_refresh();
}

static void exit_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    audio_stop();
    if (p_timer) { lv_timer_delete(p_timer); p_timer = NULL; }
    scr_player = NULL;
    lv_screen_load_anim(g_launcher, sys_exit_anim(), 200, 0, true);
}

void music_open(lv_obj_t *launcher)
{
    g_launcher = launcher;
    load_tracks();
    if (cur < 0 && ntr > 0) { cur = 0; }
    if (cur >= 0 && !audio_is_loaded()) { /* трек выбран, но ещё не играет: ждём нажатия на play */ }

    lv_obj_t *body;
    scr_player = sys_screen_create_root("\xD0\x9C\xD1\x83\xD0\xB7\xD1\x8B\xD0\xBA\xD0\xB0", exit_cb, &body);   // Музыка
    abs_body(body);

    art_view(body, 144, 24, 133, 7);
    p_title = label(body, "", WATCH_FONT_BOLD, 0xFFFFFF, 20, 160, 370, LV_TEXT_ALIGN_CENTER);
    p_sub   = label(body, "", WATCH_FONT_SMALL, WS_TEXT2, 20, 196, 370, LV_TEXT_ALIGN_CENTER);

    p_bar = lv_bar_create(body);
    lv_obj_set_size(p_bar, 330, 6);
    lv_obj_set_pos(p_bar, 40, 224);
    lv_bar_set_range(p_bar, 0, 1000);
    lv_obj_set_style_bg_color(p_bar, lv_color_hex(WS_FILL2), 0);
    lv_obj_set_style_bg_color(p_bar, lv_color_hex(0xFF4D6D), LV_PART_INDICATOR);
    lv_obj_set_ext_click_area(p_bar, 14);
    lv_obj_add_flag(p_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(p_bar, bar_cb, LV_EVENT_CLICKED, NULL);
    p_left  = label(body, "0:00", WATCH_FONT_SMALL, 0x606060, 40, 240, 100, LV_TEXT_ALIGN_LEFT);
    p_right = label(body, "", WATCH_FONT_SMALL, 0x606060, 270, 240, 100, LV_TEXT_ALIGN_RIGHT);

    lv_obj_t *prev = ws_round_button(body, 56, LV_SYMBOL_PREV, 0x000000, 0xFFFFFF, WATCH_FONT_LARGE, prev_cb, NULL);
    lv_obj_set_pos(prev, 64, 276);
    lv_obj_t *pp = ws_round_button(body, 80, LV_SYMBOL_PAUSE, 0xFF4D6D, 0x000000, WATCH_FONT_LARGE, play_pause_cb, NULL);
    lv_obj_set_pos(pp, 165, 264);
    p_play_lbl = lv_obj_get_child(pp, 0);
    lv_obj_t *next = ws_round_button(body, 56, LV_SYMBOL_NEXT, 0x000000, 0xFFFFFF, WATCH_FONT_LARGE, next_cb, NULL);
    lv_obj_set_pos(next, 290, 276);

    p_shuffle_btn = ws_round_button(body, 56, LV_SYMBOL_SHUFFLE, WS_FILL, 0xFFFFFF, WATCH_FONT_BODY, shuffle_cb, NULL);
    lv_obj_set_pos(p_shuffle_btn, 65, 362);
    lv_obj_t *vb = ws_round_button(body, 56, LV_SYMBOL_VOLUME_MAX, WS_FILL, 0xFFFFFF, WATCH_FONT_BODY, open_volume_cb, NULL);
    lv_obj_set_pos(vb, 177, 362);
    lv_obj_t *qb = ws_round_button(body, 56, LV_SYMBOL_LIST, WS_FILL, 0xFFFFFF, WATCH_FONT_BODY, open_queue_cb, NULL);
    lv_obj_set_pos(qb, 289, 362);

    p_timer = lv_timer_create(timer_cb, 200, NULL);
    player_refresh();
    lv_screen_load_anim(scr_player, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
}
