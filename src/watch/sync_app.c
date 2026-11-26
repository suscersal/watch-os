#include "sync_app.h"
#include "paired.h"
#include "pairing.h"
#include "photos.h"
#include "plat.h"
#include "sync_net.h"
#include "sync_rx.h"
#include "sys_ui.h"
#include "watch_fonts.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *g_launcher, *scr, *status_lbl, *list_box;
static int       got_photos;

static void set_status(const char *t, uint32_t color)
{
    if (!status_lbl) return;
    lv_label_set_text(status_lbl, t);
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(color), 0);
}

static lv_obj_t *add_label(lv_obj_t *body, const char *text, uint32_t color, const lv_font_t *font);
static void rebuild_list(void);

static void rebuild_async(void *ud) { LV_UNUSED(ud); rebuild_list(); }

// Забыть сопряжённый телефон: его токен перестаёт работать, дальше вход только по коду с этого экрана.
static void forget_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (paired_remove(idx)) sys_toast("Device forgotten");
    sync_net_notify_apply();                 // нет сопряжённых и уведомления выключены - порт закроется при выходе
    lv_async_call(rebuild_async, NULL);      // перестроить после выхода из обработчика кнопки
}

static void rebuild_list(void)
{
    if (!list_box) return;
    lv_obj_clean(list_box);
    int n = paired_count();
    add_label(list_box, n ? "Paired phones (connect without a code):" : "No paired phones. Enter the code in the app once, and next time no code is needed.",
              0x9CA3AF, WATCH_FONT_SMALL);
    for (int i = 0; i < n; i++) {
        char name[PAIRED_NAME_MAX + 1];
        if (!paired_info(i, name, sizeof name)) break;
        lv_obj_t *row = lv_obj_create(list_box);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, lv_pct(100), 44);
        lv_obj_t *l = lv_label_create(row);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        lv_obj_set_width(l, lv_pct(60));
        lv_label_set_text(l, name);
        lv_obj_set_style_text_color(l, lv_color_white(), 0);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_t *b = lv_button_create(row);
        lv_obj_set_size(b, LV_SIZE_CONTENT, 36);
        lv_obj_set_style_bg_color(b, lv_color_hex(0xEF4444), 0);
        lv_obj_align(b, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_t *bl = lv_label_create(b);
        lv_label_set_text(bl, "Forget");
        lv_obj_center(bl);
        lv_obj_add_event_cb(b, forget_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
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
    case SYNC_EV_AUDIO:         sys_toast("Audio received"); break;
    case SYNC_EV_PAIRED:        sys_toast("Phone paired"); rebuild_list(); break;
    case SYNC_EV_PHOTO_DELETED: sys_toast("Photo removed by phone"); break;
    case SYNC_EV_MESSAGE:       break;     // уведомление уже лежит в messages; на экране синхронизации ничего не меняем
    case SYNC_EV_DISCONNECTED:
        if (sync_net_running()) {
            if (sync_net_last_drop()[0]) { snprintf(t, sizeof t, "Dropped: %s", sync_net_last_drop()); set_status(t, 0xF59E0B); }
            else set_status(got_photos ? "Done. Waiting for phone" : "Waiting for phone", 0x9CA3AF);
        }
        break;
    }
}

static void gone_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    sync_rx_set_event_cb(NULL, NULL);
    sync_net_stop();
    scr = status_lbl = list_box = NULL;
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

    add_label(body, "First time: enter this address and code in the phone app. After that the phone connects by itself.", 0x9CA3AF, NULL);

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
    list_box = lv_obj_create(body);                    // сопряжённые телефоны: токены можно забыть
    lv_obj_remove_style_all(list_box);
    lv_obj_set_size(list_box, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(list_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list_box, 6, 0);
    rebuild_list();
    lv_screen_load_anim(scr, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 200, 0, false);   // лаунчер не удаляем
}
