#include "sync_net.h"
#include "lvgl_inc.h"
#include "plat.h"
#include "settings.h"
#include "sync_proto.h"
#include "sync_rx.h"
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

static int         g_srv = -1, g_cli = -1;
static uint16_t    g_port;
static char        g_code[SYNC_CODE_LEN + 1];
static lv_timer_t *g_timer;
static uint32_t    g_cli_last;                    // тик последней активности клиента
static bool        g_want_sync, g_want_notify;   // зачем поднят сервер: экран Sync и/или уведомления с телефона

static void set_nonblock(int fd) { fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK); }

static char        g_drop[64];                    // причина последнего разрыва (показывается на экране Sync)

static void drop_client(void)
{
    if (g_cli >= 0) { close(g_cli); g_cli = -1; }
    sync_rx_close();
}

static void drop_with(const char *why)
{
    snprintf(g_drop, sizeof g_drop, "%s", why);
    printf("[sync] drop: %s\n", g_drop);
    drop_client();
}

static void io_send(const uint8_t *d, size_t n, void *ud)
{
    (void)ud;
    size_t off = 0;
    int spins = 0;
    while (off < n && g_cli >= 0) {
        ssize_t r = send(g_cli, d + off, n - off, MSG_NOSIGNAL);
        if (r > 0) { off += (size_t)r; spins = 0; continue; }
        if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && ++spins < 2000) { usleep(1000); continue; }
        return;   // ошибка: соединение закроется при следующем чтении
    }
}

static void poll_cb(lv_timer_t *t)
{
    (void)t;
    if (g_srv < 0) return;

    if (g_cli < 0) {
        int fd = accept(g_srv, NULL, NULL);
        if (fd < 0) return;
        set_nonblock(fd);
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        g_cli = fd;
        g_cli_last = lv_tick_get();
        g_drop[0] = '\0';
        sync_io_t io = { io_send, NULL };
        sync_rx_open(g_code, &io);
        return;
    }

    // Молчащий клиент не должен занимать единственное место: при фоновом приёме уведомлений порт открыт постоянно.
    if (lv_tick_elaps(g_cli_last) > 20000) { drop_with("idle for 20 s"); return; }

    uint8_t buf[8192];
    for (int i = 0; i < 8; i++) {           // не дольше ~64 КБ за один тик, чтобы интерфейс не замирал
        ssize_t r = recv(g_cli, buf, sizeof buf, 0);
        if (r > 0) {
            g_cli_last = lv_tick_get();
            if (!sync_rx_feed(buf, (size_t)r)) { drop_with(sync_rx_close_reason()[0] ? sync_rx_close_reason() : "closed by protocol"); return; }
        } else if (r == 0) {
            drop_with("phone closed first");
            return;
        } else {
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                char w[40];
                snprintf(w, sizeof w, "recv error %d", errno);
                drop_with(w);
            }
            return;
        }
    }
}

static bool server_open(uint16_t port)
{
    if (g_srv >= 0) return true;               // уже слушаем (например, из-за уведомлений): порт не меняем
    g_port = port ? port : SYNC_PORT;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(g_port);
    if (bind(fd, (struct sockaddr *)&a, sizeof a) != 0 || listen(fd, 1) != 0) { close(fd); return false; }
    set_nonblock(fd);
    g_srv = fd;
    g_timer = lv_timer_create(poll_cb, 20, NULL);
    return true;
}

static void server_close(void)
{
    if (g_timer) { lv_timer_delete(g_timer); g_timer = NULL; }
    drop_client();
    if (g_srv >= 0) { close(g_srv); g_srv = -1; }
}

bool sync_net_start(uint16_t port)
{
    if (g_want_sync) return true;
    uint8_t rnd[SYNC_CODE_LEN];
    if (!plat_random(rnd, sizeof rnd)) return false;
    if (!server_open(port)) return false;
    for (int i = 0; i < SYNC_CODE_LEN; i++) g_code[i] = (char)('0' + rnd[i] % 10);
    g_code[SYNC_CODE_LEN] = '\0';
    g_want_sync = true;
    return true;
}

void sync_net_stop(void)
{
    g_want_sync = false;
    memset(g_code, 0, sizeof g_code);        // одноразовый код больше не действует
    g_drop[0] = '\0';
    drop_client();                           // сессия с этим кодом закрывается; уведомления переподключатся сами
    if (!g_want_notify) server_close();
}

bool sync_net_notify_apply(void)
{
    g_want_notify = settings_notify_on() && settings_notify_code()[0];
    sync_rx_set_notify_code(g_want_notify ? settings_notify_code() : NULL);
    if (g_want_notify) return server_open(0);
    if (!g_want_sync) server_close();
    return true;
}

bool        sync_net_running(void) { return g_srv >= 0; }
const char *sync_net_code(void)    { return g_code; }
uint16_t    sync_net_port(void)    { return g_port; }
const char *sync_net_last_drop(void) { return g_drop; }
