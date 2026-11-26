// fake_ota.h - подставная платформа обновления прошивки для проверок протокола на ПК: пишет образ в fw_received.bin.
// Образом считается всё, что начинается с байта 0xE9 (как у настоящих образов ESP).
#pragma once
#include "watch/ota.h"
#include <stdio.h>

static FILE *fo_f;
static unsigned long fo_total;
static int fo_restarts, fo_aborts;
static bool fo_fail_begin;                 // эмуляция "нет OTA-слота"

static bool fo_begin(uint32_t s) { (void)s; if (fo_fail_begin) return false; if (fo_f) fclose(fo_f); fo_f = fopen("fw_received.bin.part", "wb"); fo_total = 0; return fo_f != NULL; }
static int  fo_write(const uint8_t *d, size_t n)
{
    if (!fo_f) return OTA_E_STORAGE;
    if (fo_total == 0 && n && d[0] != 0xE9) return OTA_E_FORMAT;
    fwrite(d, 1, n, fo_f); fo_total += n;
    return OTA_OK;
}
static int  fo_end(void) { fclose(fo_f); fo_f = NULL; return rename("fw_received.bin.part", "fw_received.bin") == 0 ? OTA_OK : OTA_E_STORAGE; }
static void fo_abort(void) { if (fo_f) { fclose(fo_f); fo_f = NULL; } remove("fw_received.bin.part"); fo_aborts++; }
static void fo_restart(void) { fo_restarts++; }
static const ota_hw_t FAKE_OTA = { fo_begin, fo_write, fo_end, fo_abort, fo_restart };
