// launcher.c - сотовая сетка иконок как на Apple Watch. LVGL 9.
// Список приложений берётся из apps/*/manifest.json (см. watch/app_manager.c).
#include "watch/lvgl_inc.h"
#include "watch/app_manager.h"
#include "watch/app_runtime.h"
#include "watch/storage.h"
#include "watch/auth.h"
#include "watch/lock_screen.h"
#include "watch/sys_ui.h"
#include "watch/watch_fonts.h"

// Корень файлов. ПК: относительно текущей папки. ESP32: "/flash/" (задаётся в CMake порта).
#ifndef WATCH_ROOT
#define WATCH_ROOT ""
#endif

#define SCR_W    410
#define SCR_H    502
#define ICON     92     // диаметр иконки в покое
#define PITCH_X  100    // шаг по горизонтали
#define PITCH_Y  87     // шаг по вертикали (~ PITCH_X * 0.866 - сотовая сетка)
#define MIN_TOP  30     // отступ сверху, если сетка не помещается и прокручивается

// Эффект "рыбий глаз" как на Apple Watch: иконка у центра экрана крупнее, к краям уменьшается
// и подтягивается к центру, чтобы расстояние между иконками не росло.
#define SCALE_MAX   272   // 256 = 100%; в центре иконка чуть больше обычной
#define SCALE_MIN   140   // у краёв
#define FISH_R      270   // на таком расстоянии от центра экрана достигается минимум
#define PULL_PCT    30    // насколько сильно уменьшенные иконки подтягиваются к центру (%)

static lv_obj_t *launcher;

// Масштаб и сдвиг каждой иконки зависят от расстояния до центра экрана.
// Стиль меняем только если значение реально изменилось, иначе каждый кадр прокрутки перерисовывал бы всё зря.
static void fisheye_update(lv_obj_t *cont)
{
    uint32_t n = lv_obj_get_child_count(cont);
    for (uint32_t k = 0; k < n; k++) {
        lv_obj_t *c = lv_obj_get_child(cont, k);
        lv_area_t a;
        lv_obj_get_coords(c, &a);
        int32_t dx = (a.x1 + a.x2) / 2 - SCR_W / 2;
        int32_t dy = (a.y1 + a.y2) / 2 - SCR_H / 2;
        int32_t d = (int32_t)lv_sqrt32((uint32_t)(dx * dx + dy * dy));
        if (d > FISH_R) d = FISH_R;

        int32_t s  = SCALE_MAX - (SCALE_MAX - SCALE_MIN) * d / FISH_R;
        int32_t pull = (SCALE_MAX - s) * PULL_PCT / SCALE_MAX;        // 0..PULL_PCT процентов
        int32_t tx = -dx * pull / 100;
        int32_t ty = -dy * pull / 100;

        if (lv_obj_get_style_transform_scale_x(c, 0) != s) {
            lv_obj_set_style_transform_pivot_x(c, ICON / 2, 0);
            lv_obj_set_style_transform_pivot_y(c, ICON / 2, 0);
            lv_obj_set_style_transform_scale(c, s, 0);
        }
        if (lv_obj_get_style_translate_x(c, 0) != tx) lv_obj_set_style_translate_x(c, tx, 0);
        if (lv_obj_get_style_translate_y(c, 0) != ty) lv_obj_set_style_translate_y(c, ty, 0);
    }
}

static void scroll_cb(lv_event_t *e)
{
    fisheye_update(lv_event_get_target_obj(e));
}

static void app_cb(lv_event_t *e)
{
    const app_info_t *a = lv_event_get_user_data(e);
    if (a->builtin) a->builtin(launcher);    // системное приложение (Install)
    else            app_runtime_launch(a, launcher);
}

static void build_grid(void);

