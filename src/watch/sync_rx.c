#include "sync_rx.h"
#include "clock.h"
#include "crc32.h"
#include "lock_screen.h"
#include "messages.h"
#include "photos.h"
#include "sync_proto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_AUTH_FAILS 3

typedef enum { SCOPE_NONE, SCOPE_FULL, SCOPE_MSG } scope_t;

typedef struct {
    bool open, authed;
    scope_t scope;
    char code[SYNC_CODE_LEN + 1];
    int  auth_fails;
    sync_io_t io;

    // разбор потока кадров
    uint8_t  hdr[5];
    size_t   hdr_n;
    uint8_t  type;
    uint32_t len, got;
    uint8_t *payload;            // для кадров, кроме PHOTO_DATA

    // текущее фото
    bool     photo_active;
    uint32_t id, taken, thumb_n, full_n, crc, recv;
    uint8_t *buf;                // миниатюра + полный кадр подряд
} sess_t;

static sess_t        S;
static char          g_notify_code[SYNC_CODE_LEN + 1];
static sync_event_cb g_cb;
static void         *g_cb_ud;

void sync_rx_set_notify_code(const char *code)
{
    memset(g_notify_code, 0, sizeof g_notify_code);
    if (code && strlen(code) == SYNC_CODE_LEN) memcpy(g_notify_code, code, SYNC_CODE_LEN);
}

void sync_rx_set_event_cb(sync_event_cb cb, void *ud) { g_cb = cb; g_cb_ud = ud; }

static void emit(sync_event_t ev) { if (g_cb) g_cb(ev, g_cb_ud); }

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static void     wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

static void send_frame(uint8_t type, const uint8_t *data, size_t n)
{
    uint8_t out[5 + 810];
    if (n > sizeof out - 5) return;
    out[0] = type;
    wr32(out + 1, (uint32_t)n);
    if (n) memcpy(out + 5, data, n);
    if (S.io.send) S.io.send(out, 5 + n, S.io.ud);
}

static void send_ok(uint8_t req, uint8_t status) { uint8_t p[2] = { req, status }; send_frame(SYNC_T_OK, p, 2); }
static void send_err(uint8_t req, uint8_t code)  { uint8_t p[2] = { req, code };   send_frame(SYNC_T_ERR, p, 2); }

static void photo_reset(void)
{
    free(S.buf);
    S.buf = NULL;
    S.photo_active = false;
    S.recv = 0;
}

void sync_rx_open(const char *code, const sync_io_t *io)
{
    sync_rx_close();
    memset(&S, 0, sizeof S);
    S.open = true;
    snprintf(S.code, sizeof S.code, "%s", code);
    S.io = *io;
    emit(SYNC_EV_CONNECTED);
}

void sync_rx_close(void)
{
    if (!S.open) return;
    photo_reset();
    free(S.payload);
    S.payload = NULL;
    S.open = false;
    S.authed = false;
    S.scope = SCOPE_NONE;
    emit(SYNC_EV_DISCONNECTED);
}

bool sync_rx_authenticated(void) { return S.open && S.authed; }

// Сравнение кода за постоянное время. Пустой код (одноразовый закрыт или уведомления выключены) никогда не совпадает.
static bool code_match(const char *expect)
{
    if (!expect[0]) return false;
    uint8_t diff = 0;
    for (int i = 0; i < SYNC_CODE_LEN; i++) diff |= (uint8_t)(S.payload[1 + i] ^ (uint8_t)expect[i]);
    return diff == 0;
}

static void on_hello(bool locked)
{
    if (S.len < 1 + SYNC_CODE_LEN || S.payload[0] != SYNC_PROTO_VERSION) { send_err(SYNC_T_HELLO, SYNC_E_BAD_FRAME); return; }
    bool full = code_match(S.code);
    bool msg  = code_match(g_notify_code);          // оба сравнения выполняются всегда
    if (locked && !msg) { send_err(SYNC_T_HELLO, SYNC_E_LOCKED); S.auth_fails = MAX_AUTH_FAILS; return; }   // полный доступ при блокировке закрыт
    if (!full && !msg) {
        S.auth_fails++;
        send_err(SYNC_T_HELLO, SYNC_E_AUTH);
        emit(SYNC_EV_AUTH_FAIL);
        return;
    }
    S.authed = true;
    S.scope = full ? SCOPE_FULL : SCOPE_MSG;
    uint8_t p[9];
    p[0] = SYNC_PROTO_VERSION;
    size_t cnt = full ? photos_count() : 0;         // сопряжение для уведомлений о фото ничего не знает
    p[1] = (uint8_t)cnt; p[2] = (uint8_t)(cnt >> 8);
    p[3] = (uint8_t)PHOTOS_MAX; p[4] = (uint8_t)(PHOTOS_MAX >> 8);
    wr32(p + 5, PHOTO_FULL_MAX_BYTES);
    send_frame(SYNC_T_HELLO_OK, p, sizeof p);
    emit(SYNC_EV_AUTH_OK);
}

