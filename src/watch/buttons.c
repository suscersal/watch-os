#include "buttons.h"
#include "lock_screen.h"
#include "power.h"
#include "sys_ui.h"
#include "watchface.h"

static lv_obj_t *g_launcher;
static void (*g_hook)(btn_id_t b);

void buttons_set_test_hook(void (*cb)(btn_id_t b)) { g_hook = cb; }

void buttons_init(lv_obj_t *launcher) { g_launcher = launcher; }

void buttons_pwr_short(void)
{
    if (power_screen_off()) { power_wake(); return; }
    if (g_hook) { lv_display_trigger_activity(NULL); g_hook(BTN_PWR); return; }
    lock_screen_lock();          // без PIN ничего не делает
    power_sleep_now();
}

void buttons_boot_short(void)
{
    if (power_screen_off()) { power_wake(); return; }      // кнопка на выключенном экране только включает его
    if (g_hook) { lv_display_trigger_activity(NULL); g_hook(BTN_BOOT); return; }
    if (lock_screen_is_locked()) return;                     // под блокировкой навигации нет
    lv_obj_t *a = lv_screen_active();
    if (g_launcher && a == g_launcher) {
        if (!watchface_visible()) watchface_show(g_launcher);
    } else {
        sys_screen_exit(a);                                  // на вложенных экранах ничего не делает
    }
}
