// sync_server.c - ядро часов без окна как сервер синхронизации, для проверки клиентов (Python, Kotlin) настоящим TCP.
// Запуск: sync_server <port> <сколько сеансов обслужить>. Печатает CODE, потом итоги: PHOTOS, TIME.
#include "watch/clock.h"
#include "watch/lvgl_inc.h"
#include "watch/photos.h"
#include "watch/sync_net.h"
#include "watch/sync_rx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void watch_launcher_create(void);

static int sessions_left;
static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px) { (void)a; (void)px; lv_display_flush_ready(d); }
static void on_ev(sync_event_t ev, void *ud) { (void)ud; if (ev == SYNC_EV_DISCONNECTED) sessions_left--; }

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    int port = argc > 1 ? atoi(argv[1]) : 18766;
    sessions_left = argc > 2 ? atoi(argv[2]) : 1;
    if (access("src", F_OK) == 0 || access("CMakeLists.txt", F_OK) == 0) {   // тест стирает data/ и apps/ в текущей папке
        printf("Run this from an empty scratch directory, not from the project root.\n");
        return 2;
    }
    lv_init();
    lv_display_t *d = lv_display_create(410, 502);
    static uint8_t buf[410 * 502 * 4];
    lv_display_set_buffers(d, buf, NULL, sizeof buf, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush);
    if (system("rm -rf data apps")) return 2;
    watch_launcher_create();
    sync_rx_set_event_cb(on_ev, NULL);
    if (!sync_net_start((uint16_t)port)) { printf("ERROR cannot listen\n"); return 1; }
    printf("CODE %s\n", sync_net_code());
    for (int i = 0; i < 30000 && sessions_left > 0; i++) { lv_tick_inc(5); lv_timer_handler(); usleep(2000); }
    uint32_t ids[PHOTOS_MAX];
    int n = photos_list(ids, NULL, PHOTOS_MAX);
    printf("PHOTOS %d", n);
    for (int i = 0; i < n; i++) printf(" %08x", (unsigned)ids[i]);
    struct tm t;
    if (clock_local(&t)) printf("\nTIME %04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday); else printf("\nTIME none");
    printf("\nSESSIONS_LEFT %d\n", sessions_left);
    sync_net_stop();
    return 0;
}
