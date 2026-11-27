// gallery_sync_test.c - проверка протокола синхронизации, TCP-приёма и галереи без окна.
// Рисует в буфер в памяти и проверяет цвета пикселей, поэтому декодирование JPEG проверяется по-настоящему.
// Запуск: см. комментарий в CMakeLists.txt (цель watch_sync_test) или docs/sync-protocol.md.
#include "watch/app_manager.h"
#include "watch/auth.h"
#include "watch/clock.h"
#include "watch/crc32.h"
#include "watch/gallery_app.h"
#include "watch/lock_screen.h"
#include "watch/messages.h"
#include "watch/settings.h"
#include "watch/lvgl_inc.h"
#include "watch/photos.h"
#include "fake_ota.h"
#include "watch/fs.h"
#include "watch/sync_app.h"
#include "watch/sync_net.h"
#include "watch/sync_proto.h"
#include "watch/sync_rx.h"
#include "watch/sys_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

void watch_launcher_create(void);

static int fails;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s\n", msg); fails++; } else printf("ok:   %s\n", msg); } while (0)

// ---------- экран в памяти ----------
#define W 410
#define H 502
static uint8_t fb[W * H * 4];
static int fb_bpp;

static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    int w = lv_area_get_width(a);
    for (int y = a->y1; y <= a->y2; y++) memcpy(fb + ((size_t)y * W + a->x1) * fb_bpp, px + (size_t)(y - a->y1) * w * fb_bpp, (size_t)w * fb_bpp);
    lv_display_flush_ready(d);
}

static void run(int ms) { for (int i = 0; i < ms; i += 5) { lv_tick_inc(5); lv_timer_handler(); } }

static void pixel(int x, int y, int *r, int *g, int *b)
{
    const uint8_t *p = fb + ((size_t)y * W + x) * fb_bpp;
    if (fb_bpp == 2) {
        unsigned v = p[0] | (p[1] << 8);
        *r = ((v >> 11) & 31) * 255 / 31; *g = ((v >> 5) & 63) * 255 / 63; *b = (v & 31) * 255 / 31;
    } else { *b = p[0]; *g = p[1]; *r = p[2]; }
}
static bool is_color(int x, int y, int er, int eg, int eb)
{
    int r, g, b;
    pixel(x, y, &r, &g, &b);
    int tol = 45;
    return abs(r - er) < tol && abs(g - eg) < tol && abs(b - eb) < tol;
}

// Сохраняет экран в PPM (если задана переменная SHOT_DIR), чтобы можно было посмотреть, как всё выглядит.
static void dump(const char *name)
{
    const char *dir = getenv("SHOT_DIR");
    if (!dir) return;
    char path[300];
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) { int r, g, b; pixel(x, y, &r, &g, &b); fputc(r, f); fputc(g, f); fputc(b, f); }
    fclose(f);
}

static lv_obj_t *find_label(lv_obj_t *o, const char *text)
{
    if (lv_obj_has_class(o, &lv_label_class) && strcmp(lv_label_get_text(o), text) == 0) return o;
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) { lv_obj_t *r = find_label(lv_obj_get_child(o, i), text); if (r) return r; }
    return NULL;
}
static void press(lv_obj_t *root, const char *t)
{
    lv_obj_t *l = find_label(root, t);
    if (!l) { printf("FAIL: no button '%s'\n", t); fails++; return; }
    lv_obj_send_event(lv_obj_get_parent(l), LV_EVENT_CLICKED, NULL);
    run(300);
}

// ---------- файлы ----------
static uint8_t *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) { printf("FAIL: cannot read %s\n", path); fails++; *n = 0; return NULL; }
    fseek(f, 0, SEEK_END); *n = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(*n); if (fread(b, 1, *n, f) != *n) { *n = 0; }
    fclose(f);
    return b;
}
static bool exists(const char *p) { return access(p, F_OK) == 0; }

// ---------- сессия протокола в памяти ----------
static uint8_t out[4096];
static size_t  out_n;
static void io_send(const uint8_t *d, size_t n, void *ud) { (void)ud; if (out_n + n <= sizeof out) { memcpy(out + out_n, d, n); out_n += n; } }
static int ev_count[16];
static void on_ev(sync_event_t ev, void *ud) { (void)ud; ev_count[ev]++; }

