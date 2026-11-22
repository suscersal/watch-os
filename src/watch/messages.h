// messages.h - уведомления с телефона (кадр MESSAGE протокола синхронизации). Хранятся в памяти, последние MSG_MAX;
// после перезагрузки список пуст (содержимое сообщений во флеш не пишем).
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MSG_MAX        20
#define MSG_APP_MAX    32     // байт в utf-8
#define MSG_TITLE_MAX  64
#define MSG_TEXT_MAX   256

typedef struct {
    uint32_t id;                       // идентификатор от телефона: то же id заменяет прежнее сообщение
    uint32_t ts;                       // epoch UTC; 0 - неизвестно
    char     app[MSG_APP_MAX + 1];
    char     title[MSG_TITLE_MAX + 1];
    char     text[MSG_TEXT_MAX + 1];
    bool     read;
} msg_t;

// Новое сообщение попадает в начало списка. Если часы не заблокированы PIN - всплывает тост, экран включается.
void          messages_add(uint32_t id, uint32_t ts, const char *app, const char *title, const char *text);
size_t        messages_count(void);
const msg_t  *messages_get(size_t i);            // 0 - самое новое; NULL вне диапазона
size_t        messages_unread(void);
void          messages_mark_read(size_t i);
void          messages_clear(void);
void          messages_set_change_cb(void (*cb)(void));   // вызывается после любого изменения списка
