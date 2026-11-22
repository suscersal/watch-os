#include "sync_app.h"
#include "pairing.h"
#include "photos.h"
#include "plat.h"
#include "sync_net.h"
#include "sync_rx.h"
#include "sys_ui.h"
#include "watch_fonts.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *g_launcher, *scr, *status_lbl;
static int       got_photos;

static void set_status(const char *t, uint32_t color)
{
    if (!status_lbl) return;
    lv_label_set_text(status_lbl, t);
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(color), 0);
}

static void on_event(sync_event_t ev, void *ud)
{
    LV_UNUSED(ud);
    char t[64];
    switch (ev) {
    case SYNC_EV_CONNECTED:     set_status("Phone connected...", 0x9CA3AF); break;
    case SYNC_EV_AUTH_OK:       set_status("Paired. Waiting for photos", 0x22C55E); break;
    case SYNC_EV_AUTH_FAIL:     set_status("Wrong code", 0xEF4444); break;
    case SYNC_EV_TIME_SET:      sys_toast("Time synced"); break;
    case SYNC_EV_PHOTO:
        got_photos++;
        snprintf(t, sizeof t, "Photos received: %d", got_photos);
        set_status(t, 0x22C55E);
        break;
    case SYNC_EV_PHOTO_DELETED: sys_toast("Photo removed by phone"); break;
    case SYNC_EV_DISCONNECTED:  if (sync_net_running()) set_status(got_photos ? "Done. Waiting for phone" : "Waiting for phone", 0x9CA3AF); break;
    }
}

static void gone_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    sync_rx_set_event_cb(NULL, NULL);
    sync_net_stop();
    scr = status_lbl = NULL;
}

static void back_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    sync_net_stop();     // приём выключается сразу при выходе
    lv_screen_load_anim(g_launcher, sys_exit_anim(), 200, 0, true);
}

static lv_obj_t *add_label(lv_obj_t *body, const char *text, uint32_t color, const lv_font_t *font)
{
    lv_obj_t *l = lv_label_create(body);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    if (font) lv_obj_set_style_text_font(l, font, 0);
    return l;
}

void sync_app_open(lv_obj_t *launcher)
{
    g_launcher = launcher;
    got_photos = 0;
    lv_obj_t *body;
    scr = sys_screen_create_root("Sync", back_cb, &body);
    lv_obj_add_event_cb(scr, gone_cb, LV_EVENT_DELETE, NULL);

    add_label(body, "In the phone app enter this address and code:", 0x9CA3AF, NULL);

    if (!sync_net_start(0)) {
        add_label(body, "Cannot open the sync port.", 0xEF4444, NULL);
        status_lbl = NULL;
    } else {
        char ip[40], line[64];
        if (!plat_local_ip(ip, sizeof ip)) snprintf(ip, sizeof ip, "no network");
        snprintf(line, sizeof line, "%s : %u", ip, (unsigned)sync_net_port());
        add_label(body, "Address", 0x9CA3AF, WATCH_FONT_SMALL);
        add_label(body, line, 0xFFFFFF, WATCH_FONT_LARGE);

        const char *c = sync_net_code();
        char code[16];
        snprintf(code, sizeof code, "%c%c%c %c%c%c", c[0], c[1], c[2], c[3], c[4], c[5]);
        add_label(body, "Code", 0x9CA3AF, WATCH_FONT_SMALL);
        add_label(body, code, 0x38BDF8, WATCH_FONT_LARGE);

        if (strcmp(ip, "no network") != 0) {          // QR: телефон открывает ссылку и подставляет адрес и код
            char url[128];
            pairing_url(url, sizeof url, ip, sync_net_port(), c, false);
            lv_obj_t *wrap = lv_obj_create(body);
            lv_obj_remove_style_all(wrap);
            lv_obj_set_size(wrap, lv_pct(100), LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(wrap, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            pairing_qr_create(wrap, url, 200);
        }

        status_lbl = add_label(body, "Waiting for phone", 0x9CA3AF, NULL);
        sync_rx_set_event_cb(on_event, NULL);
    }
    lv_screen_load_anim(scr, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 200, 0, false);   // лаунчер не удаляем
}
