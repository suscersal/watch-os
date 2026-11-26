// pairing_rx_test.c - рукопожатие с токеном: код -> PAIR_REQ -> TOKEN, потом HELLO_TOKEN без кода; блокировка, неверный токен, область доступа.
// Остальные модули ядра заменены заглушками, LVGL не линкуется (нужны только его заголовки).
#include "watch/paired.h"
#include "watch/sync_proto.h"
#include "watch/sync_rx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "watch/clock.h"
#include "watch/lock_screen.h"
#include "watch/messages.h"
#include "watch/music_app.h"
#include "watch/audio.h"
#include "watch/photos.h"

#include "fake_ota.h"
static bool g_locked;
bool plat_battery(int *pct, bool *chg) { *pct = 80; *chg = false; return true; }
bool lock_screen_is_locked(void) { return g_locked; }
void clock_set(int64_t e, int tz) { (void)e; (void)tz; }
void messages_add(uint32_t a, uint32_t b, const char *c, const char *d, const char *e) { (void)a;(void)b;(void)c;(void)d;(void)e; }
const char *music_dir(void) { return "x"; }
bool audio_wav_info(const char *p, wav_info_t *w) { (void)p;(void)w; return false; }
size_t photos_count(void) { return 3; }
bool photos_exists(uint32_t id) { (void)id; return false; }
bool photos_check_jpeg(const uint8_t *d, size_t n, int w, int h, int *a, int *b) { (void)d;(void)n;(void)w;(void)h;(void)a;(void)b; return true; }
bool photos_store(uint32_t id, uint32_t t, const uint8_t *a, size_t an, const uint8_t *b, size_t bn) { (void)id;(void)t;(void)a;(void)an;(void)b;(void)bn; return true; }
int photos_list(uint32_t *ids, uint32_t *t, int max) { (void)ids;(void)t;(void)max; return 0; }
bool photos_delete(uint32_t id) { (void)id; return true; }

static unsigned rs = 7;
bool plat_random(void *buf, size_t n) { uint8_t *p = buf; for (size_t i = 0; i < n; i++) { rs = rs * 1103515245u + 12345u; p[i] = (uint8_t)(rs >> 16); } return true; }

static uint8_t out[512]; static size_t out_n;
static void io_send(const uint8_t *d, size_t n, void *ud) { (void)ud; memcpy(out + out_n, d, n); out_n += n; }
static int fails;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s\n", msg); fails++; } else printf("ok:   %s\n", msg); } while (0)

static bool feed(uint8_t type, const uint8_t *p, size_t n)
{
    uint8_t h[5] = { type, (uint8_t)n, (uint8_t)(n >> 8), 0, 0 };
    out_n = 0;
    if (!sync_rx_feed(h, 5)) return false;
    return n == 0 ? true : sync_rx_feed(p, n);
}
static uint8_t last_type(void) { return out_n ? out[0] : 0; }
static int last_err(void) { return out_n >= 7 ? out[6] : -1; }

static bool hello_code(const char *code, const char *name)
{
    uint8_t p[64]; p[0] = 1; memcpy(p + 1, code, 6); size_t nl = strlen(name); memcpy(p + 7, name, nl);
    return feed(SYNC_T_HELLO, p, 7 + nl);
}
static bool hello_token(const uint8_t *tok)
{
    uint8_t p[1 + PAIRED_TOKEN_LEN + 5]; p[0] = 1; memcpy(p + 1, tok, PAIRED_TOKEN_LEN); memcpy(p + 17, "phone", 5);
    return feed(SYNC_T_HELLO_TOKEN, p, sizeof p);
}
static const sync_io_t IO = { io_send, NULL };

int main(void)
{
    if (system("rm -rf rxdir")) return 2;
    paired_init("rxdir");
    sync_rx_set_notify_code("999111");
    uint8_t tok[PAIRED_TOKEN_LEN];

    sync_rx_open("123456", &IO);
    CHECK(hello_code("123456", "Pixel 8") && last_type() == SYNC_T_HELLO_OK, "HELLO with one-time code");
    CHECK(feed(SYNC_T_PAIR_REQ, NULL, 0) && last_type() == SYNC_T_TOKEN && out_n == 5 + PAIRED_TOKEN_LEN, "PAIR_REQ returns token");
    memcpy(tok, out + 5, sizeof tok);
    CHECK(paired_count() == 1, "watch stored the device");
    sync_rx_close();

    sync_rx_open("", &IO);                           // Sync закрыт: одноразового кода нет
    CHECK(hello_token(tok) && last_type() == SYNC_T_HELLO_OK, "HELLO_TOKEN works with no code");
    CHECK(feed(SYNC_T_PHOTO_LIST, NULL, 0) && last_type() == SYNC_T_LIST, "full scope after token");
    CHECK(feed(SYNC_T_PAIR_REQ, NULL, 0) && last_type() == SYNC_T_ERR && last_err() == SYNC_E_SCOPE, "token cannot mint new tokens");
    ota_set_hw(&FAKE_OTA);
    { uint8_t fb[8] = { 0, 0, 4, 0, 1, 2, 3, 4 };                       // 256 КБ, любой crc
      CHECK(feed(SYNC_T_FW_BEGIN, fb, 8) && last_type() == SYNC_T_ERR && last_err() == SYNC_E_SCOPE, "token session cannot flash firmware (code only)"); }
    ota_set_hw(NULL);
    sync_rx_close();

    sync_rx_open("", &IO);
    g_locked = true;
    CHECK(!hello_token(tok) && last_err() == SYNC_E_LOCKED, "locked watch refuses token");
    g_locked = false;
    sync_rx_close();

    sync_rx_open("", &IO);
    uint8_t bad[PAIRED_TOKEN_LEN]; memset(bad, 7, sizeof bad);
    CHECK(hello_token(bad) && last_err() == SYNC_E_AUTH, "bad token 1");
    CHECK(hello_token(bad) && last_err() == SYNC_E_AUTH, "bad token 2");
    CHECK(!hello_token(bad), "3rd bad token closes connection");
    sync_rx_close();

    sync_rx_open("", &IO);
    CHECK(hello_code("999111", "Pixel 8") && last_type() == SYNC_T_HELLO_OK, "notify code still works");
    CHECK(feed(SYNC_T_PAIR_REQ, NULL, 0) && last_err() == SYNC_E_SCOPE, "notify code cannot pair");
    sync_rx_close();

    sync_rx_open("", &IO);
    CHECK(feed(SYNC_T_PAIR_REQ, NULL, 0) == false && last_err() == SYNC_E_STATE, "PAIR_REQ before HELLO refused");
    sync_rx_close();

    paired_remove(0);
    sync_rx_open("", &IO);
    CHECK(hello_token(tok) && last_err() == SYNC_E_AUTH, "forgotten token refused");
    sync_rx_close();

    printf(fails ? "FAILED\n" : "ALL OK\n");
    return fails ? 1 : 0;
}
