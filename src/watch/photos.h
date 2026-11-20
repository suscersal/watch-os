// photos.h - хранилище фото, полученных с телефона. Для каждого фото два JPEG-файла в папке data/photos:
//   <снято, hex8>_<id, hex8>.jpg    полный кадр (до 410x502)
//   <снято, hex8>_<id, hex8>.t.jpg  миниатюра (до 160x160) для сетки галереи
// Имя начинается со времени съёмки, поэтому сортировка по имени = по дате.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PHOTOS_MAX          100            // больше фото не принимаем, пока пользователь не удалит часть
#define PHOTO_FULL_MAX_W    410
#define PHOTO_FULL_MAX_H    502
#define PHOTO_THUMB_MAX     160
#define PHOTO_FULL_MAX_BYTES  (256 * 1024)
#define PHOTO_THUMB_MAX_BYTES (32 * 1024)

void   photos_init(const char *dir);                         // запоминает папку и создаёт её
size_t photos_count(void);
// Новые первыми. Возвращает число записанных элементов (не больше max).
int    photos_list(uint32_t *ids, uint32_t *taken, int max);
bool   photos_exists(uint32_t id);
bool   photos_store(uint32_t id, uint32_t taken, const uint8_t *thumb, size_t thumb_n, const uint8_t *full, size_t full_n);
uint8_t *photos_load(uint32_t id, bool thumb, size_t *len);  // malloc-буфер, освобождает вызывающий; NULL при ошибке
bool   photos_delete(uint32_t id);

// Проверка JPEG: заголовок JFIF, baseline (SOF0), размер не больше max_w x max_h (w и h можно передать NULL).
// Декодер LVGL (TJpgDec) прогрессивные JPEG не читает, поэтому они отклоняются.
bool   photos_check_jpeg(const uint8_t *d, size_t n, int max_w, int max_h, int *w, int *h);
