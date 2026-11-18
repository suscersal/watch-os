// auth.h - PIN-код: хранение (соль + многократный SHA-256), проверка, лимит попыток, автоблокировка.
// Данные лежат в <data_dir>/auth.json и недоступны приложениям (у них нет файловых вызовов).
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define AUTH_PIN_MIN        4
#define AUTH_PIN_MAX        6
#define AUTH_FREE_ATTEMPTS  5     // после стольких неудач начинаются задержки 30 с, 60 с, 120 с ... до 15 мин

typedef enum { AUTH_OK, AUTH_WRONG, AUTH_LOCKED, AUTH_NO_PIN } auth_result_t;

void          auth_init(const char *data_dir);   // загрузить состояние; после перезапуска задержка сохраняется
bool          auth_has_pin(void);
int           auth_pin_len(void);                // длина сохранённого PIN (для автоввода), 0 если нет
bool          auth_set_pin(const char *pin);     // 4..6 цифр; новая соль
bool          auth_remove_pin(void);
auth_result_t auth_verify(const char *pin);      // каждая неудача сохраняется на диск
int           auth_attempts_left(void);          // до начала задержек
uint32_t      auth_lock_remaining_s(void);       // сколько секунд ждать, 0 - можно вводить
int           auth_timeout_s(void);              // автоблокировка при бездействии, 0 = никогда
bool          auth_set_timeout_s(int s);
