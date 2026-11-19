// smoke.c - автоматическая проверка на виртуальном экране (окно и SDL не нужны).
// Запускает каждое приложение из apps/ (и системные), случайно нажимает кнопки, вводит текст
// в поля и смотрит, что ядро не падает. Собирается с AddressSanitizer/UBSan: ловит утечки и выходы за границы.
//   cmake --build build --target watch_smoke && ./bin/watch_smoke
#include "watch/lvgl_inc.h"
#include "watch/app_manager.h"
#include "watch/app_runtime.h"
#include "watch/sys_ui.h"
#include "watch/storage.h"
#include "watch/power.h"
#include "watch/tables.h"
#include "watch/vars.h"
#include "watch/syscalls.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void watch_launcher_create(void);

static uint32_t g_ms;
static uint32_t tick_cb(void) { return g_ms; }
static void flush_cb(lv_display_t *d, const lv_area_t *a, uint8_t *px) { LV_UNUSED(a); LV_UNUSED(px); lv_display_flush_ready(d); }

static void pump(int n)
{
    for (int i = 0; i < n; i++) { g_ms += 20; lv_timer_handler(); }
}

// Виртуальная «мышь»: настоящие нажатия проходят через индев, как в симуляторе и на сенсоре часов.
static lv_point_t g_pt;
static lv_indev_state_t g_state = LV_INDEV_STATE_RELEASED;
static void ptr_read(lv_indev_t *i, lv_indev_data_t *dd) { LV_UNUSED(i); dd->point = g_pt; dd->state = g_state; }
static void tap(int x, int y)
{
    g_pt.x = x; g_pt.y = y; g_state = LV_INDEV_STATE_PRESSED; pump(4);
    g_state = LV_INDEV_STATE_RELEASED; pump(4);
}

static unsigned rng = 12345;
static unsigned rnd(void) { rng = rng * 1103515245u + 12345u; return (rng >> 16) & 0x7fff; }

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

static int g_fail;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); g_fail++; } } while (0)

static void unit_tests(void)
{
    // --- таблицы
    tables_clear();
    CHECK(tables_set_json("t", "{\"rows\":[[\"a\",\"b\"],[1,2.5]]}"), "tables_set_json");
    const wtable_t *t = tables_get("t");
    CHECK(t && t->rows == 2 && t->cols == 2 && strcmp(t->cell[1][1], "2.5") == 0, "table content");
    CHECK(!tables_set_json("t", "not json"), "bad json rejected");
    CHECK(!tables_set_json("bad name!", "[[1]]"), "bad table name rejected");
    // длинная кириллица обрезается по границе символа
    char longcell[200] = "";
    for (int i = 0; i < 40; i++) strcat(longcell, "я");
    const char *cells[] = { longcell };
    CHECK(tables_set_cells("u", 1, 1, cells), "set_cells");
    const char *cc = tables_get("u")->cell[0][0];
    CHECK(strlen(cc) < TBL_CELL && strlen(cc) % 2 == 0, "utf8 cut");
    for (int i = 0; i < 10; i++) { char nm[8]; snprintf(nm, sizeof nm, "t%d", i); tables_set_cells(nm, 1, 1, cells); }
    CHECK(tables_get("t9") == NULL, "table limit");

    // --- хранилище
    char v[STORE_VAL];
    CHECK(storage_set("app1", "k", "значение"), "store set");
    CHECK(storage_get("app1", "k", v, sizeof v) && strcmp(v, "значение") == 0, "store get");
    CHECK(!storage_get("app2", "k", v, sizeof v), "store isolated between apps");
    CHECK(!storage_set("../evil", "k", "x"), "bad app id rejected");
    CHECK(!storage_set("app1", "bad key", "x"), "bad key rejected");
    CHECK(storage_remove("app1", "k") && !storage_get("app1", "k", v, sizeof v), "store remove");
    char key[8];
    int stored = 0;
    for (int i = 0; i < STORE_MAX_KEYS + 5; i++) { snprintf(key, sizeof key, "k%d", i); if (storage_set("app3", key, "1")) stored++; }
    CHECK(stored == STORE_MAX_KEYS, "key limit");
}