static void open_session(const char *code) { sync_io_t io = { io_send, NULL }; out_n = 0; sync_rx_open(code, &io); }

static size_t put_frame(uint8_t *buf, uint8_t type, const uint8_t *p, size_t n)
{
    buf[0] = type; buf[1] = (uint8_t)n; buf[2] = (uint8_t)(n >> 8); buf[3] = (uint8_t)(n >> 16); buf[4] = (uint8_t)(n >> 24);
    if (n) memcpy(buf + 5, p, n);
    return 5 + n;
}
// Отправить кадр; chunk - на сколько кусков произвольной длины порезать поток (проверка разбора).
static bool send_frame(uint8_t type, const uint8_t *p, size_t n, size_t chunk)
{
    uint8_t *buf = malloc(5 + n);
    size_t total = put_frame(buf, type, p, n);
    bool ok = true;
    for (size_t off = 0; off < total && ok; off += chunk) ok = sync_rx_feed(buf + off, total - off < chunk ? total - off : chunk);
    free(buf);
    return ok;
}
static bool tx(uint8_t type, const uint8_t *p, size_t n) { out_n = 0; return send_frame(type, p, n, 1 << 20); }
static int  resp_type(void) { return out_n >= 5 ? out[0] : -1; }
static int  resp_code(void) { return out_n >= 7 ? out[6] : -1; }   // ERR: код, OK: статус
static void w32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

static bool hello(const char *code)
{
    uint8_t p[1 + SYNC_CODE_LEN + 4];
    p[0] = 1; memcpy(p + 1, code, SYNC_CODE_LEN); memcpy(p + 1 + SYNC_CODE_LEN, "test", 4);
    return tx(SYNC_T_HELLO, p, sizeof p);
}

// Передача прошивки: FW_BEGIN, FW_DATA кусками, FW_END. Возвращает тип последнего ответа.
static int send_fw(const uint8_t *d, size_t n, uint32_t claimed_size, bool bad_crc, bool finish)
{
    uint8_t b[8];
    w32(b, claimed_size); w32(b + 4, crc32_update(0, d, n) ^ (bad_crc ? 1u : 0));
    tx(SYNC_T_FW_BEGIN, b, 8);
    int r = resp_type();
    if (r == SYNC_T_OK && resp_code() == SYNC_S_READY) {
        for (size_t off = 0; off < n; off += 9000) {
            size_t k = n - off < 9000 ? n - off : 9000;
            out_n = 0;
            if (!send_frame(SYNC_T_FW_DATA, d + off, k, 31)) return resp_type();   // часы закрыли соединение: ответ ERR в out
            if (!finish && off >= 9000) return r;
        }
        tx(SYNC_T_FW_END, NULL, 0);
        r = resp_type();
    }
    return r;
}

// Полная передача фото: BEGIN, DATA кусками, END. Возвращает тип последнего ответа.
static int send_photo(uint32_t id, uint32_t taken, const uint8_t *th, size_t tn, const uint8_t *fu, size_t fn, uint32_t crc_override, bool use_override)
{
    uint8_t *both = malloc(tn + fn);
    memcpy(both, th, tn); memcpy(both + tn, fu, fn);
    uint8_t b[20];
    w32(b, id); w32(b + 4, taken); w32(b + 8, (uint32_t)tn); w32(b + 12, (uint32_t)fn);
    w32(b + 16, use_override ? crc_override : crc32_update(0, both, tn + fn));
    tx(SYNC_T_PHOTO_BEGIN, b, sizeof b);
    int r = resp_type();
    if (r == SYNC_T_OK && resp_code() == SYNC_S_READY) {
        size_t off = 0, step = 777;                       // нарочно "неудобный" размер куска
        while (off < tn + fn) {
            size_t n = tn + fn - off < step ? tn + fn - off : step;
            send_frame(SYNC_T_PHOTO_DATA, both + off, n, 13);   // и кадр режется на куски по 13 байт
            off += n;
        }
        tx(SYNC_T_PHOTO_END, NULL, 0);
        r = resp_type();
    }
    free(both);
    return r;
}

