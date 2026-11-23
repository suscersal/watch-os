/**
 * @file main.c
 *
 */

/*********************
 *      INCLUDES
 *********************/

#ifndef _DEFAULT_SOURCE
  #define _DEFAULT_SOURCE /* needed for usleep() */
#endif

#include <stdlib.h>
#include <stdio.h>
#ifdef _MSC_VER
  #include <Windows.h>
#else
  #include <unistd.h>
  #include <pthread.h>
#endif
#include "lvgl/lvgl.h"
#include "lvgl/examples/lv_examples.h"

void watch_launcher_create(void);
#include "watch/buttons.h"
#include "watch/clock.h"
#include "watch/watchface.h"
#include <SDL.h>

#include "hal/hal.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

#if LV_USE_OS != LV_OS_FREERTOS

/* Кнопки платы в симуляторе: P - PWR (экран выкл/вкл и блокировка), H - BOOT (выход из приложения / домой). */
static int key_watch(void *ud, SDL_Event *ev)
{
  (void)ud;
  if (ev->type != SDL_KEYDOWN || ev->key.repeat) return 0;
  if (ev->key.keysym.sym == SDLK_p) buttons_pwr_short();
  if (ev->key.keysym.sym == SDLK_h) buttons_boot_short();
  return 0;
}

int main(int argc, char **argv)
{
  (void)argc; /*Unused*/
  (void)argv; /*Unused*/

  /* Заряд для строки состояния: на ПК батареи нет, показываем пробную (WATCH_BATTERY=40 или 40+ - заряжается; WATCH_BATTERY= - скрыть) */
#ifndef _MSC_VER
  setenv("WATCH_BATTERY", "85", 0);
#endif

  /*Initialize LVGL*/
  lv_init();

  /*Initialize the HAL (display, input devices, tick) for LVGL*/
  sdl_hal_init(410, 502);
  SDL_AddEventWatch(key_watch, NULL);   /* клавиши P и H имитируют кнопки PWR и BOOT */

  /* Run the default demo */
  /* To try a different demo or example, replace this with one of: */
  /* - lv_demo_benchmark(); */
  /* - lv_demo_stress(); */
  /* - lv_example_label_1(); */
  /* - etc. */
  watch_launcher_create();
  clock_init();                          /* время из RTC (на ПК - системное), пояс из настроек */
  watchface_show(lv_screen_active());    /* после включения сразу циферблат; свайп вверх - сетка приложений */

  while(1) {
    /* Periodically call the lv_task handler.
     * It could be done in a timer interrupt or an OS task too.*/
    uint32_t sleep_time_ms = lv_timer_handler();
    if(sleep_time_ms == LV_NO_TIMER_READY){
	sleep_time_ms =  LV_DEF_REFR_PERIOD;
    }
#ifdef _MSC_VER
    Sleep(sleep_time_ms);
#else
    usleep(sleep_time_ms * 1000);
#endif
  }

  return 0;
}


#endif

/**********************
 *   STATIC FUNCTIONS
 **********************/