static lv_obj_t *find_button(const char *needle)
{
    nfound = 0; collect(lv_screen_active(), &lv_button_class);
    for (int k = 0; k < nfound; k++) {
        lv_obj_t *lab = lv_obj_get_child(found[k], 0);
        if (lab && lv_obj_check_type(lab, &lv_label_class) && strstr(lv_label_get_text(lab), needle)) return found[k];
    }
    return NULL;
}

static bool click_text(const char *needle)
{
    lv_obj_t *b = find_button(needle);
    if (!b) return false;
    lv_obj_send_event(b, LV_EVENT_CLICKED, NULL);
    pump(30);
    return true;
}

static void run_json(const char *json)
{
    cJSON *a = cJSON_Parse(json);
    sys_run_actions(app_runtime_current(), a);
    pump(30);
    cJSON_Delete(a);
}

// Сквозная проверка приложения Tools: ползунок, индикатор, список, таблицы, хранилище.
static void tools_flow(lv_obj_t *launcher)
{
    const app_info_t *a = NULL;
    for (size_t i = 0; i < app_manager_count(); i++)
        if (strcmp(app_manager_get(i)->name, "Tools") == 0) a = app_manager_get(i);
    CHECK(a != NULL, "Tools app present");
    if (!a) return;
    app_runtime_launch(a, launcher);
    pump(30);

    nfound = 0; collect(lv_screen_active(), &lv_slider_class);
    CHECK(nfound == 1, "slider built");
    if (nfound) {
        lv_slider_set_value(found[0], 70, LV_ANIM_OFF);
        lv_obj_send_event(found[0], LV_EVENT_VALUE_CHANGED, NULL);
        CHECK(strcmp(vars_get("vol"), "70") == 0, "slider writes variable");
    }
    nfound = 0; collect(lv_screen_active(), &lv_bar_class);
    CHECK(nfound == 1 && lv_bar_get_value(found[0]) == 70, "bar follows variable");

    CHECK(click_text("Planets table"), "list item clickable");
    CHECK(strcmp(vars_get("item"), "Planets table") == 0, "list sets variable");

    run_json("[{\"do\":\"ai.ask_table\",\"q\":\"Planets\",\"out\":\"result\"},{\"do\":\"goto\",\"screen\":\"table\"}]");
    nfound = 0; collect(lv_screen_active(), &lv_table_class);
    CHECK(nfound == 1, "table from ai.ask_table built");
    if (nfound) CHECK(strcmp(lv_table_get_cell_value(found[0], 1, 1), "(stub)") == 0, "table cell value");

    run_json("[{\"do\":\"goto\",\"screen\":\"static\"}]");
    nfound = 0; collect(lv_screen_active(), &lv_table_class);
    CHECK(nfound == 1, "static table built");
    if (nfound) CHECK(strcmp(lv_table_get_cell_value(found[0], 2, 1), "2") == 0, "static table number cell");

    run_json("[{\"do\":\"goto\",\"screen\":\"note\"}]");
    nfound = 0; collect(lv_screen_active(), &lv_textarea_class);
    CHECK(nfound == 1, "note input built");
    if (nfound) lv_textarea_set_text(found[0], "Привет");
    CHECK(click_text("Save"), "Save clickable");
    char v[STORE_VAL];
    CHECK(storage_get("tools", "note", v, sizeof v) && strcmp(v, "Привет") == 0, "store.set persisted");
    run_json("[{\"do\":\"set\",\"var\":\"note\",\"value\":\"\"}]");
    CHECK(click_text("Load"), "Load clickable");
    CHECK(strcmp(vars_get("note"), "Привет") == 0, "store.get loads value");
    CHECK(click_text("Delete"), "Delete clickable");
    CHECK(!storage_get("tools", "note", v, sizeof v), "store.remove deleted");
    run_json("[{\"do\":\"store.get\",\"key\":\"note\",\"out\":\"note\",\"default\":\"none\"}]");
    CHECK(strcmp(vars_get("note"), "none") == 0, "store.get default");
    printf("tools flow done\n");

    // выйти из приложения
    for (int k = 0; k < 8 && lv_screen_active() != launcher; k++) {
        nfound = 0; collect(lv_screen_active(), &lv_button_class);
        if (nfound) lv_obj_send_event(found[0], LV_EVENT_CLICKED, NULL);
        pump(25);
    }
}

