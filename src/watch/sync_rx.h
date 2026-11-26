// sync_rx.h - приёмная сторона протокола: разбор кадров, проверка кода, приём фото, установка времени.
// Не зависит от транспорта: байты подаются через sync_rx_feed(), ответы уходят через io.send().
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    void (*send)(const uint8_t *data, size_t n, void *ud);
    void *ud;
} sync_io_t;

typedef enum {
    SYNC_EV_CONNECTED,       // клиент подключился
    SYNC_EV_AUTH_OK,         // код верный
    SYNC_EV_AUTH_FAIL,       // код неверный
    SYNC_EV_TIME_SET,
    SYNC_EV_PHOTO,           // принято и сохранено фото
    SYNC_EV_PHOTO_DELETED,
    SYNC_EV_AUDIO,           // принят и сохранён аудиофайл (data/music)
    SYNC_EV_MESSAGE,         // принято уведомление (уже добавлено в messages)
    SYNC_EV_PAIRED,          // телефону выдан постоянный токен (список сопряжённых изменился)
    SYNC_EV_DISCONNECTED,
} sync_event_t;

typedef void (*sync_event_cb)(sync_event_t ev, void *ud);

void sync_rx_set_event_cb(sync_event_cb cb, void *ud);
// Постоянный код сопряжения для уведомлений (NULL - выключен). Сессия с этим кодом ограничена кадрами HELLO/TIME/MESSAGE/BYE
// и работает даже при блокировке PIN. Одноразовый код (sync_rx_open) даёт полный доступ, но не при блокировке.
void sync_rx_set_notify_code(const char *code);
void sync_rx_open(const char *code, const sync_io_t *io);   // началась новая сессия
void sync_rx_close(void);                                    // сессия завершена (разрыв или закрытие)
// Подаёт принятые байты. false - соединение нужно закрыть (ошибка протокола, 3 неверных кода, блокировка).
bool sync_rx_feed(const uint8_t *data, size_t n);
const char *sync_rx_close_reason(void);                     // короткий текст: почему последний кадр закрыл соединение
bool sync_rx_authenticated(void);
