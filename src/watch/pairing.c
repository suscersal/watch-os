#include "pairing.h"
#include <stdio.h>
#include <string.h>

void pairing_url(char *out, unsigned n, const char *host, unsigned port, const char *code, bool notify)
{
    snprintf(out, n, "watchsync://pair?host=%s&port=%u&code=%s%s", host, port, code, notify ? "&mode=notify" : "");
}

lv_obj_t *pairing_qr_create(lv_obj_t *parent, const char *text, int size)
{
#if LV_USE_QRCODE
    lv_obj_t *qr = lv_qrcode_create(parent);
    lv_qrcode_set_size(qr, size);
    lv_qrcode_set_dark_color(qr, lv_color_black());
    lv_qrcode_set_light_color(qr, lv_color_white());
    lv_qrcode_update(qr, text, strlen(text));
    lv_obj_set_style_border_color(qr, lv_color_white(), 0);
    lv_obj_set_style_border_width(qr, 8, 0);
    return qr;
#else
    LV_UNUSED(size);
    lv_obj_t *l = lv_label_create(parent);     // QR отключён в lv_conf.h: показываем ссылку текстом
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_text(l, text);
    return l;
#endif
}