// Клавиатура: открывается при фокусе, закрывается нажатием вне неё и поля, снова открывается по нажатию на поле.
static void keyboard_flow(lv_obj_t *launcher)
{
    const app_info_t *a = NULL;
    for (size_t i = 0; i < app_manager_count(); i++)
        if (strcmp(app_manager_get(i)->name, "Hello") == 0) a = app_manager_get(i);
    CHECK(a != NULL, "Hello app present");
    if (!a) return;
    app_runtime_launch(a, launcher);
    pump(30);

    nfound = 0; collect(lv_screen_active(), &lv_textarea_class);
    CHECK(nfound >= 1, "input present");
    if (!nfound) return;
    lv_obj_t *ta = found[0];

    CHECK(!sys_keyboard_visible(), "keyboard hidden at start");
    lv_obj_send_event(ta, LV_EVENT_FOCUSED, NULL);
    CHECK(sys_keyboard_visible(), "keyboard shown on focus");

    sys_keyboard_press_outside(ta);                       // нажали на само поле: остаётся
    CHECK(sys_keyboard_visible(), "tap on the input keeps keyboard");
    sys_keyboard_press_outside(lv_screen_active());       // нажали на пустое место экрана: закрылась
    CHECK(!sys_keyboard_visible(), "tap outside closes keyboard");

    lv_obj_send_event(ta, LV_EVENT_CLICKED, NULL);        // поле уже в фокусе, нажатие снова открывает клавиатуру
    CHECK(sys_keyboard_visible(), "tap on focused input reopens keyboard");

    // то же через настоящие нажатия (координаты экрана 410x502: поле сверху, клавиатура внизу, пустое место между ними)
    CHECK(sys_keyboard_visible(), "keyboard open before real taps");
    tap(200, 400);
    CHECK(sys_keyboard_visible(), "real tap on keyboard keeps it open");
    tap(200, 122);
    CHECK(sys_keyboard_visible(), "real tap on input keeps keyboard open");
    tap(200, 245);
    CHECK(!sys_keyboard_visible(), "real tap on empty area closes keyboard");
    tap(200, 122);
    CHECK(sys_keyboard_visible(), "real tap on input opens keyboard again");

    sys_keyboard_hide();
    for (int k = 0; k < 8 && lv_screen_active() != launcher; k++) {
        nfound = 0; collect(lv_screen_active(), &lv_button_class);
        if (nfound) lv_obj_send_event(found[0], LV_EVENT_CLICKED, NULL);
        pump(25);
    }
    printf("keyboard flow done\n");
}

static int g_last_pct = -1;
static void backend_cb(int pct) { g_last_pct = pct; }

// Затемнение и выключение экрана; касание выключенного экрана только будит и ничего не нажимает.
static void power_flow(lv_obj_t *launcher)
{
    power_set_backend(backend_cb);
    power_set_settings(2, 4, 80);
    int dim, off, br;
    power_get_settings(&dim, &off, &br);
    CHECK(dim == 2 && off == 4 && br == 80, "power settings stored");
    power_set_settings(10, 5, 50);
    power_get_settings(&dim, &off, &br);
    CHECK(off > dim, "off is later than dim");
    power_set_settings(2, 4, 80);

    lv_display_trigger_activity(NULL);
    pump(20);
    CHECK(!power_dimmed() && !power_screen_off() && g_last_pct == 80, "screen on at start");
    pump(150);                      // 3 с бездействия (шаг pump = 20 мс)
    CHECK(power_dimmed() && !power_screen_off(), "dimmed after dim timeout");
    CHECK(g_last_pct < 80 && g_last_pct > 0, "brightness lowered when dimmed");
    pump(150);                      // ещё 3 с
    CHECK(power_screen_off() && g_last_pct == 0, "screen off after off timeout");

    // нажатие на то место, где в лаунчере иконка: экран просыпается, приложение не запускается
    CHECK(app_runtime_current() == NULL, "no app before wake tap");
    tap(205, 251);
    CHECK(!power_screen_off() && !power_dimmed() && g_last_pct == 80, "tap wakes the screen");
    CHECK(app_runtime_current() == NULL && lv_screen_active() == launcher, "wake tap is swallowed");

    // обычное нажатие по иконке после пробуждения работает
    // (после затемнения, когда экран ещё не выключен, оно тоже проходит и будит)
    pump(150);
    CHECK(power_dimmed(), "dimmed again");
    power_wake();
    CHECK(!power_dimmed() && g_last_pct == 80, "wake restores brightness");

    power_preview_brightness(30);
    CHECK(g_last_pct == 30, "preview applies brightness");

    power_set_settings(0, 0, 70);   // никогда не гасить на время остальных проверок
    power_wake();
}

