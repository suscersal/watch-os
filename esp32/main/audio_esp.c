// audio_esp.c - звук на плате через официальный BSP Waveshare и esp_codec_dev.
//   Пины (из BSP): I2C SCL14/SDA15, I2S MCLK16 BCLK41 WS45 DOUT40 DIN42, усилитель динамика GPIO46 (включает BSP).
//   Шина I2S у BSP одна и работает на 24 кГц, поэтому:
//     воспроизведение: любой 16-битный WAV (8 кГц и выше, моно/стерео) -> 24 кГц стерео;
//     запись: микрофон 1 (слот 0 ES7210) 24 кГц -> 16 кГц моно, как ждёт watch/audio (AUDIO_REC_RATE).
#include "audio_esp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_codec_dev.h"
#include "bsp/esp32_s3_touch_amoled_2_06.h"
#include "watch/audio.h"
#include "driver/i2s_std.h"

static const char *TAG = "audio_esp";

#define BUS_RATE     24000
#define CHUNK        480            // 20 мс при 24 кГц
#define MIC_GAIN_DB  24.0f
#define MIC_MASK     0x01           // слот/микрофон 1

static esp_codec_dev_handle_t s_spk, s_mic;
static volatile int s_vol = 70;

// ---------- простой линейный ресемплер (моно, дробный шаг 16.16) ----------
typedef struct { uint32_t step, acc; int16_t prev; } rs_t;

static void rs_init(rs_t *r, uint32_t in_rate, uint32_t out_rate)
{
    r->step = (uint32_t)(((uint64_t)in_rate << 16) / out_rate);
    r->acc = 0;
    r->prev = 0;
}

// Возвращает число выходных отсчётов. out должен вмещать n * in_rate / out_rate + 2.
static size_t rs_run(rs_t *r, const int16_t *in, size_t n, int16_t *out)
{
    size_t o = 0;
    while (((r->acc >> 16) + 1) <= n) {
        uint32_t i = r->acc >> 16, f = r->acc & 0xFFFF;
        int32_t a = i == 0 ? r->prev : in[i - 1];
        int32_t b = in[i];
        out[o++] = (int16_t)(a + (((b - a) * (int32_t)f) >> 16));
        r->acc += r->step;
    }
    r->acc -= (uint32_t)n << 16;
    r->prev = in[n - 1];
    return o;
}

// ---------- воспроизведение ----------
typedef struct { FILE *f; uint32_t rate; uint16_t channels; uint32_t remain; } play_t;
static volatile bool s_play_stop, s_play_alive;

static void play_task(void *arg)
{
    play_t *p = arg;
    const size_t frame = 2u * p->channels;
    const size_t max_out = (size_t)CHUNK * BUS_RATE / p->rate + 4;
    uint8_t *raw = malloc(CHUNK * frame);
    int16_t *mono = malloc(CHUNK * sizeof(int16_t));
    int16_t *res = malloc(max_out * sizeof(int16_t));
    int16_t *st = malloc(max_out * 2 * sizeof(int16_t));
    rs_t rs;
    rs_init(&rs, p->rate, BUS_RATE);

    while (raw && mono && res && st && !s_play_stop && p->remain) {
        size_t want = p->remain < CHUNK * frame ? p->remain : CHUNK * frame;
        size_t got = fread(raw, 1, want, p->f);
        size_t n = got / frame;
        if (n == 0) break;
        p->remain -= (uint32_t)(n * frame);
        const int16_t *s16 = (const int16_t *)raw;
        for (size_t i = 0; i < n; i++)
            mono[i] = p->channels == 1 ? s16[i] : (int16_t)(((int32_t)s16[2 * i] + s16[2 * i + 1]) / 2);
        size_t m = rs_run(&rs, mono, n, res);
        for (size_t i = 0; i < m; i++) st[2 * i] = st[2 * i + 1] = res[i];
        if (m && esp_codec_dev_write(s_spk, st, (int)(m * 4)) != ESP_CODEC_DEV_OK) break;
    }
    free(raw); free(mono); free(res); free(st);
    esp_codec_dev_close(s_spk);
    fclose(p->f);
    free(p);
    s_play_alive = false;
    vTaskDelete(NULL);
}

