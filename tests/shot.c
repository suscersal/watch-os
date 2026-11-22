// shot.c - рисует экраны в PNG без окна: можно посмотреть вид интерфейса без SDL и без часов.
//   cmake --build build --target watch_shot
//   ./bin/watch_shot out.png launcher                      главный экран
//   ./bin/watch_shot out.png app=Tools click="Planets table"   открыть приложение и нажать кнопку
//   ./bin/watch_shot out.png app=Notes focus                поле ввода с клавиатурой
// Шаги выполняются по порядку; снимок делается после последнего.
//   launcher          главный экран (после открытых приложений - вернуться)
//   app=<имя>         запустить приложение по имени (как в лаунчере)
//   click=<текст>     нажать кнопку, в подписи которой есть этот текст
//   focus             сфокусировать первое поле ввода (покажет клавиатуру)
//   scroll=<dy>       прокрутить сетку лаунчера на dy пикселей
//   tapout            нажать вне клавиатуры и поля (клавиатура должна закрыться)
//   face              показать циферблат
//   time=<epoch>,<tz> задать время (epoch UTC, пояс в минутах), чтобы снимок не зависел от часов компьютера
//   msg=<app>|<title>|<text>   добавить уведомление (как будто пришло с телефона)
//   swipe             свайп вправо (выход из приложения; на лаунчере ничего не делает)
//   tap=<x>,<y>       настоящее нажатие в точку экрана (410x502)
//   wait=<мс>         подождать (время виртуальное): идёт запись, воспроизведение, таймеры
#include "watch/lvgl_inc.h"
#include "watch/app_manager.h"
#include "watch/app_runtime.h"
#include "watch/clock.h"
#include "watch/messages.h"
#include "watch/sys_ui.h"
#include "watch/watchface.h"
#include "watch/ws_style.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void watch_launcher_create(void);

#define W 410
#define H 502

static uint32_t g_ms;
static uint32_t tick_cb(void) { return g_ms; }
static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px) { LV_UNUSED(a); LV_UNUSED(px); lv_display_flush_ready(d); }
static void pump(int n) { for (int i = 0; i < n; i++) { g_ms += 20; lv_timer_handler(); } }

// Виртуальный палец: настоящие нажатия и свайпы проходят через индев, как на сенсоре часов.
static lv_point_t g_pt;
static lv_indev_state_t g_state = LV_INDEV_STATE_RELEASED;
static void ptr_read(lv_indev_t *i, lv_indev_data_t *dd) { LV_UNUSED(i); dd->point = g_pt; dd->state = g_state; }
static void tap(int x, int y)
{
    g_pt.x = x; g_pt.y = y; g_state = LV_INDEV_STATE_PRESSED; pump(4);
    g_state = LV_INDEV_STATE_RELEASED; pump(4);
}
static void swipe_right(void)
{
    g_pt.x = 4; g_pt.y = 250; g_state = LV_INDEV_STATE_PRESSED; pump(3);   // от левого края, как свайп "назад"
    for (int x = 70; x <= 330; x += 52) { g_pt.x = x; pump(2); }
    g_state = LV_INDEV_STATE_RELEASED; pump(40);
}

// ---- минимальный PNG без сжатия (stored-блоки deflate), чтобы не зависеть от zlib ----
static uint32_t crc_tab[256];
static void crc_init(void)
{
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_tab[n] = c;
    }
}
static uint32_t crc_upd(uint32_t c, const uint8_t *d, size_t n)
{
    for (size_t i = 0; i < n; i++) c = crc_tab[(c ^ d[i]) & 0xFF] ^ (c >> 8);
    return c;
}
static void be32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static void chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len)
{
    uint8_t b[4];
    be32(b, len); fwrite(b, 1, 4, f);
    fwrite(type, 1, 4, f);
    if (len) fwrite(data, 1, len, f);
    uint32_t c = crc_upd(0xFFFFFFFFu, (const uint8_t *)type, 4);
    if (len) c = crc_upd(c, data, len);
    be32(b, c ^ 0xFFFFFFFFu); fwrite(b, 1, 4, f);
}
static int write_png(const char *path, const uint8_t *xrgb)
{
    crc_init();
    size_t rawn = (size_t)H * (1 + (size_t)W * 3);
    uint8_t *raw = malloc(rawn);
    if (!raw) return 1;
    for (int y = 0; y < H; y++) {
        uint8_t *row = raw + (size_t)y * (1 + W * 3);
        row[0] = 0;
        for (int x = 0; x < W; x++) {
            const uint8_t *s = xrgb + ((size_t)y * W + x) * 4;   // XRGB8888 в памяти: B, G, R, X
            row[1 + x * 3 + 0] = s[2]; row[1 + x * 3 + 1] = s[1]; row[1 + x * 3 + 2] = s[0];
        }
    }
    size_t nblocks = (rawn + 65534) / 65535;
    size_t zn = 2 + rawn + nblocks * 5 + 4;
    uint8_t *z = malloc(zn);
    if (!z) { free(raw); return 1; }
    size_t o = 0;
    z[o++] = 0x78; z[o++] = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t pos = 0; pos < rawn; ) {
        size_t n = rawn - pos > 65535 ? 65535 : rawn - pos;
        z[o++] = (pos + n >= rawn) ? 1 : 0;
        z[o++] = n & 0xFF; z[o++] = n >> 8; z[o++] = ~n & 0xFF; z[o++] = (~n >> 8) & 0xFF;
        memcpy(z + o, raw + pos, n); o += n;
        for (size_t i = 0; i < n; i++) { a = (a + raw[pos + i]) % 65521; b = (b + a) % 65521; }
        pos += n;
    }
    be32(z + o, (b << 16) | a); o += 4;
    FILE *f = fopen(path, "wb");
    if (!f) { free(raw); free(z); return 1; }
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13];
    be32(ihdr, W); be32(ihdr + 4, H); ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = ihdr[11] = ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, (uint32_t)o);
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw); free(z);
    return 0;
}