static void on_time(void)
{
    if (S.len < 10) { send_err(SYNC_T_TIME, SYNC_E_BAD_FRAME); return; }
    int64_t epoch = 0;
    for (int i = 7; i >= 0; i--) epoch = (epoch << 8) | S.payload[i];
    int tz = (int16_t)((uint16_t)S.payload[8] | ((uint16_t)S.payload[9] << 8));
    if (epoch < 1500000000 || tz < -14 * 60 || tz > 14 * 60) { send_err(SYNC_T_TIME, SYNC_E_BAD_FRAME); return; }
    clock_set(epoch, tz);
    send_ok(SYNC_T_TIME, 0);
    emit(SYNC_EV_TIME_SET);
}

static void on_photo_begin(void)
{
    if (S.len != 20) { send_err(SYNC_T_PHOTO_BEGIN, SYNC_E_BAD_FRAME); return; }
    photo_reset();
    uint32_t id = rd32(S.payload), taken = rd32(S.payload + 4), tn = rd32(S.payload + 8), fn = rd32(S.payload + 12), crc = rd32(S.payload + 16);
    if (tn == 0 || fn == 0 || tn > PHOTO_THUMB_MAX_BYTES || fn > PHOTO_FULL_MAX_BYTES) { send_err(SYNC_T_PHOTO_BEGIN, SYNC_E_TOO_BIG); return; }
    if (photos_exists(id)) { send_ok(SYNC_T_PHOTO_BEGIN, SYNC_S_HAVE); return; }
    if (photos_count() >= PHOTOS_MAX) { send_err(SYNC_T_PHOTO_BEGIN, SYNC_E_STORAGE); return; }
    S.buf = malloc((size_t)tn + fn);
    if (!S.buf) { send_err(SYNC_T_PHOTO_BEGIN, SYNC_E_STORAGE); return; }
    S.photo_active = true;
    S.id = id; S.taken = taken; S.thumb_n = tn; S.full_n = fn; S.crc = crc; S.recv = 0;
    send_ok(SYNC_T_PHOTO_BEGIN, SYNC_S_READY);
}

static void on_photo_end(void)
{
    if (!S.photo_active || S.recv != S.thumb_n + S.full_n) { photo_reset(); send_err(SYNC_T_PHOTO_END, SYNC_E_STATE); return; }
    if (crc32_update(0, S.buf, S.recv) != S.crc) { photo_reset(); send_err(SYNC_T_PHOTO_END, SYNC_E_CRC); return; }
    const uint8_t *thumb = S.buf, *full = S.buf + S.thumb_n;
    if (!photos_check_jpeg(thumb, S.thumb_n, PHOTO_THUMB_MAX, PHOTO_THUMB_MAX, NULL, NULL) ||
        !photos_check_jpeg(full, S.full_n, PHOTO_FULL_MAX_W, PHOTO_FULL_MAX_H, NULL, NULL)) {
        photo_reset(); send_err(SYNC_T_PHOTO_END, SYNC_E_FORMAT); return;
    }
    bool ok = photos_store(S.id, S.taken, thumb, S.thumb_n, full, S.full_n);
    photo_reset();
    if (!ok) { send_err(SYNC_T_PHOTO_END, SYNC_E_STORAGE); return; }
    send_ok(SYNC_T_PHOTO_END, 0);
    emit(SYNC_EV_PHOTO);
}

static void on_list(void)
{
    uint32_t ids[200];
    int n = photos_list(ids, NULL, 200);
    uint8_t p[2 + 200 * 4];
    p[0] = (uint8_t)n; p[1] = (uint8_t)(n >> 8);
    for (int i = 0; i < n; i++) wr32(p + 2 + 4 * i, ids[i]);
    send_frame(SYNC_T_LIST, p, (size_t)(2 + 4 * n));
}

static void on_delete(void)
{
    if (S.len != 4) { send_err(SYNC_T_PHOTO_DELETE, SYNC_E_BAD_FRAME); return; }
    if (photos_delete(rd32(S.payload))) { send_ok(SYNC_T_PHOTO_DELETE, 0); emit(SYNC_EV_PHOTO_DELETED); }
    else send_err(SYNC_T_PHOTO_DELETE, SYNC_E_STATE);
}