void watch_launcher_create(void)
{
    launcher = lv_screen_active();
    lv_obj_set_style_bg_color(launcher, lv_color_black(), 0);
    lv_obj_set_style_pad_all(launcher, 0, 0);
    lv_obj_remove_flag(launcher, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(launcher, WATCH_FONT_BODY, 0);   // шрифт с кириллицей

    sys_ui_init();
    auth_init(WATCH_ROOT "data");
    storage_init(WATCH_ROOT "data");          // PIN и настройки безопасности, папка data/ недоступна приложениям
    app_manager_scan(WATCH_ROOT "apps");   // приложения берутся из папки apps/ (manifest.json у каждого)
    build_grid();
    lock_screen_init();         // если задан PIN, стартуем заблокированными
}

// Перестроить сетку после установки/удаления приложения.
void watch_launcher_refresh(void)
{
    lv_obj_clean(launcher);
    build_grid();
}

static void build_grid(void)
{
    size_t n = app_manager_count();
    if (n == 0) {
        lv_obj_t *empty = lv_label_create(launcher);
        lv_label_set_text(empty, "No apps in ./apps");
        lv_obj_set_style_text_color(empty, lv_color_white(), 0);
        lv_obj_center(empty);
        return;
    }
    // прокручиваемый контейнер на весь экран
    lv_obj_t *cont = lv_obj_create(launcher);
    lv_obj_set_size(cont, SCR_W, SCR_H);
    lv_obj_set_pos(cont, 0, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_radius(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_style_pad_bottom(cont, 160, 0);   // запас, чтобы нижние иконки доехали до центра
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_event_cb(cont, scroll_cb, LV_EVENT_SCROLL, NULL);

    // Сколько рядов получится: 3 иконки, 4, 3, 4, ...
    int rows = 0;
    for (size_t left = n; left > 0; rows++) {
        size_t cols = (rows % 2 == 0) ? 3 : 4;
        left = left > cols ? left - cols : 0;
    }
    int grid_h = (rows - 1) * PITCH_Y + ICON;
    int y0 = (SCR_H - grid_h) / 2;            // небольшая сетка стоит по центру экрана, как на часах
    if (y0 < MIN_TOP) y0 = MIN_TOP;           // большая прокручивается

    size_t i = 0;
    int row = 0;
    while (i < n) {
        int cols = (row % 2 == 0) ? 3 : 4;
        int in_row = (int)(n - i) < cols ? (int)(n - i) : cols;   // последний ряд может быть неполным
        int total_w = (in_row - 1) * PITCH_X + ICON;
        int x0 = (SCR_W - total_w) / 2;
        int y = y0 + row * PITCH_Y;

        for (int c = 0; c < in_row; c++, i++) {
            lv_obj_t *btn = lv_button_create(cont);
            lv_obj_set_size(btn, ICON, ICON);
            lv_obj_set_pos(btn, x0 + c * PITCH_X, y);
            lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(btn, lv_color_hex(app_manager_get(i)->color), 0);
            // мягкий вертикальный градиент + тонкая светлая кромка
            lv_obj_set_style_bg_grad_color(btn, lv_color_darken(lv_color_hex(app_manager_get(i)->color), 60), 0);
            lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_VER, 0);
            lv_obj_set_style_border_width(btn, 2, 0);
            lv_obj_set_style_border_color(btn, lv_color_white(), 0);
            lv_obj_set_style_border_opa(btn, LV_OPA_20, 0);
            lv_obj_set_style_shadow_width(btn, 0, 0);
            lv_obj_add_event_cb(btn, app_cb, LV_EVENT_CLICKED, (void *)app_manager_get(i));

            lv_obj_t *l = lv_label_create(btn);
            lv_label_set_text(l, app_icon_symbol(app_manager_get(i)->icon));
            lv_obj_set_style_text_color(l, lv_color_white(), 0);
            lv_obj_set_style_text_font(l, WATCH_FONT_LARGE, 0);
            lv_obj_center(l);
        }
        row++;
    }

    lv_obj_update_layout(cont);
    fisheye_update(cont);
}
