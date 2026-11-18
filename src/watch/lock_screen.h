// lock_screen.h - экран блокировки: перекрывает всё (lv_layer_sys), включая приложения и диалоги.
#pragma once
#include <stdbool.h>

void lock_screen_init(void);        // один раз при старте: блокирует, если задан PIN, и запускает автоблокировку
void lock_screen_lock(void);        // заблокировать сейчас (если PIN задан)
bool lock_screen_is_locked(void);