// ---- поиск объектов ----
#define MAXOBJ 400
static lv_obj_t *found[MAXOBJ];
static int nfound;
static void collect(lv_obj_t *o, const lv_obj_class_t *cls)
{
    if (nfound >= MAXOBJ) return;
    if (lv_obj_check_type(o, cls) && !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) found[nfound++] = o;
    uint32_t n = lv_obj_get_child_count(o);
    for (uint32_t i = 0; i < n; i++) collect(lv_obj_get_child(o, i), cls);
}
static lv_obj_t *find_button(lv_obj_t *root, const char *needle)
{
    nfound = 0; collect(root, &lv_button_class);
    for (int k = 0; k < nfound; k++) {
        lv_obj_t *lab = lv_obj_get_child(found[k], 0);
        if (lab && lv_obj_check_type(lab, &lv_label_class) && strstr(lv_label_get_text(lab), needle)) return found[k];
    }
    return NULL;
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: %s out.png step...\n", argv[0]); return 2; }
    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *d = lv_display_create(W, H);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_XRGB8888);
    static uint8_t buf[W * H * 4];
    lv_display_set_buffers(d, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(d, flush_cb);
    lv_indev_t *ptr = lv_indev_create();
    lv_indev_set_type(ptr, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(ptr, ptr_read);

    watch_launcher_create();
    pump(40);
    lv_obj_t *launcher = lv_screen_active();

    for (int i = 2; i < argc; i++) {
        const char *s = argv[i];
        if (strcmp(s, "launcher") == 0) {
            for (int k = 0; k < 8 && lv_screen_active() != launcher; k++) {
                swipe_right();
                if (lv_screen_active() == launcher) break;
                lv_obj_t *back = find_button(lv_screen_active(), LV_SYMBOL_LEFT);   // вложенный экран: круглая кнопка "назад"
                if (back) { lv_obj_send_event(back, LV_EVENT_CLICKED, NULL); pump(30); }
            }
        } else if (strncmp(s, "app=", 4) == 0) {
            const app_info_t *a = NULL;
            for (size_t k = 0; k < app_manager_count(); k++)
                if (strcmp(app_manager_get(k)->name, s + 4) == 0) a = app_manager_get(k);
            if (!a) { fprintf(stderr, "no app %s\n", s + 4); return 1; }
            ws_set_accent(a->color);
            if (a->builtin) a->builtin(launcher); else app_runtime_launch(a, launcher);
            pump(40);
        } else if (strncmp(s, "click=", 6) == 0) {
            lv_obj_t *b = find_button(lv_screen_active(), s + 6);
            if (!b) { fprintf(stderr, "no button %s\n", s + 6); return 1; }
            lv_obj_send_event(b, LV_EVENT_CLICKED, NULL);
            pump(40);
        } else if (strcmp(s, "focus") == 0) {
            nfound = 0; collect(lv_screen_active(), &lv_textarea_class);
            if (!nfound) { fprintf(stderr, "no textarea\n"); return 1; }
            lv_obj_send_event(found[0], LV_EVENT_FOCUSED, NULL);
            pump(40);
        } else if (strncmp(s, "scroll=", 7) == 0) {
            lv_obj_t *cont = lv_obj_get_child(launcher, 0);
            lv_obj_scroll_by(cont, 0, -atoi(s + 7), LV_ANIM_OFF);
            pump(20);
        } else if (strcmp(s, "face") == 0) {
            watchface_show(launcher);
            pump(30);
        } else if (strncmp(s, "time=", 5) == 0) {
            long long e = 0; int tz = 0;
            if (sscanf(s + 5, "%lld,%d", &e, &tz) != 2) { fprintf(stderr, "bad time step\n"); return 2; }
            clock_set(e, tz);
            pump(30);
        } else if (strncmp(s, "msg=", 4) == 0) {
            char tmp[400];
            snprintf(tmp, sizeof tmp, "%s", s + 4);
            char *t = strchr(tmp, '|'), *x = t ? strchr(t + 1, '|') : NULL;
            if (!t || !x) { fprintf(stderr, "bad msg step\n"); return 2; }
            *t = *x = '\0';
            messages_add((uint32_t)i, 0, tmp, t + 1, x + 1);
            pump(30);
        } else if (strcmp(s, "swipe") == 0) {
            swipe_right();
        } else if (strncmp(s, "tap=", 4) == 0) {
            int x, y;
            if (sscanf(s + 4, "%d,%d", &x, &y) != 2) { fprintf(stderr, "bad tap\n"); return 2; }
            tap(x, y);
        } else if (strncmp(s, "wait=", 5) == 0) {
            pump(atoi(s + 5) / 20);
        } else if (strcmp(s, "tapout") == 0) {
            sys_keyboard_press_outside(lv_screen_active());
            pump(20);
        } else { fprintf(stderr, "unknown step %s\n", s); return 2; }
    }
    pump(20);
    return write_png(argv[1], buf);
}
