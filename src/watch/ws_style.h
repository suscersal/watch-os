// ws_style.h - единый стиль интерфейса в духе watchOS: чёрный фон, тёмные закруглённые карточки,
// круглые кнопки, цвет приложения в заголовке, время в правом верхнем углу.
#pragma once
#include <stdbool.h>
#include <time.h>
#include "lvgl_inc.h"

#define WS_BG      0x000000
#define WS_CARD    0x19191C   // карточки, поля ввода (цвета сняты с макета)
#define WS_FILL    0x19191C   // кнопки
#define WS_FILL2   0x303034   // нажатые элементы, дорожки ползунков, выключенный переключатель
#define WS_TEXT2   0x818189   // второстепенный текст
#define WS_GREEN   0x30D158
#define WS_RED     0xFF453A
#define WS_BLUE    0x0A84FF
#define WS_ORANGE  0xFF9F0A

// Цвет текущего приложения: им рисуются заголовок экрана, шапка таблицы, ползунок. Лаунчер ставит его при открытии приложения.
void     ws_set_accent(uint32_t rgb);
uint32_t ws_accent(void);

// Местное время для показа: время от телефона, а если его нет - системное (на ПК); false, если времени нет (ESP32 до синхронизации).
bool ws_now(struct tm *out);

// Метка "ЧЧ:ММ", обновляется раз в секунду сама; пустая, пока нет времени.
lv_obj_t *ws_time_label(lv_obj_t *parent);

// Тёмная закруглённая карточка во всю ширину (высота по содержимому).
lv_obj_t *ws_card(lv_obj_t *parent);

// Круглая кнопка диаметром d с подписью или символом.
lv_obj_t *ws_round_button(lv_obj_t *parent, int d, const char *text, uint32_t bg, uint32_t fg,
                          const lv_font_t *font, lv_event_cb_t cb, void *ud);

// Тёмный выпадающий список в общем стиле.
void ws_style_dropdown(lv_obj_t *dd);
