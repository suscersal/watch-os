// colorcode.h - "цветной код" 16x16 для ручного ввода данных на часы (без камеры).
// Телефон/компьютер рисует сетку из 4 цветов, человек повторяет её тапами на часах, ядро проверяет и разбирает.
//
// Сетка 16x16, клетка = 2 бита (0 R красный, 1 G зелёный, 2 B синий, 3 Y жёлтый).
//   клетки (строка 0..14, столбец 0..14): данные, 225 клеток = 56 байт (последняя клетка не используется, всегда 0);
//   столбец 15 и строка 15: чётность (XOR) так, что XOR любой строки и любого столбца из 16 клеток равен 0.
//   Одна неверная клетка находится и исправляется.
// Данные (56 байт): [тип][длина n][n байт][CRC-16/CCITT-FALSE от тип+длина+данные, старший байт первым][нули].
//   n <= 52. Клетки идут по строкам, в байте первой стоит старшая пара бит: байт = c0<<6 | c1<<4 | c2<<2 | c3.
// Тип 1 = Wi-Fi: данные = SSID, байт 0, пароль (UTF-8). Тот же код считает tools/colorcode.py.
#pragma once
#include <stddef.h>
#include <stdint.h>

#define CC_N          16
#define CC_CELLS      (CC_N * CC_N)
#define CC_DATA_BYTES 56
#define CC_MAX_DATA   52
#define CC_UNSET      0xFF     // клетка ещё не закрашена (только в вводе, в готовом коде такого нет)
#define CC_TYPE_WIFI  1

typedef enum {
    CC_OK = 0,        // чётность и CRC сошлись
    CC_EMPTY,         // есть незакрашенные клетки (info: сколько)
    CC_FIXABLE,       // ошибка в одной клетке: row/col - где она, fix - на что заменить значение клетки
    CC_BAD,           // чётность не сходится по нескольким клеткам (info: сколько строк/столбцов не сошлось) или не сошёлся CRC
} cc_status_t;

typedef struct {
    cc_status_t status;
    int         empty;       // CC_EMPTY: число незакрашенных клеток
    int         row, col;    // CC_FIXABLE: координаты клетки (0..15)
    uint8_t     fix;         // CC_FIXABLE: верное значение клетки
    int         bad_rows;    // CC_BAD: число строк, где чётность не сошлась
    int         bad_cols;    // CC_BAD: то же для столбцов
} cc_report_t;

// Кадр -> сетка. type, данные длиной n <= CC_MAX_DATA. 0 - успех, -1 - не помещается.
int cc_encode(uint8_t type, const uint8_t *data, size_t n, uint8_t cells[CC_CELLS]);

// Проверка введённой сетки (значения 0..3 или CC_UNSET). cells не меняется.
cc_report_t cc_check(const uint8_t cells[CC_CELLS]);

// Исправить клетку по отчёту CC_FIXABLE.
void cc_apply_fix(uint8_t cells[CC_CELLS], const cc_report_t *r);

// Сетка -> кадр. Вызывать после cc_check == CC_OK. 0 - успех, -1 - не сошёлся CRC или длина.
int cc_decode(const uint8_t cells[CC_CELLS], uint8_t *type, uint8_t data[CC_MAX_DATA], size_t *n);

// Wi-Fi: упаковать и разобрать. ssid не пустой, <= 32 байт; ssid+1+pass <= CC_MAX_DATA. 0 - успех, -1 - не помещается.
int cc_wifi_encode(const char *ssid, const char *pass, uint8_t cells[CC_CELLS]);
// out_ssid >= 33 байт, out_pass >= 64 байт. 0 - успех, -1 - кадр не Wi-Fi или битый.
int cc_wifi_decode(const uint8_t cells[CC_CELLS], char *out_ssid, char *out_pass);
