// sync_proto.h - протокол синхронизации часов с телефоном/ПК (версия 1). Описание: docs/sync-protocol.md.
// Кадр: [тип: 1 байт][длина: 4 байта, little-endian][данные]. Транспорт - любой надёжный поток байт (TCP по Wi-Fi, позже BLE).
#pragma once

#define SYNC_PROTO_VERSION 1
#define SYNC_PORT          8765
#define SYNC_CODE_LEN      6
#define SYNC_MAX_FRAME     20000      // данные одного кадра
#define SYNC_MSG_APP_MAX   32         // пределы полей MESSAGE, байт (см. messages.h)
#define SYNC_MSG_TITLE_MAX 64
#define SYNC_MSG_TEXT_MAX  256

// телефон -> часы
#define SYNC_T_HELLO         0x01     // proto u8, code[6] ASCII, имя устройства (utf8, до 32 байт)
#define SYNC_T_TIME          0x02     // epoch i64, tz_min i16
#define SYNC_T_HELLO_TOKEN   0x03     // вход без кода: proto u8, token[16], имя устройства (utf8, до 32 байт); токен выдан ранее (PAIR_REQ)
#define SYNC_T_PAIR_REQ      0x04     // после HELLO с одноразовым кодом: попросить постоянный токен; ответ TOKEN
#define SYNC_T_PHOTO_BEGIN   0x10     // id u32, taken u32, thumb_size u32, full_size u32, crc32 u32 (от миниатюры+полного)
#define SYNC_T_PHOTO_DATA    0x11     // сырые байты: сначала миниатюра, потом полный кадр
#define SYNC_T_PHOTO_END     0x12
#define SYNC_T_PHOTO_LIST    0x13
#define SYNC_T_PHOTO_DELETE  0x14     // id u32
#define SYNC_T_AUDIO_BEGIN   0x30     // id u32, size u32, crc32 u32 (от всего файла), name_len u8, имя (utf8, .wav)
#define SYNC_T_AUDIO_DATA    0x31     // сырые байты WAV, куски до 16384 байт
#define SYNC_T_AUDIO_END     0x32
#define SYNC_T_AUDIO_COVER   0x33     // после AUDIO_BEGIN (ответ READY), до DATA: обложка, baseline JFIF JPEG до 160x160, не больше 20000 байт
#define SYNC_AUDIO_MAX_BYTES (24u * 1024 * 1024)
#define SYNC_T_MESSAGE       0x20     // уведомление: id u32, ts u32, app_len u8, title_len u8, text_len u16, затем app, title, text (utf8)
#define SYNC_T_BYE           0x7F

// часы -> телефон
#define SYNC_T_OK            0x80     // req u8, status u8
#define SYNC_T_TOKEN         0x82     // token[16]: постоянный токен этого телефона (показывается один раз)
#define SYNC_T_HELLO_OK      0x81     // proto u8, count u16, max_photos u16, max_full_bytes u32
#define SYNC_T_LIST          0x91     // count u16, id u32 * count (новые первыми)
#define SYNC_T_ERR           0x8F     // req u8, code u8

// коды ошибок
#define SYNC_E_BAD_FRAME     1
#define SYNC_E_AUTH          2
#define SYNC_E_STATE         3        // команда в неподходящий момент (нет HELLO, нет BEGIN)
#define SYNC_E_TOO_BIG       4
#define SYNC_E_CRC           5
#define SYNC_E_FORMAT        6        // не baseline JPEG или неподходящий размер
#define SYNC_E_STORAGE       7        // нет места или запись не удалась
#define SYNC_E_UNSUPPORTED   8
#define SYNC_E_LOCKED        9        // часы заблокированы PIN-кодом
#define SYNC_E_SCOPE         10       // команда не разрешена для этого кода (постоянный код пускает только TIME и MESSAGE)

// статус в SYNC_T_OK для PHOTO_BEGIN
#define SYNC_S_READY         0        // можно слать данные
#define SYNC_S_HAVE          1        // такое фото уже есть, данные слать не нужно