static size_t mk_msg(uint8_t *o, uint32_t id, const char *app, const char *title, const char *text)
{
    size_t al = strlen(app), tl = strlen(title), xl = strlen(text);
    w32(o, id); w32(o + 4, 1700000000);
    o[8] = (uint8_t)al; o[9] = (uint8_t)tl; o[10] = (uint8_t)xl; o[11] = (uint8_t)(xl >> 8);
    memcpy(o + 12, app, al); memcpy(o + 12 + al, title, tl); memcpy(o + 12 + al + tl, text, xl);
    return 12 + al + tl + xl;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 3) { printf("usage: %s <fixtures dir> <sync_client.py>\n", argv[0]); return 2; }
    const char *fx = argv[1], *client = argv[2];

    if (access("src", F_OK) == 0 || access("CMakeLists.txt", F_OK) == 0) {   // тест стирает data/ и apps/ в текущей папке
        printf("Run this from an empty scratch directory, not from the project root.\n");
        return 2;
    }
    lv_init();
    lv_display_t *d = lv_display_create(W, H);
    fb_bpp = (int)lv_color_format_get_size(lv_display_get_color_format(d));
    static uint8_t buf[W * H * 4];
    lv_display_set_buffers(d, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush);

    if (system("rm -rf data apps")) return 2;
    watch_launcher_create();
    run(100);

    char p[256];
    size_t an, bn, tan, tbn, pn, gn, tgn;
    snprintf(p, sizeof p, "%s/full_a.jpg", fx);  uint8_t *full_a = slurp(p, &an);
    snprintf(p, sizeof p, "%s/thumb_a.jpg", fx); uint8_t *thumb_a = slurp(p, &tan);
    snprintf(p, sizeof p, "%s/full_b.jpg", fx);  uint8_t *full_b = slurp(p, &bn);
    snprintf(p, sizeof p, "%s/thumb_b.jpg", fx); uint8_t *thumb_b = slurp(p, &tbn);
    snprintf(p, sizeof p, "%s/prog.jpg", fx);    uint8_t *prog = slurp(p, &pn);
    snprintf(p, sizeof p, "%s/big.jpg", fx);     uint8_t *big = slurp(p, &gn);
    snprintf(p, sizeof p, "%s/nojfif.jpg", fx);  uint8_t *nojfif = slurp(p, &tgn);

    // ============ протокол ============
    sync_rx_set_event_cb(on_ev, NULL);
    CHECK(photos_check_jpeg(full_a, an, 410, 502, NULL, NULL), "baseline 410x502 JPEG accepted");
    CHECK(!photos_check_jpeg(prog, pn, 410, 502, NULL, NULL), "progressive JPEG rejected");
    CHECK(!photos_check_jpeg(big, gn, 410, 502, NULL, NULL), "oversized JPEG rejected");
    CHECK(!photos_check_jpeg(nojfif, tgn, 410, 502, NULL, NULL), "JPEG without JFIF header rejected (LVGL would not decode it)");
    CHECK(!photos_check_jpeg((const uint8_t *)"not a jpeg", 10, 410, 502, NULL, NULL), "garbage rejected");

    open_session("123456");
    CHECK(ev_count[SYNC_EV_CONNECTED] == 1, "connect event");
    CHECK(!tx(SYNC_T_PHOTO_LIST, NULL, 0) && resp_type() == SYNC_T_ERR && resp_code() == SYNC_E_STATE, "command before HELLO: error + close");
    open_session("123456");
    CHECK(hello("000000") && resp_type() == SYNC_T_ERR && resp_code() == SYNC_E_AUTH, "wrong code 1: AUTH error, link stays");
    CHECK(hello("111111"), "wrong code 2: still open");
    CHECK(!hello("222222") && ev_count[SYNC_EV_AUTH_FAIL] == 3, "wrong code 3: connection closed");
    open_session("123456");
    CHECK(hello("123456") && resp_type() == SYNC_T_HELLO_OK && ev_count[SYNC_EV_AUTH_OK] == 1, "correct code: HELLO_OK");
    CHECK(out_n == 5 + 9 && out[5] == SYNC_PROTO_VERSION && out[8] == PHOTOS_MAX, "HELLO_OK carries protocol, count, limit");

    uint8_t t[10] = {0};
    int64_t ep = 1700000000;                      // 2023-11-14 22:13:20 UTC
    for (int i = 0; i < 8; i++) t[i] = (uint8_t)(ep >> (8 * i));
    t[8] = (uint8_t)180; t[9] = 0;                // UTC+3
    CHECK(!clock_is_set(), "clock not set before TIME");
    CHECK(tx(SYNC_T_TIME, t, 10) && resp_type() == SYNC_T_OK && clock_is_set(), "TIME accepted");
    struct tm lt;
    CHECK(clock_local(&lt) && lt.tm_mday == 15 && lt.tm_hour == 1 && lt.tm_min == 13, "local time = 2023-11-15 01:13 (UTC+3)");
    char fmt[40];
    clock_format_epoch(1700001000, fmt, sizeof fmt);
    CHECK(strcmp(fmt, "2023-11-15 01:30") == 0, "clock_format_epoch uses the synced zone");

    // ---- прошивка по воздуху: только по одноразовому коду, в неактивный слот, с проверками ----
    {
        size_t fn = 100000;
        uint8_t *fw = malloc(fn);
        for (size_t i = 0; i < fn; i++) fw[i] = (uint8_t)(i * 31 + 7);
        fw[0] = 0xE9;
        CHECK(send_fw(fw, fn, (uint32_t)fn, false, true) == SYNC_T_ERR && resp_code() == SYNC_E_UNSUPPORTED, "firmware: no platform (PC) -> unsupported");
        ota_set_hw(&FAKE_OTA);
        CHECK(send_fw(fw, fn, (uint32_t)fn, false, true) == SYNC_T_OK && resp_code() == 0, "firmware: image accepted");
        CHECK(fs_file_size("fw_received.bin") == (long)fn && !exists("fw_received.bin.part"), "firmware: all bytes in the slot, no partial file");
        CHECK(fo_restarts == 1 && ev_count[SYNC_EV_FIRMWARE] == 1, "firmware: restart requested once, event emitted");
        CHECK(send_fw(fw, fn, (uint32_t)fn, true, true) == SYNC_T_ERR && resp_code() == SYNC_E_CRC && fo_restarts == 1 && !exists("fw_received.bin.part"), "firmware: bad CRC rejected, no restart, slot dropped");
        CHECK(send_fw(fw, 1000, 1000, false, true) == SYNC_T_ERR && resp_code() == SYNC_E_FORMAT, "firmware: tiny file rejected (below 64 KB)");
        CHECK(send_fw(fw, fn, SYNC_FW_MAX_BYTES + 1, false, true) == SYNC_T_ERR && resp_code() == SYNC_E_TOO_BIG, "firmware: bigger than the slot rejected");
        fo_fail_begin = true;
        CHECK(send_fw(fw, fn, (uint32_t)fn, false, true) == SYNC_T_ERR && resp_code() == SYNC_E_STORAGE, "firmware: no free slot -> storage error");
        fo_fail_begin = false;
        uint8_t *junk = calloc(1, fn);                                   // не образ: первый байт не 0xE9
        open_session("123456"); CHECK(hello("123456"), "firmware: new session");
        CHECK(send_fw(junk, fn, (uint32_t)fn, false, true) == SYNC_T_ERR && resp_code() == SYNC_E_FORMAT && fo_restarts == 1, "firmware: not an image rejected on the first block, connection closed");
        open_session("123456"); CHECK(hello("123456"), "firmware: new session after rejection");
        int aborts = fo_aborts;
        send_fw(fw, fn, (uint32_t)fn, false, false);                     // оборвали посередине
        open_session("123456");                                          // новая сессия: недокачанное бросается
        CHECK(fo_aborts > aborts && !exists("fw_received.bin.part") && fo_restarts == 1, "firmware: interrupted transfer aborted, no restart");
        CHECK(hello("123456") && resp_type() == SYNC_T_HELLO_OK, "firmware: session reopened");
        free(fw); free(junk);
        ota_set_hw(NULL);
    }
    uint8_t badt[10] = {0};
    CHECK(tx(SYNC_T_TIME, badt, 10) && resp_type() == SYNC_T_ERR, "TIME with epoch 0 rejected");

    CHECK(send_photo(0xA1, 1700000000, thumb_a, tan, full_a, an, 0, false) == SYNC_T_OK && photos_count() == 1, "photo A stored (stream cut into odd pieces)");
    CHECK(ev_count[SYNC_EV_PHOTO] == 1, "photo event");
    CHECK(exists("data/photos/6553f100_000000a1.jpg") && exists("data/photos/6553f100_000000a1.t.jpg"), "files named <taken>_<id>.jpg and .t.jpg");
    uint8_t b[20];
    w32(b, 0xA1); w32(b + 4, 1700000000); w32(b + 8, (uint32_t)tan); w32(b + 12, (uint32_t)an); w32(b + 16, 0);
    CHECK(tx(SYNC_T_PHOTO_BEGIN, b, sizeof b) && resp_type() == SYNC_T_OK && resp_code() == SYNC_S_HAVE, "same id again: 'already have', no upload");
    CHECK(send_photo(0xB2, 1700001000, thumb_b, tbn, full_b, bn, 0, false) == SYNC_T_OK && photos_count() == 2, "photo B stored");
    CHECK(send_photo(0xC3, 1700002000, thumb_a, tan, full_a, an, 0xDEADBEEF, true) == SYNC_T_ERR && resp_code() == SYNC_E_CRC && !photos_exists(0xC3), "bad CRC rejected, nothing stored");
    CHECK(send_photo(0xC4, 1700002000, thumb_a, tan, prog, pn, 0, false) == SYNC_T_ERR && resp_code() == SYNC_E_FORMAT && !photos_exists(0xC4), "progressive full image rejected");
    CHECK(send_photo(0xC5, 1700002000, thumb_a, tan, big, gn, 0, false) == SYNC_T_ERR && resp_code() == SYNC_E_FORMAT, "image larger than 410x502 rejected");
    w32(b, 0xC6); w32(b + 4, 1); w32(b + 8, 100); w32(b + 12, 10 * 1024 * 1024); w32(b + 16, 0);
    CHECK(tx(SYNC_T_PHOTO_BEGIN, b, sizeof b) && resp_type() == SYNC_T_ERR && resp_code() == SYNC_E_TOO_BIG, "10 MB announcement rejected up front");
    CHECK(tx(SYNC_T_PHOTO_LIST, NULL, 0) && resp_type() == SYNC_T_LIST && out[5] == 2 && out[7] == 0xB2 && out[11] == 0xA1, "LIST: 2 ids, newest (B) first");
    CHECK(tx(SYNC_T_MESSAGE, NULL, 0) && resp_type() == SYNC_T_ERR && resp_code() == SYNC_E_BAD_FRAME, "empty MESSAGE: bad frame");
    uint8_t big_hdr[5] = { SYNC_T_PHOTO_DATA, 0xFF, 0xFF, 0xFF, 0x7F };
    out_n = 0;
    CHECK(!sync_rx_feed(big_hdr, 5) && resp_type() == SYNC_T_ERR, "absurd frame length: error + close");

    open_session("123456"); hello("123456");
    CHECK(!send_frame(SYNC_T_PHOTO_DATA, (const uint8_t *)"x", 1, 1 << 20), "DATA without BEGIN: close");
    open_session("123456"); hello("123456");
    uint8_t id_b[4]; w32(id_b, 0xDEAD);
    CHECK(tx(SYNC_T_PHOTO_DELETE, id_b, 4) && resp_type() == SYNC_T_ERR, "delete of unknown id: error");

    // блокировка PIN: приём запрещён
    CHECK(auth_set_pin("1234"), "PIN set");
    lock_screen_lock(); run(100);
    out_n = 0;
    CHECK(!send_frame(SYNC_T_PHOTO_LIST, NULL, 0, 1 << 20) && resp_type() == SYNC_T_ERR && resp_code() == SYNC_E_LOCKED, "locked watch refuses sync");
    for (const char *k = "1234"; *k; k++) { char s[2] = { *k, 0 }; press(lv_layer_sys(), s); }
    run(300);
    CHECK(!lock_screen_is_locked(), "unlocked");
    auth_remove_pin();
    sync_rx_close();

    // ============ уведомления: постоянный код, ограниченный доступ, MESSAGE ============
    {
        uint8_t m[400];
        sync_rx_set_notify_code("654321");
        open_session("123456");
        CHECK(hello("654321") && resp_type() == SYNC_T_HELLO_OK, "notify code accepted");
        CHECK(out[5 + 1] == 0 && out[5 + 2] == 0, "notify session reports 0 photos");
        size_t n = mk_msg(m, 7, "Telegram", "Anna", "See you at 7?");
        CHECK(tx(SYNC_T_MESSAGE, m, n) && resp_type() == SYNC_T_OK && messages_count() == 1, "MESSAGE stored");
        CHECK(strcmp(messages_get(0)->title, "Anna") == 0 && strcmp(messages_get(0)->app, "Telegram") == 0 && messages_unread() == 1, "message fields and unread count");
        CHECK(tx(SYNC_T_PHOTO_LIST, NULL, 0) && resp_type() == SYNC_T_ERR && resp_code() == SYNC_E_SCOPE, "notify code cannot list photos (link stays)");
        n = mk_msg(m, 7, "Telegram", "Anna", "Edited");
        CHECK(tx(SYNC_T_MESSAGE, m, n) && messages_count() == 1 && strcmp(messages_get(0)->text, "Edited") == 0, "same id replaces the message");
        n = mk_msg(m, 8, "Mail", "Привет", "Текст");
        CHECK(tx(SYNC_T_MESSAGE, m, n) && messages_count() == 2 && strcmp(messages_get(0)->title, "Привет") == 0, "utf-8 message, newest first");
        n = mk_msg(m, 9, "A", "B", "C");
        m[10] = 50;                                                  // длина текста не сходится с кадром
        CHECK(tx(SYNC_T_MESSAGE, m, n) && resp_type() == SYNC_T_ERR && resp_code() == SYNC_E_BAD_FRAME && messages_count() == 2, "inconsistent lengths rejected");
        char big[300]; memset(big, 'x', sizeof big - 1); big[sizeof big - 1] = 0;
        n = mk_msg(m, 10, "A", "B", big);
        CHECK(tx(SYNC_T_MESSAGE, m, n) && resp_type() == SYNC_T_ERR && resp_code() == SYNC_E_TOO_BIG, "oversized text rejected");
        CHECK(hello("123456") && resp_type() == SYNC_T_HELLO_OK, "one-time code still gives full access on the same server");
        sync_rx_close();

        // при блокировке PIN постоянный код пускает, полный - нет
        CHECK(auth_set_pin("1234"), "PIN set for notify test");
        lock_screen_lock(); run(100);
        open_session("123456");
        CHECK(hello("654321") && resp_type() == SYNC_T_HELLO_OK, "notify code works on a locked watch");
        n = mk_msg(m, 11, "Signal", "Bob", "hi");
        CHECK(tx(SYNC_T_MESSAGE, m, n) && messages_count() == 3, "message accepted while locked");
        open_session("123456");
        CHECK(!hello("123456") && resp_type() == SYNC_T_ERR && resp_code() == SYNC_E_LOCKED, "full code refused on a locked watch");
        for (const char *k = "1234"; *k; k++) { char sk[2] = { *k, 0 }; press(lv_layer_sys(), sk); }
        run(300);
        auth_remove_pin();
        sync_rx_set_notify_code(NULL);
        open_session("123456");
        CHECK(hello("654321") && resp_type() == SYNC_T_ERR && resp_code() == SYNC_E_AUTH, "notify code off: rejected");
        sync_rx_close();
        messages_clear();
        CHECK(messages_count() == 0 && messages_unread() == 0, "messages cleared");

        // настройки сохраняются в файл
        settings_set_notify(true);
        CHECK(strlen(settings_notify_code()) == 6, "notify code generated");
        char code1[8]; snprintf(code1, sizeof code1, "%s", settings_notify_code());
        settings_set_api_key("sk-test-123");
        settings_wifi_set("Home", "pass1234");
        settings_set_tz(false, 330);
        settings_init("data");
        char ss[34], pp[66];
        CHECK(settings_notify_on() && strcmp(settings_notify_code(), code1) == 0, "notify settings persist");
        CHECK(strcmp(settings_api_key(), "sk-test-123") == 0, "API key persists");
        CHECK(settings_wifi_get(ss, sizeof ss, pp, sizeof pp) && strcmp(ss, "Home") == 0 && strcmp(pp, "pass1234") == 0, "Wi-Fi network persists");
        CHECK(!settings_tz_auto() && settings_tz_min() == 330, "manual time zone persists");
        clock_set(1700000000, 180);                                  // телефон шлёт +3, но выбран ручной пояс +5:30
        { struct tm lt; CHECK(clock_local(&lt) && lt.tm_hour == 3 && lt.tm_min == 43, "manual zone overrides the phone's zone"); }
        clock_set_tz(true, 0);
        clock_set(1700000000, 180);
        { struct tm lt; CHECK(clock_local(&lt) && lt.tm_hour == 1 && lt.tm_min == 13 && settings_tz_min() == 180, "auto zone follows the phone and is remembered"); }
        settings_set_notify(false);
        settings_wifi_clear();
        settings_set_api_key("");
    }

    // ============ галерея ============
    gallery_open(lv_screen_active()); run(500);
    // первая ячейка: x 12..132, y 68..188 (после панели 56 + отступ 12); вторая правее на 128
    dump("gallery_grid");
    CHECK(is_color(72, 128, 255, 0, 255), "grid cell 1 shows the newest photo's thumbnail (B, magenta)");
    CHECK(is_color(200, 128, 230, 0, 0), "grid cell 2 shows photo A's thumbnail (red)");
    CHECK(!is_color(328, 128, 230, 0, 0) && !is_color(328, 128, 255, 0, 255), "no third cell");

    lv_obj_t *body = lv_obj_get_child(lv_screen_active(), 1);
    lv_obj_send_event(lv_obj_get_child(body, 0), LV_EVENT_CLICKED, NULL); run(500);
    CHECK(gallery_viewer_index() == 0, "viewer opened on photo 1");
    dump("gallery_viewer_b");
    CHECK(is_color(205, 251, 255, 255, 0), "viewer shows B at full size (yellow, centered)");
    CHECK(!is_color(30, 251, 255, 255, 0) && is_color(30, 251, 0, 0, 0), "B is 300 px wide: black margins at the sides");
    CHECK(find_label(lv_screen_active(), "1 / 2") != NULL, "counter '1 / 2'");
    CHECK(find_label(lv_screen_active(), "2023-11-15 01:30") != NULL, "date label uses the synced time zone");
    gallery_viewer_step(+1); run(300);
    CHECK(gallery_viewer_index() == 1 && find_label(lv_screen_active(), "2 / 2") != NULL, "next photo");
    dump("gallery_viewer_a");
    CHECK(is_color(205, 150, 0, 0, 255) && is_color(205, 400, 0, 200, 0), "photo A: blue top, green bottom");
    gallery_viewer_step(+1); run(100);
    CHECK(gallery_viewer_index() == 1, "no step past the last photo");
    gallery_viewer_step(-1); run(300);
    CHECK(gallery_viewer_index() == 0 && is_color(205, 251, 255, 255, 0), "previous photo");
    gallery_viewer_step(+1); run(300);

    press(lv_screen_active(), LV_SYMBOL_TRASH);
    CHECK(find_label(lv_layer_top(), "Удалить фото?") != NULL, "delete asks for confirmation");
    press(lv_layer_top(), "Отмена");
    CHECK(photos_count() == 2, "cancel keeps the photo");
    press(lv_screen_active(), LV_SYMBOL_TRASH);
    press(lv_layer_top(), "ОК"); run(300);
    CHECK(photos_count() == 1 && !exists("data/photos/6553f100_000000a1.jpg") && !exists("data/photos/6553f100_000000a1.t.jpg"), "photo A deleted with its thumbnail");
    CHECK(gallery_viewer_index() == 0 && find_label(lv_screen_active(), "1 / 1") != NULL && is_color(205, 251, 255, 255, 0), "viewer moved to the remaining photo");
    press(lv_screen_active(), LV_SYMBOL_LEFT); run(400);
    CHECK(is_color(72, 128, 255, 0, 255) && !is_color(200, 128, 230, 0, 0), "back to the grid: only B left");
    sys_screen_exit(lv_screen_active()); run(400);   // главный экран приложения: выход как свайпом

    // ============ TCP + клиент на Python ============
    memset(ev_count, 0, sizeof ev_count);
    sync_rx_set_event_cb(on_ev, NULL);
    CHECK(!sync_net_running(), "server off while Sync screen is closed");
    CHECK(sync_net_start(18765) && sync_net_running() && strlen(sync_net_code()) == 6, "server started with a 6-digit code");
    char code[8]; snprintf(code, sizeof code, "%s", sync_net_code());
    char cmd[600];
    // неверный код
    snprintf(cmd, sizeof cmd, "python3 %s 127.0.0.1 18765 000000 %s/src_c.png >/dev/null 2>&1", client, fx);
    pid_t pid = fork();
    if (pid == 0) { execlp("sh", "sh", "-c", cmd, (char *)NULL); _exit(127); }
    int st = 0, waited = 0;
    for (int i = 0; i < 5000 && !waited; i++) { run(10); usleep(2000); if (waitpid(pid, &st, WNOHANG) == pid) waited = 1; }
    CHECK(waited && WIFEXITED(st) && WEXITSTATUS(st) == 2, "client with a wrong code is refused (exit 2)");
    CHECK(photos_count() == 1, "refused client stored nothing");
    // верный код: время + два фото + список
    snprintf(cmd, sizeof cmd, "python3 %s 127.0.0.1 18765 %s --time --list %s/src_c.png %s/src_d.png > client.log 2>&1", client, code, fx, fx);
    pid = fork();
    if (pid == 0) { execlp("sh", "sh", "-c", cmd, (char *)NULL); _exit(127); }
    st = 0; waited = 0;
    for (int i = 0; i < 5000 && !waited; i++) { run(10); usleep(2000); if (waitpid(pid, &st, WNOHANG) == pid) waited = 1; }
    CHECK(waited && WIFEXITED(st) && WEXITSTATUS(st) == 0, "python client: pair, time, 2 photos, list - exit 0");
    CHECK(photos_count() == 3, "3 photos on the watch (B + 2 from TCP)");
    CHECK(ev_count[SYNC_EV_PHOTO] == 2 && ev_count[SYNC_EV_TIME_SET] == 1 && ev_count[SYNC_EV_DISCONNECTED] >= 1, "events: 2 photos, time set, disconnect");
    CHECK(clock_is_set(), "clock set over TCP");

    // обработанные клиентом фото помещаются в 410x502 и отображаются
    gallery_open(lv_screen_active()); run(500);
    int cyan = 0, orange = 0;
    for (int cell = 0; cell < 3; cell++) {
        int x = 72 + (cell % 3) * 128, y = 128;
        if (is_color(x, y, 0, 255, 255)) cyan++;
        if (is_color(x, y, 255, 128, 0)) orange++;
    }
    CHECK(cyan == 1 && orange == 1, "thumbnails of both TCP photos are visible (cyan portrait, orange landscape)");
    sys_screen_exit(lv_screen_active()); run(400);   // главный экран приложения: выход как свайпом

    sync_net_stop();
    CHECK(!sync_net_running(), "server stopped");
    snprintf(cmd, sizeof cmd, "python3 %s 127.0.0.1 18765 %s >/dev/null 2>&1", client, code);
    CHECK(system(cmd) != 0, "nobody listens after stop");


    // ============ экран Sync и часы на экране блокировки ============
    sync_app_open(lv_screen_active()); run(400);
    dump("sync_screen");
    CHECK(sync_net_running(), "Sync screen starts the server");
    { char c3[16]; const char *c = sync_net_code(); snprintf(c3, sizeof c3, "%c%c%c %c%c%c", c[0], c[1], c[2], c[3], c[4], c[5]);
      CHECK(find_label(lv_screen_active(), c3) != NULL, "pairing code shown as 'NNN NNN'");
      CHECK(find_label(lv_screen_active(), "Ждём телефон") != NULL, "status: waiting for phone"); }
    sys_screen_exit(lv_screen_active()); run(400);   // главный экран приложения: выход как свайпом
    CHECK(!sync_net_running(), "leaving Sync screen stops the server");

    CHECK(auth_set_pin("1234"), "PIN set for the lock screen clock test");
    lock_screen_lock(); run(100);
    { struct tm now; clock_local(&now); char tm5[8]; snprintf(tm5, sizeof tm5, "%02d:%02d", now.tm_hour, now.tm_min);
      CHECK(find_label(lv_layer_sys(), tm5) != NULL, "lock screen shows the synced time"); }
    dump("lock_screen");
    for (const char *k = "1234"; *k; k++) { char s[2] = { *k, 0 }; press(lv_layer_sys(), s); }
    run(300);
    auth_remove_pin();

    printf(fails ? "\n%d FAILED\n" : "\nALL PASSED\n", fails);
    return fails != 0;
}
