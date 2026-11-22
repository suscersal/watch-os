// audio.h - звук: воспроизведение WAV и запись с микрофона.
// Общая логика (позиция, пауза, перемотка, длительность, запись WAV) не зависит от платы. Плата подключается через audio_hw_t
// (ESP32: esp32/main/audio_esp.c). Без неё звук не звучит, но всё остальное работает "вхолостую", как в симуляторе на ПК.
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define AUDIO_REC_RATE 16000   // запись: 16 кГц, 16 бит, моно

typedef struct {
    uint32_t rate;
    uint16_t channels, bits;
    uint32_t data_off, data_bytes;
    uint32_t duration_ms;
} wav_info_t;

// Читает заголовок WAV (PCM). false - не WAV или не PCM.
bool audio_wav_info(const char *path, wav_info_t *out);

// ---- подключение платы ----
typedef struct {
    bool (*play_start)(const char *path, uint32_t start_ms);   // начать играть файл с позиции
    void (*play_stop)(void);
    void (*set_volume)(int pct);                               // 0..100
    bool (*rec_start)(const char *path);                       // писать WAV 16 кГц моно сюда (заголовок дописать при остановке)
    void (*rec_stop)(void);
    int  (*rec_level)(void);                                   // текущий уровень 0..100
    void (*rec_pause)(void);                                   // необязательно: приостановить запись (звук не писать). Без этих двух пауза недоступна
    void (*rec_resume)(void);
} audio_hw_t;
void audio_set_hw(const audio_hw_t *hw);

// ---- воспроизведение ----
// path может быть пустым или несуществующим файлом: тогда играет "виртуальный" трек длиной duration_hint_ms (демо без звука).
bool     audio_play(const char *path, uint32_t duration_hint_ms);
void     audio_pause(void);
void     audio_resume(void);
void     audio_stop(void);
void     audio_seek(uint32_t ms);
uint32_t audio_pos_ms(void);
uint32_t audio_duration_ms(void);
bool     audio_is_loaded(void);
bool     audio_is_playing(void);     // играет сейчас (не на паузе)
bool     audio_finished(void);       // дошёл до конца
void     audio_set_volume(int pct);
int      audio_get_volume(void);

// ---- запись ----
bool     audio_rec_start(const char *path);
void     audio_rec_stop(void);       // дописывает заголовок WAV
bool     audio_rec_active(void);
bool     audio_rec_can_pause(void);  // пауза возможна (на ПК всегда; на плате - если заданы rec_pause и rec_resume)
void     audio_rec_pause(void);      // время и запись замирают, уровень 0
void     audio_rec_resume(void);
bool     audio_rec_paused(void);
uint32_t audio_rec_ms(void);         // длина записи без пауз
int      audio_rec_level(void);      // 0..100