// MESSAGE: id u32, ts u32, app_len u8, title_len u8, text_len u16, app, title, text. Длины должны сходиться с размером кадра.
static void on_message(void)
{
    if (S.len < 12) { send_err(SYNC_T_MESSAGE, SYNC_E_BAD_FRAME); return; }
    uint32_t id = rd32(S.payload), ts = rd32(S.payload + 4);
    size_t al = S.payload[8], tl = S.payload[9], xl = (size_t)S.payload[10] | ((size_t)S.payload[11] << 8);
    if (al > SYNC_MSG_APP_MAX || tl > SYNC_MSG_TITLE_MAX || xl > SYNC_MSG_TEXT_MAX || 12 + al + tl + xl != S.len) {
        send_err(SYNC_T_MESSAGE, al > SYNC_MSG_APP_MAX || tl > SYNC_MSG_TITLE_MAX || xl > SYNC_MSG_TEXT_MAX ? SYNC_E_TOO_BIG : SYNC_E_BAD_FRAME);
        return;
    }
    char app[SYNC_MSG_APP_MAX + 1], title[SYNC_MSG_TITLE_MAX + 1], text[SYNC_MSG_TEXT_MAX + 1];
    memcpy(app, S.payload + 12, al);           app[al] = '\0';
    memcpy(title, S.payload + 12 + al, tl);    title[tl] = '\0';
    memcpy(text, S.payload + 12 + al + tl, xl); text[xl] = '\0';
    messages_add(id, ts, app, title, text);
    send_ok(SYNC_T_MESSAGE, 0);
    emit(SYNC_EV_MESSAGE);
}

// Возвращает false, если соединение надо закрыть.
static bool on_frame(void)
{
    bool locked = lock_screen_is_locked();
    if (S.type == SYNC_T_BYE) return false;
    if (S.type == SYNC_T_HELLO) {
        on_hello(locked);
        return S.auth_fails < MAX_AUTH_FAILS;
    }
    if (locked && S.scope != SCOPE_MSG) { send_err(S.type, SYNC_E_LOCKED); return false; }
    if (!S.authed) { send_err(S.type, SYNC_E_STATE); return false; }
    if (S.scope == SCOPE_MSG && S.type != SYNC_T_TIME && S.type != SYNC_T_MESSAGE) {
        send_err(S.type, SYNC_E_SCOPE);
        return true;
    }
    switch (S.type) {
    case SYNC_T_TIME:         on_time(); break;
    case SYNC_T_MESSAGE:      on_message(); break;
    case SYNC_T_PHOTO_BEGIN:  on_photo_begin(); break;
    case SYNC_T_PHOTO_END:    on_photo_end(); break;
    case SYNC_T_PHOTO_LIST:   on_list(); break;
    case SYNC_T_PHOTO_DELETE: on_delete(); break;
    default:                  send_err(S.type, SYNC_E_BAD_FRAME); break;
    }
    return true;
}

bool sync_rx_feed(const uint8_t *d, size_t n)
{
    if (!S.open) return false;
    while (n > 0) {
        if (S.hdr_n < 5) {                          // читаем заголовок кадра
            S.hdr[S.hdr_n++] = *d++; n--;
            if (S.hdr_n < 5) continue;
            S.type = S.hdr[0];
            S.len = rd32(S.hdr + 1);
            S.got = 0;
            if (S.len > SYNC_MAX_FRAME) { send_err(S.type, SYNC_E_TOO_BIG); return false; }
            if (S.type != SYNC_T_PHOTO_DATA) {
                free(S.payload);
                S.payload = malloc(S.len ? S.len : 1);
                if (!S.payload) return false;
            } else if (!S.authed || !S.photo_active) {
                send_err(S.type, SYNC_E_STATE);
                return false;
            }
            if (S.len == 0) {                       // пустой кадр: сразу обработать
                S.hdr_n = 0;
                if (S.type == SYNC_T_PHOTO_DATA) continue;
                if (!on_frame()) return false;
            }
            continue;
        }
        size_t take = S.len - S.got;                // читаем данные кадра
        if (take > n) take = n;
        if (S.type == SYNC_T_PHOTO_DATA) {
            if (S.recv + take > S.thumb_n + S.full_n) { send_err(S.type, SYNC_E_TOO_BIG); photo_reset(); return false; }
            memcpy(S.buf + S.recv, d, take);
            S.recv += (uint32_t)take;
        } else {
            memcpy(S.payload + S.got, d, take);
        }
        S.got += (uint32_t)take; d += take; n -= take;
        if (S.got == S.len) {
            S.hdr_n = 0;
            if (S.type != SYNC_T_PHOTO_DATA && !on_frame()) return false;
        }
    }
    return true;
}
