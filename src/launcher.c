// launcher.c - сотовая сетка иконок как на Apple Watch. LVGL 9.
// Список приложений берётся из apps/*/manifest.json (см. watch/app_manager.c).
#include <stdio.h>
#include "watch/lvgl_inc.h"
#include "watch/app_manager.h"
#include "watch/app_runtime.h"
#include "watch/buttons.h"
#include "watch/settings.h"
#include "watch/statusbar.h"
#include "watch/storage.h"
#include "watch/sync_net.h"
#include "watch/power.h"
#include "watch/ws_style.h"
#include "watch/music_app.h"
#include "watch/recorder_app.h"
#include "watch/auth.h"
#include "watch/paired.h"
#include "watch/lock_screen.h"
#include "watch/photos.h"
#include "watch/sys_ui.h"
#include "watch/watch_fonts.h"
#include "watch/icon_data.h"

// Корень файлов. ПК: относительно текущей папки. ESP32: "/flash/" (задаётся в CMake порта).
#ifndef WATCH_ROOT
#define WATCH_ROOT ""
#endif

#define SCR_W    410
#define SCR_H    502
#define ICON     112    // диаметр иконки в покое
#define PITCH_X  110    // шаг по горизонтали
#define PITCH_Y  135    // шаг по вертикали: под иконкой место для подписи (без подписей было 95 - сотовая сетка вплотную)
#define LABEL_W  104    // ширина подписи; длинные названия обрезаются многоточием
#define LABEL_GAP 4     // зазор между иконкой и подписью
#define LABEL_H  20     // высота строки подписи (учитывается при центрировании сетки)
#define ICON_GLYPH_SCALE 340   // масштаб глифа внутри иконки, 256 = 100%
#define MIN_TOP  30     // отступ сверху, если сетка не помещается и прокручивается

// Эффект "рыбий глаз" как на Apple Watch: иконка у центра экрана крупнее, к краям уменьшается
// и подтягивается к центру, чтобы расстояние между иконками не росло.
#define SCALE_MAX   272   // 256 = 100%; в центре иконка чуть больше обычной
#define SCALE_MIN   140   // у краёв
#define FISH_R      270   // на таком расстоянии от центра экрана достигается минимум
#define PULL_PCT    30    // насколько сильно уменьшенные иконки подтягиваются к центру (%)
#define FISH_STEP_SCALE 16   // ступень масштаба (256 = 100%)
#define FISH_STEP_POS   4    // ступень сдвига, пикселей
#define FISH_THROTTLE_MS 80  // на ESP32 при прокрутке пересчитываем не чаще, чем раз в столько мс
// По умолчанию эффект ВЫКЛЮЧЕН (на часах он давал низкий FPS при прокрутке). Чтобы включить: -DWATCH_FISHEYE_ON (например, в CMake порта).

static lv_obj_t *launcher;

