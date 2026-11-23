#include "messages.h"
#include "lock_screen.h"
#include "power.h"
#include "sys_ui.h"
#include <stdio.h>
#include <string.h>

static msg_t  g_list[MSG_MAX];
static size_t g_n;
static void (*g_cb)(void);

static void changed(void) { if (g_cb) g_cb(); }

// Копирует не больше cap байт, не обрывая символ utf-8 посередине.
static void copy_utf8(char *dst, size_t cap, const char *src)
{
    size_t n = src ? strlen(src) : 0;
    if (n > cap) {
        n = cap;
        while (n > 0 && ((unsigned char)src[n] & 0xC0) == 0x80) n--;   // src[n] - продолжение символа: отступаем к его началу
    }
    if (n) memcpy(dst, src, n);
    dst[n] = '\0';
}

void messages_add(uint32_t id, uint32_t ts, const char *app, const char *title, const char *text)
{
    size_t at = g_n;                                   // позиция, которую освободим
    for (size_t i = 0; i < g_n; i++) if (g_list[i].id == id) { at = i; break; }
    if (at == g_n) {                                   // новое сообщение: если список полон, самое старое вылетает
        if (g_n < MSG_MAX) g_n++;
        at = g_n - 1;
    }
    memmove(&g_list[1], &g_list[0], at * sizeof g_list[0]);   // сдвинуть новее-старее, освободив начало
    msg_t *m = &g_list[0];
    memset(m, 0, sizeof *m);
    m->id = id;
    m->ts = ts;
    copy_utf8(m->app, MSG_APP_MAX, app);
    copy_utf8(m->title, MSG_TITLE_MAX, title);
    copy_utf8(m->text, MSG_TEXT_MAX, text);
    changed();

    power_wake();                                      // включить экран; при блокировке виден только экран PIN
    if (!lock_screen_is_locked()) {
        char t[128];
        snprintf(t, sizeof t, "%s%s%s", m->app, m->app[0] && m->title[0] ? ": " : "", m->title[0] ? m->title : (m->app[0] ? "" : "Message"));
        sys_toast(t);
    }
}

size_t       messages_count(void)      { return g_n; }
const msg_t *messages_get(size_t i)    { return i < g_n ? &g_list[i] : NULL; }

size_t messages_unread(void)
{
    size_t c = 0;
    for (size_t i = 0; i < g_n; i++) if (!g_list[i].read) c++;
    return c;
}

void messages_mark_read(size_t i)
{
    if (i < g_n && !g_list[i].read) { g_list[i].read = true; changed(); }
}

void messages_clear(void)
{
    g_n = 0;
    changed();
}

void messages_set_change_cb(void (*cb)(void)) { g_cb = cb; }
