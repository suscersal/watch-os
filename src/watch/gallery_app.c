#include "gallery_app.h"
#include "ws_style.h"
#include "clock.h"
#include "photos.h"
#include "sys_ui.h"
#include "watch_fonts.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GRID_MAX   60      // миниатюр в сетке
#define THUMB_CELL 118     // 3 в ряд: 3*118 + 2*8 = 370 = 410 - 2*20 (при 120 влезало только 2)

// Объявлена в src/misc/cache/instance/lv_image_cache.h, которого нет в общем заголовке lvgl.h (путь к нему различается
// у ПК и ESP-IDF), поэтому объявляем прототип здесь.
void lv_image_cache_drop(const void *src);

typedef struct { lv_image_dsc_t dsc; uint8_t *data; } imgbuf_t;

static lv_obj_t *g_launcher, *grid_scr, *grid_body;
static lv_obj_t *view_scr, *view_img, *view_count, *view_date;
static imgbuf_t *view_buf;
static uint32_t  ids[PHOTOS_MAX], taken[PHOTOS_MAX];
static int       n_ids, idx = -1;
static lv_timer_t *thumb_timer, *full_timer;   // миниатюры подгружаются по две за тик; полный кадр декодируется после показа окна
static int       thumb_next, thumb_total;
static bool      grid_dirty;                   // список фото изменился (удаление): сетку надо перестроить

static void build_grid(void);

// ---------- буферы изображений ----------
// Декодер получает JPEG прямо из памяти; кэш LVGL ключуется адресом dsc, поэтому перед free() запись надо сбросить.
static imgbuf_t *imgbuf_load(uint32_t id, bool thumb)
{
    size_t len = 0;
    uint8_t *d = photos_load(id, thumb, &len);
    if (!d) return NULL;
    imgbuf_t *b = calloc(1, sizeof *b);
    if (!b) { free(d); return NULL; }
    b->data = d;
    b->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    b->dsc.header.cf = LV_COLOR_FORMAT_RAW;
    int w = 0, h = 0;
    if (!photos_check_jpeg(d, len, 4096, 4096, &w, &h)) { free(d); free(b); return NULL; }
    b->dsc.header.w = (uint32_t)w;       // для JPEG из памяти декодер берёт размеры из заголовка dsc
    b->dsc.header.h = (uint32_t)h;
    b->dsc.data = d;
    b->dsc.data_size = (uint32_t)len;
    return b;
}

static void imgbuf_free(imgbuf_t *b)
{
    if (!b) return;
    lv_image_cache_drop(&b->dsc);
    free(b->data);
    free(b);
}

static void thumb_deleted_cb(lv_event_t *e) { imgbuf_free(lv_event_get_user_data(e)); }

// ---------- просмотр ----------
static void viewer_show(int i);

static void viewer_gone_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (full_timer) { lv_timer_delete(full_timer); full_timer = NULL; }
    imgbuf_free(view_buf);
    view_buf = NULL;
    view_scr = view_img = view_count = view_date = NULL;
    idx = -1;
}

static void viewer_back_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (grid_dirty) { build_grid(); grid_dirty = false; }      // без удаления сетка уже готова: заново ничего не читаем с диска
    else if (thumb_timer) lv_timer_resume(thumb_timer);
    lv_screen_load_anim(grid_scr, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, 200, 0, true);   // окно просмотра удаляется
}

void gallery_viewer_step(int dir)
{
    if (!view_scr || n_ids == 0) return;
    int ni = idx + dir;
    if (ni < 0 || ni >= n_ids) return;
    viewer_show(ni);
}

int gallery_viewer_index(void) { return view_scr ? idx : -1; }

static void step_cb(lv_event_t *e) { gallery_viewer_step((int)(intptr_t)lv_event_get_user_data(e)); }

static void gesture_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_indev_t *in = lv_indev_active();
    if (!in) return;
    lv_dir_t d = lv_indev_get_gesture_dir(in);
    if (d == LV_DIR_LEFT)  gallery_viewer_step(+1);
    if (d == LV_DIR_RIGHT) gallery_viewer_step(-1);
}