static void name_test(void)
{
    // имя из манифеста длиннее лимита обрезается по границе символа
    for (size_t i = 0; i < app_manager_count(); i++) {
        const char *n = app_manager_get(i)->name;
        size_t len = strlen(n);
        int ok = 1;
        for (size_t k = 0; k < len; k++) {
            unsigned char c = (unsigned char)n[k];
            size_t need = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 0;
            if (need == 0 || k + need > len) { ok = 0; break; }
            for (size_t j = 1; j < need; j++) if (((unsigned char)n[k + j] & 0xC0) != 0x80) ok = 0;
            k += need - 1;
        }
        CHECK(ok, "app name is valid UTF-8");
    }
}

int main(void)
{
    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *d = lv_display_create(410, 502);
    static uint8_t buf[410 * 60 * 4];
    lv_display_set_buffers(d, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush_cb);
    lv_indev_t *ptr = lv_indev_create();
    lv_indev_set_type(ptr, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(ptr, ptr_read);

    storage_init("data");
    unit_tests();

    watch_launcher_create();
    power_set_settings(0, 0, 70);    // экран не гасим, пока проверяем остальное (гашение проверяет power_flow)
    pump(30);
    lv_obj_t *launcher = lv_screen_active();
    size_t napps = app_manager_count();
    printf("apps: %u\n", (unsigned)napps);
    CHECK(napps >= 3, "apps found");

    tools_flow(launcher);
    keyboard_flow(launcher);
    power_flow(launcher);
    name_test();

    int clicks = 0;
    for (size_t i = 0; i < napps; i++) {
        const app_info_t *a = app_manager_get(i);
        if (!a) continue;
        printf("app %s: ", a->name);
        int app_clicks = 0;
        for (int step = 0; step < 80; step++) {
            if (!app_runtime_current() && lv_screen_active() == launcher) {
                if (step > 0 && !a->builtin) { /* приложение закрылось: открываем снова */ }
                if (a->builtin) a->builtin(launcher); else app_runtime_launch(a, launcher);
                pump(30);
            }
            lv_obj_t *scr = lv_screen_active();
            // вводим текст во все поля
            nfound = 0; collect(scr, &lv_textarea_class);
            for (int k = 0; k < nfound; k++) lv_textarea_set_text(found[k], (rnd() & 1) ? "Привет, world 123" : "");
            // ползунки
            nfound = 0; collect(scr, &lv_slider_class);
            for (int k = 0; k < nfound; k++) { lv_slider_set_value(found[k], (int)(rnd() % 100), LV_ANIM_OFF); lv_obj_send_event(found[k], LV_EVENT_VALUE_CHANGED, NULL); }
            // кнопки (включая «назад»)
            nfound = 0; collect(scr, &lv_button_class);
            if (nfound == 0) { pump(5); continue; }
            lv_obj_t *b = found[rnd() % (unsigned)nfound];
            lv_obj_send_event(b, LV_EVENT_CLICKED, NULL);
            clicks++; app_clicks++;
            pump(25);
        }
        // вернуться в лаунчер, если приложение ещё открыто
        for (int k = 0; k < 6 && lv_screen_active() != launcher; k++) {
            nfound = 0; collect(lv_screen_active(), &lv_button_class);
            if (nfound) lv_obj_send_event(found[0], LV_EVENT_CLICKED, NULL);
            pump(25);
        }
        printf("%d clicks\n", app_clicks);
    }
    printf("total clicks: %d\n", clicks);
    printf(g_fail ? "SMOKE FAILED (%d)\n" : "SMOKE OK\n", g_fail);
    return g_fail ? 1 : 0;
}
