#include "ota.h"

static const ota_hw_t *g_hw;

void ota_set_hw(const ota_hw_t *hw) { g_hw = hw; }
bool ota_available(void) { return g_hw != NULL; }
bool ota_begin(uint32_t size) { return g_hw && g_hw->begin(size); }
int  ota_write(const uint8_t *d, size_t n) { return g_hw ? g_hw->write(d, n) : OTA_E_STORAGE; }
int  ota_end(void) { return g_hw ? g_hw->end() : OTA_E_STORAGE; }
void ota_abort(void) { if (g_hw) g_hw->abort(); }
void ota_restart_soon(void) { if (g_hw) g_hw->restart_soon(); }