// Полный кадр 410x502 декодируется программно и занимает заметное время. Поэтому окно появляется сразу с маленькой
// миниатюрой, а полный кадр подгружается следом, отдельным тиком таймера.
static void full_timer_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    full_timer = NULL;                          // таймер с одним повтором LVGL удалит сам
    if (!view_scr || idx < 0 || idx >= n_ids) return;
    imgbuf_t *nb = imgbuf_load(ids[idx], false);
    if (!nb) { sys_toast("Cannot open photo"); return; }       // остаётся миниатюра
    lv_image_set_src(view_img, &nb->dsc);
    imgbuf_free(view_buf);                      // старый буфер освобождаем после переключения виджета
    view_buf = nb;
}

static void viewer_show(int i)
{
    idx = i;
    if (full_timer) { lv_timer_delete(full_timer); full_timer = NULL; }
    imgbuf_t *tb = imgbuf_load(ids[i], true);
    lv_image_set_src(view_img, tb ? &tb->dsc : NULL);
    imgbuf_free(view_buf);
    view_buf = tb;
    full_timer = lv_timer_create(full_timer_cb, 30, NULL);
    lv_timer_set_repeat_count(full_timer, 1);

    char t[48];
    snprintf(t, sizeof t, "%d / %d", i + 1, n_ids);
    lv_label_set_text(view_count, t);
    char d[40];
    lv_label_set_text(view_date, clock_format_epoch(taken[i], d, sizeof d) ? d : "");
}

static void delete_confirm_cb(bool ok, void *ud)
{
    LV_UNUSED(ud);
    if (!ok || !view_scr || idx < 0) return;
    uint32_t id = ids[idx];
    if (!photos_delete(id)) { sys_toast("Could not delete"); return; }
    sys_toast("Photo deleted");
    grid_dirty = true;
    int keep = idx;
    n_ids = photos_list(ids, taken, PHOTOS_MAX);
    if (n_ids == 0) { viewer_back_cb(NULL); return; }
    viewer_show(keep < n_ids ? keep : n_ids - 1);
}

static void delete_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    sys_confirm("Delete photo?", "The photo will be removed from the watch.", delete_confirm_cb, NULL);
}

static lv_obj_t *round_btn(lv_obj_t *parent, const char *sym, uint32_t color, lv_event_cb_t cb, void *ud)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, 52, 52);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_80, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, sym);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    return b;
}