// Масштаб и сдвиг каждой иконки зависят от расстояния до центра экрана.
// Стиль меняем только если значение реально изменилось, иначе каждый кадр прокрутки перерисовывал бы всё зря.
__attribute__((unused)) static void fisheye_update(lv_obj_t *cont)
{
    uint32_t n = lv_obj_get_child_count(cont);
    for (uint32_t k = 0; k < n; k++) {
        lv_obj_t *c = lv_obj_get_child(cont, k);
        if (lv_obj_check_type(c, &lv_label_class)) continue;   // подписи остаются на месте
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

        // Ступени вместо плавного значения: иначе масштаб меняется на каждый пиксель прокрутки,
        // и LVGL заново рисует иконки через промежуточный слой. Со ступенями большинство кадров
        // ничего не перерисовывает.
        s  = (s  / FISH_STEP_SCALE) * FISH_STEP_SCALE;
        tx = (tx / FISH_STEP_POS)   * FISH_STEP_POS;
        ty = (ty / FISH_STEP_POS)   * FISH_STEP_POS;

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
#ifdef WATCH_FISHEYE_ON
#ifdef ESP_PLATFORM
    // На часах эффект тяжёлый: при прокрутке обновляем не каждый кадр, а по таймеру; в конце прокрутки - обязательно.
    static uint32_t last;
    if (lv_event_get_code(e) == LV_EVENT_SCROLL) {
        if (lv_tick_elaps(last) < FISH_THROTTLE_MS) return;
        last = lv_tick_get();
    }
#endif
    fisheye_update(lv_event_get_target_obj(e));
#else
    (void)e;
#endif
}

static void app_cb(lv_event_t *e)
{
    const app_info_t *a = lv_event_get_user_data(e);
    ws_set_accent(a->color);                 // цвет приложения: заголовок экрана, ползунки, шапка таблиц
    if (a->builtin) a->builtin(launcher);    // системное приложение (Install)
    else            app_runtime_launch(a, launcher);
}

static void build_grid(void);

void watch_launcher_create(void)
{
    printf("LTRACE 0: start\n");
    launcher = lv_screen_active();
    lv_obj_set_style_bg_color(launcher, lv_color_black(), 0);
    lv_obj_set_style_pad_all(launcher, 0, 0);
    lv_obj_remove_flag(launcher, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(launcher, WATCH_FONT_BODY, 0);   // шрифт с кириллицей

    sys_ui_init();
    printf("LTRACE 1 ok: sys_ui_init\n");
    statusbar_init();            // общая строка состояния поверх всего: время, таймер, Wi-Fi, заряд
    printf("LTRACE 2 ok: statusbar_init\n");
    settings_init(WATCH_ROOT "data");        // часовой пояс, Wi-Fi, ключ API, уведомления
    printf("LTRACE 3 ok: settings_init\n");
    auth_init(WATCH_ROOT "data");
    printf("LTRACE 4 ok: auth_init\n");
    paired_init(WATCH_ROOT "data");          // сопряжённые телефоны (вход без кода по токену)
    storage_init(WATCH_ROOT "data");         // хранилище приложений; папка data/ приложениям недоступна
    printf("LTRACE 5 ok: storage_init\n");
    power_init(WATCH_ROOT "data");
    printf("LTRACE 6 ok: power_init\n");
    music_init(WATCH_ROOT "data");           // музыка: data/music/*.wav
    printf("LTRACE 7 ok: music_init\n");
    recorder_init(WATCH_ROOT "data");        // записи: data/rec
    printf("LTRACE 8 ok: recorder_init\n");
    photos_init(WATCH_ROOT "data/photos");   // фото с телефона (Gallery)
    printf("LTRACE 9 ok: photos_init\n");
    app_manager_scan(WATCH_ROOT "apps");   // приложения берутся из папки apps/ (manifest.json у каждого)
    printf("LTRACE 10 ok: app_manager_scan\n");
    build_grid();
    printf("LTRACE 11 ok: build_grid\n");
    lock_screen_init();         // если задан PIN, стартуем заблокированными
    printf("LTRACE 12 ok: lock_screen_init\n");
    buttons_init(launcher);     // кнопки корпуса
    printf("LTRACE 13 ok: buttons_init\n");
    sync_net_notify_apply();    // если включены уведомления или есть сопряжённые телефоны - начать слушать порт
    printf("LTRACE 14 ok: sync_net_notify_apply\n");
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
    lv_obj_add_event_cb(cont, scroll_cb, LV_EVENT_SCROLL_END, NULL);

    // Сколько рядов получится: 3 иконки, 4, 3, 4, ...
    int rows = 0;
    for (size_t left = n; left > 0; rows++) {
        size_t cols = (rows % 2 == 0) ? 3 : 4;
        left = left > cols ? left - cols : 0;
    }
    int grid_h = (rows - 1) * PITCH_Y + ICON + LABEL_GAP + LABEL_H;
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

            // Подпись под иконкой. Это соседний с кнопкой объект, а не её потомок: потомка, вылезающего за границу кнопки,
            // LVGL обрезает (OVERFLOW_VISIBLE растягивает область лишь на ext_draw_size, а у кнопки он 0), и подпись пропадала.
            lv_obj_t *cap = lv_label_create(cont);
            lv_label_set_long_mode(cap, LV_LABEL_LONG_DOT);
            lv_obj_set_width(cap, LABEL_W);
            lv_label_set_text(cap, app_manager_get(i)->name);
            lv_obj_set_style_text_font(cap, WATCH_FONT_SMALL, 0);
            lv_obj_set_style_text_color(cap, lv_color_white(), 0);
            lv_obj_set_style_text_align(cap, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_pos(cap, x0 + c * PITCH_X + (ICON - LABEL_W) / 2, y + ICON + LABEL_GAP);
            lv_obj_add_flag(cap, LV_OBJ_FLAG_CLICKABLE);              // нажатие на подпись открывает приложение, как и на иконку
            lv_obj_add_event_cb(cap, app_cb, LV_EVENT_CLICKED, (void *)app_manager_get(i));

            const lv_image_dsc_t *img = icon_image(app_manager_get(i)->icon);
            if (img) {   // готовая круглая иконка (assets/icons): кружок и глиф уже внутри картинки
                lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
                lv_obj_set_style_border_width(btn, 0, 0);
                lv_obj_t *im = lv_image_create(btn);
                lv_image_set_src(im, img);
                lv_obj_center(im);
                continue;
            }
            lv_obj_t *l = lv_label_create(btn);
            lv_label_set_text(l, app_icon_symbol(app_manager_get(i)->icon));
            lv_obj_set_style_text_color(l, lv_color_white(), 0);
            lv_obj_set_style_text_font(l, WATCH_FONT_LARGE, 0);
            lv_obj_set_style_transform_pivot_x(l, lv_pct(50), 0);   // глиф крупнее, чем шрифт 28: масштаб вокруг центра
            lv_obj_set_style_transform_pivot_y(l, lv_pct(50), 0);
            lv_obj_set_style_transform_scale(l, ICON_GLYPH_SCALE, 0);
            lv_obj_center(l);
        }
        row++;
    }

    lv_obj_update_layout(cont);
#ifdef WATCH_FISHEYE_ON
    fisheye_update(cont);
#endif
}
