// pairing.h - адрес часов для телефона: ссылка watchsync://pair?... и её QR-код.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "lvgl_inc.h"

// watchsync://pair?host=192.168.0.5&port=8765&code=123456[&mode=notify]
void pairing_url(char *out, unsigned n, const char *host, unsigned port, const char *code, bool notify);

// QR-код с белой рамкой (без неё тёмный экран мешает сканеру). size - сторона в пикселях.
lv_obj_t *pairing_qr_create(lv_obj_t *parent, const char *text, int size);