static void viewer_open(int start)
{
    n_ids = photos_list(ids, taken, PHOTOS_MAX);
    if (n_ids == 0 || start < 0 || start >= n_ids) return;

    view_scr = lv_obj_create(NULL);
    lv_obj_remove_flag(view_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(view_scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(view_scr, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(view_scr, WATCH_FONT_BODY, 0);
    lv_obj_set_style_pad_all(view_scr, 0, 0);
    lv_obj_add_event_cb(view_scr, viewer_gone_cb, LV_EVENT_DELETE, NULL);
    lv_obj_add_event_cb(view_scr, gesture_cb, LV_EVENT_GESTURE, NULL);

    view_img = lv_image_create(view_scr);
    lv_obj_center(view_img);

    lv_obj_align(round_btn(view_scr, LV_SYMBOL_LEFT, WS_FILL, viewer_back_cb, NULL), LV_ALIGN_TOP_LEFT, WS_CORNER_INSET(52), WS_CORNER_INSET(52));
    lv_obj_align(round_btn(view_scr, LV_SYMBOL_TRASH, 0xB91C1C, delete_cb, NULL), LV_ALIGN_TOP_RIGHT, -WS_CORNER_INSET(52), WS_CORNER_INSET(52));
    lv_obj_align(round_btn(view_scr, LV_SYMBOL_PREV, WS_FILL, step_cb, (void *)(intptr_t)-1), LV_ALIGN_BOTTOM_LEFT, WS_CORNER_INSET(52), -WS_CORNER_INSET(52));
    lv_obj_align(round_btn(view_scr, LV_SYMBOL_NEXT, WS_FILL, step_cb, (void *)(intptr_t)+1), LV_ALIGN_BOTTOM_RIGHT, -WS_CORNER_INSET(52), -WS_CORNER_INSET(52));

    view_count = lv_label_create(view_scr);
    lv_obj_set_style_text_color(view_count, lv_color_white(), 0);
    lv_obj_align(view_count, LV_ALIGN_BOTTOM_MID, 0, -34);
    view_date = lv_label_create(view_scr);
    lv_obj_set_style_text_color(view_date, lv_color_hex(0x9CA3AF), 0);
    lv_obj_set_style_text_font(view_date, WATCH_FONT_SMALL, 0);
    lv_obj_align(view_date, LV_ALIGN_BOTTOM_MID, 0, -12);

    viewer_show(start);
    lv_screen_load_anim(view_scr, LV_SCREEN_LOAD_ANIM_FADE_IN, 150, 0, false);       // сетку не удаляем
}

// ---------- сетка миниатюр ----------
static void cell_cb(lv_event_t *e)
{
    if (thumb_timer) lv_timer_pause(thumb_timer);     // пока открыт просмотр, миниатюры не грузим
    viewer_open((int)(intptr_t)lv_event_get_user_data(e));
}

static void thumbs_stop(void)
{
    if (thumb_timer) { lv_timer_delete(thumb_timer); thumb_timer = NULL; }
}

// Ячейки создаются сразу (дёшево), а JPEG миниатюр читаются с диска по две за тик: интерфейс не замирает на сотни миллисекунд.
static void thumbs_tick_cb(lv_timer_t *t)
{
    if (!grid_body || grid_dirty) { thumb_timer = NULL; lv_timer_delete(t); return; }
    for (int k = 0; k < 2 && thumb_next < thumb_total; k++, thumb_next++) {
        lv_obj_t *cell = lv_obj_get_child(grid_body, thumb_next);
        imgbuf_t *b = cell ? imgbuf_load(ids[thumb_next], true) : NULL;
        if (!b) continue;
        lv_obj_t *img = lv_image_create(cell);
        lv_image_set_src(img, &b->dsc);
        lv_obj_center(img);
        lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);                               // касание достаётся ячейке
        lv_obj_add_event_cb(img, thumb_deleted_cb, LV_EVENT_DELETE, b);
    }
    if (thumb_next >= thumb_total) { thumb_timer = NULL; lv_timer_delete(t); }
}

static void build_grid(void)
{
    if (!grid_body) return;
    thumbs_stop();
    lv_obj_clean(grid_body);
    n_ids = photos_list(ids, taken, PHOTOS_MAX);

    if (n_ids == 0) {
        lv_obj_t *l = lv_label_create(grid_body);
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(l, lv_pct(100));
        lv_label_set_text(l, "No photos yet.\nOpen Sync on the watch and send photos from the phone app.");
        lv_obj_set_style_text_color(l, lv_color_hex(0x9CA3AF), 0);
        return;
    }
    int shown = n_ids < GRID_MAX ? n_ids : GRID_MAX;
    for (int i = 0; i < shown; i++) {
        lv_obj_t *cell = lv_obj_create(grid_body);
        lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(cell, THUMB_CELL, THUMB_CELL);
        lv_obj_set_style_pad_all(cell, 0, 0);
        lv_obj_set_style_radius(cell, 10, 0);
        lv_obj_set_style_border_width(cell, 0, 0);
        lv_obj_set_style_bg_color(cell, lv_color_hex(WS_CARD), 0);
        lv_obj_set_style_clip_corner(cell, true, 0);
        lv_obj_add_event_cb(cell, cell_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    if (n_ids > shown) {
        lv_obj_t *l = lv_label_create(grid_body);
        char t[64];
        snprintf(t, sizeof t, "Showing %d newest of %d", shown, n_ids);
        lv_label_set_text(l, t);
        lv_obj_set_style_text_color(l, lv_color_hex(0x9CA3AF), 0);
    }
    thumb_next = 0;
    thumb_total = shown;
    thumb_timer = lv_timer_create(thumbs_tick_cb, 5, NULL);
}

static void grid_gone_cb(lv_event_t *e) { LV_UNUSED(e); thumbs_stop(); grid_scr = grid_body = NULL; grid_dirty = false; }

static void grid_back_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_screen_load_anim(g_launcher, sys_exit_anim(), 200, 0, true);   // экран галереи удаляется
}

void gallery_open(lv_obj_t *launcher)
{
    g_launcher = launcher;
    grid_scr = sys_screen_create_root("Gallery", grid_back_cb, &grid_body);
    lv_obj_add_event_cb(grid_scr, grid_gone_cb, LV_EVENT_DELETE, NULL);
    lv_obj_set_flex_flow(grid_body, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(grid_body, 8, 0);
    lv_obj_set_style_pad_column(grid_body, 8, 0);
    grid_dirty = false;
    build_grid();
    lv_screen_load_anim(grid_scr, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 200, 0, false);     // лаунчер не удаляем
}