static void hw_play_stop(void)
{
    if (!s_play_alive) return;
    s_play_stop = true;
    for (int i = 0; i < 100 && s_play_alive; i++) vTaskDelay(pdMS_TO_TICKS(10));
}

static bool hw_play_start(const char *path, uint32_t start_ms)
{
    hw_play_stop();
    wav_info_t wi;
    if (!audio_wav_info(path, &wi) || wi.bits != 16 || wi.channels < 1 || wi.channels > 2 || wi.rate < 8000) {
        ESP_LOGW(TAG, "WAV не подходит: %s", path);
        return false;
    }
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint32_t frame = 2u * wi.channels;
    uint64_t skip = (uint64_t)start_ms * wi.rate / 1000 * frame;
    if (skip > wi.data_bytes) skip = wi.data_bytes;
    fseek(f, (long)(wi.data_off + skip), SEEK_SET);

    esp_codec_dev_sample_info_t fs = { .bits_per_sample = 16, .channel = 2, .sample_rate = BUS_RATE };
    if (esp_codec_dev_open(s_spk, &fs) != ESP_CODEC_DEV_OK) { fclose(f); return false; }
    esp_codec_dev_set_out_mute(s_spk, false);
    esp_codec_dev_set_out_vol(s_spk, s_vol);

    play_t *p = malloc(sizeof *p);
    if (!p) { esp_codec_dev_close(s_spk); fclose(f); return false; }
    *p = (play_t){ .f = f, .rate = wi.rate, .channels = wi.channels, .remain = (uint32_t)(wi.data_bytes - skip) };
    s_play_stop = false;
    s_play_alive = true;
    if (xTaskCreate(play_task, "play", 4096, p, 5, NULL) != pdPASS) {
        s_play_alive = false;
        esp_codec_dev_close(s_spk); fclose(f); free(p);
        return false;
    }
    return true;
}

static void hw_set_volume(int pct)
{
    s_vol = pct;
    if (s_play_alive) esp_codec_dev_set_out_vol(s_spk, pct);   // на закрытом устройстве не нужно: громкость ставится при открытии
}

// ---------- запись ----------
typedef struct { FILE *f; } rec_t;
static volatile bool s_rec_stop, s_rec_pause, s_rec_alive;
static volatile int s_level;

static void put32(uint8_t *b, uint32_t v) { b[0] = v; b[1] = v >> 8; b[2] = v >> 16; b[3] = v >> 24; }

static void wav_header(uint8_t *h, uint32_t data_bytes)
{
    memcpy(h, "RIFF", 4);  put32(h + 4, 36 + data_bytes);
    memcpy(h + 8, "WAVEfmt ", 8);  put32(h + 16, 16);
    h[20] = 1; h[21] = 0;                       // PCM
    h[22] = 1; h[23] = 0;                       // моно
    put32(h + 24, AUDIO_REC_RATE);
    put32(h + 28, AUDIO_REC_RATE * 2);
    h[32] = 2; h[33] = 0; h[34] = 16; h[35] = 0;
    memcpy(h + 36, "data", 4);  put32(h + 40, data_bytes);
}

