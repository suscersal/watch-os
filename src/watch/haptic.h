// haptic.h - готовые узоры вибромотора (см. plat_haptic в plat.h).
#pragma once
#include "plat.h"

// Короткий "тик": подтверждение нажатия на управление таймером.
static inline void haptic_tick(void)
{
    static const uint16_t p[] = { 25 };
    plat_haptic(p, 1);
}

// Время таймера вышло: три импульса подряд.
static inline void haptic_timer_done(void)
{
    static const uint16_t p[] = { 200, 130, 200, 130, 200 };
    plat_haptic(p, 5);
}

// Будильник звонит: три длинных импульса (повторяется, пока не нажата "Стоп").
static inline void haptic_alarm(void)
{
    static const uint16_t p[] = { 250, 120, 250, 120, 250 };
    plat_haptic(p, 5);
}