static void rec_task(void *arg)
{
    rec_t *r = arg;
    int16_t *in = malloc(CHUNK * sizeof(int16_t));
    int16_t *out = malloc((CHUNK * AUDIO_REC_RATE / BUS_RATE + 4) * sizeof(int16_t));
    rs_t rs;
    rs_init(&rs, BUS_RATE, AUDIO_REC_RATE);
    uint32_t bytes = 0;

    while (in && out && !s_rec_stop) {
        if (esp_codec_dev_read(s_mic, in, CHUNK * sizeof(int16_t)) != ESP_CODEC_DEV_OK) break;
        if (s_rec_pause) { s_level = 0; continue; }              // на паузе читаем и выбрасываем, чтобы шина не переполнялась
        int peak = 0;
        for (int i = 0; i < CHUNK; i++) { int v = in[i] < 0 ? -in[i] : in[i]; if (v > peak) peak = v; }
        int lvl = peak * 100 / 12000;
        if (lvl > 100) lvl = 100;
        s_level = lvl > s_level ? lvl : s_level * 3 / 4;
        size_t m = rs_run(&rs, in, CHUNK, out);
        if (fwrite(out, 2, m, r->f) != m) break;
        bytes += (uint32_t)(m * 2);
    }
    uint8_t h[44];
    wav_header(h, bytes);
    fseek(r->f, 0, SEEK_SET);
    fwrite(h, 1, sizeof h, r->f);
    fclose(r->f);
    esp_codec_dev_close(s_mic);
    free(in); free(out); free(r);
    s_level = 0;
    s_rec_alive = false;
    vTaskDelete(NULL);
}

static bool hw_rec_start(const char *path)
{
    if (s_rec_alive) return false;
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    uint8_t h[44];
    wav_header(h, 0);
    fwrite(h, 1, sizeof h, f);

    esp_codec_dev_sample_info_t fs = { .bits_per_sample = 16, .channel = 1, .channel_mask = MIC_MASK, .sample_rate = BUS_RATE };
    if (esp_codec_dev_open(s_mic, &fs) != ESP_CODEC_DEV_OK) { fclose(f); return false; }
    esp_codec_dev_set_in_channel_gain(s_mic, MIC_MASK, MIC_GAIN_DB);

    rec_t *r = malloc(sizeof *r);
    if (!r) { esp_codec_dev_close(s_mic); fclose(f); return false; }
    r->f = f;
    s_rec_stop = s_rec_pause = false;
    s_level = 0;
    s_rec_alive = true;
    if (xTaskCreate(rec_task, "rec", 4096, r, 5, NULL) != pdPASS) {
        s_rec_alive = false;
        esp_codec_dev_close(s_mic); fclose(f); free(r);
        return false;
    }
    return true;
}

static void hw_rec_stop(void)
{
    if (!s_rec_alive) return;
    s_rec_stop = true;
    for (int i = 0; i < 100 && s_rec_alive; i++) vTaskDelay(pdMS_TO_TICKS(10));
}

static int  hw_rec_level(void)  { return s_level; }
static void hw_rec_pause(void)  { s_rec_pause = true; }
static void hw_rec_resume(void) { s_rec_pause = false; }

esp_err_t audio_esp_init(void)
{
    const i2s_std_config_t i2s_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(BUS_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BSP_I2S_MCLK,
            .bclk = BSP_I2S_SCLK,
            .ws   = BSP_I2S_LCLK,
            .dout = BSP_I2S_DOUT,
            .din  = BSP_I2S_DSIN,
        },
    };
    esp_err_t e = bsp_audio_init(&i2s_cfg);
    if (e != ESP_OK) { ESP_LOGE(TAG, "аудиошина: %s", esp_err_to_name(e)); return e; }

    s_spk = bsp_audio_codec_speaker_init();
    s_mic = bsp_audio_codec_microphone_init();
    if (!s_spk || !s_mic) { ESP_LOGE(TAG, "кодек динамика/микрофона не создан"); return ESP_FAIL; }

    static const audio_hw_t hw = {
        .play_start = hw_play_start, .play_stop = hw_play_stop, .set_volume = hw_set_volume,
        .rec_start = hw_rec_start, .rec_stop = hw_rec_stop, .rec_level = hw_rec_level,
        .rec_pause = hw_rec_pause, .rec_resume = hw_rec_resume,
    };
    audio_set_hw(&hw);
    return ESP_OK;
}
