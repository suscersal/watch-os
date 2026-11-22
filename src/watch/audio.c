#include "audio.h"
#include "fs.h"
#include "lvgl_inc.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static const audio_hw_t *g_hw;
void audio_set_hw(const audio_hw_t *hw) { g_hw = hw; if (hw && hw->set_volume) hw->set_volume(70); }

// ---------- WAV ----------
static uint32_t rd32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static void wr32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static void wr16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }

bool audio_wav_info(const char *path, wav_info_t *out)
{
    uint8_t h[512];
    long n = fs_read_range(path, 0, h, sizeof h);
    if (n < 44 || memcmp(h, "RIFF", 4) != 0 || memcmp(h + 8, "WAVE", 4) != 0) return false;
    wav_info_t w = { 0 };
    bool have_fmt = false;
    size_t pos = 12;
    long fsize = fs_file_size(path);
    while (pos + 8 <= (size_t)n) {
        uint32_t sz = rd32(h + pos + 4);
        if (memcmp(h + pos, "fmt ", 4) == 0 && pos + 8 + 16 <= (size_t)n) {
            if (rd16(h + pos + 8) != 1) return false;              // только PCM
            w.channels = rd16(h + pos + 10);
            w.rate = rd32(h + pos + 12);
            w.bits = rd16(h + pos + 22);
            have_fmt = true;
        } else if (memcmp(h + pos, "data", 4) == 0) {
            w.data_off = (uint32_t)(pos + 8);
            long avail = fsize - (long)w.data_off;
            w.data_bytes = (avail >= 0 && sz > (uint32_t)avail) ? (uint32_t)avail : sz;   // запись могла оборваться
            break;
        }
        pos += 8 + sz + (sz & 1);
    }
    if (!have_fmt || w.data_off == 0 || w.rate == 0 || w.channels == 0 || w.bits == 0) return false;
    uint64_t bps = (uint64_t)w.rate * w.channels * (w.bits / 8);
    w.duration_ms = bps ? (uint32_t)((uint64_t)w.data_bytes * 1000 / bps) : 0;
    if (out) *out = w;
    return true;
}

static void wav_header(uint8_t *h, uint32_t data_bytes)
{
    memcpy(h, "RIFF", 4);  wr32(h + 4, 36 + data_bytes);
    memcpy(h + 8, "WAVEfmt ", 8);
    wr32(h + 16, 16); wr16(h + 20, 1); wr16(h + 22, 1);
    wr32(h + 24, AUDIO_REC_RATE); wr32(h + 28, AUDIO_REC_RATE * 2);
    wr16(h + 32, 2); wr16(h + 34, 16);
    memcpy(h + 36, "data", 4); wr32(h + 40, data_bytes);
}

// ---------- воспроизведение ----------
static struct {
    bool     loaded, playing, finished;
    char     path[128];
    uint32_t dur_ms, base_ms, t0;
    int      volume;
} P = { .volume = 70 };

static uint32_t play_pos_raw(void)
{
    if (!P.playing) return P.base_ms;
    return P.base_ms + (lv_tick_get() - P.t0);
}

static void play_check_end(void)
{
    if (P.loaded && P.playing && play_pos_raw() >= P.dur_ms) {
        P.base_ms = P.dur_ms;
        P.playing = false;
        P.finished = true;
        if (g_hw && g_hw->play_stop) g_hw->play_stop();
    }
}

static void hw_play_from(uint32_t ms)
{
    if (g_hw && g_hw->play_start && P.path[0]) g_hw->play_start(P.path, ms);
}

bool audio_play(const char *path, uint32_t duration_hint_ms)
{
    audio_stop();
    memset(&P.path, 0, sizeof P.path);
    wav_info_t w;
    if (path && path[0]) snprintf(P.path, sizeof P.path, "%s", path);
    P.dur_ms = (P.path[0] && audio_wav_info(P.path, &w)) ? w.duration_ms : duration_hint_ms;
    if (P.dur_ms == 0) return false;
    P.loaded = true; P.finished = false; P.playing = true;
    P.base_ms = 0; P.t0 = lv_tick_get();
    hw_play_from(0);
    return true;
}

void audio_pause(void)
{
    if (!P.loaded || !P.playing) return;
    play_check_end();
    if (!P.playing) return;
    P.base_ms = play_pos_raw();
    P.playing = false;
    if (g_hw && g_hw->play_stop) g_hw->play_stop();
}

void audio_resume(void)
{
    if (!P.loaded || P.playing) return;
    if (P.finished) { P.base_ms = 0; P.finished = false; }
    P.playing = true; P.t0 = lv_tick_get();
    hw_play_from(P.base_ms);
}

void audio_stop(void)
{
    if (g_hw && g_hw->play_stop && P.loaded) g_hw->play_stop();
    P.loaded = P.playing = P.finished = false;
    P.base_ms = 0;
}

void audio_seek(uint32_t ms)
{
    if (!P.loaded) return;
    if (ms > P.dur_ms) ms = P.dur_ms;
    bool was = P.playing;
    P.base_ms = ms; P.t0 = lv_tick_get();
    P.finished = false;
    if (was) { if (g_hw && g_hw->play_stop) g_hw->play_stop(); hw_play_from(ms); }
}

uint32_t audio_pos_ms(void)       { play_check_end(); uint32_t p = play_pos_raw(); return p > P.dur_ms ? P.dur_ms : p; }
uint32_t audio_duration_ms(void)  { return P.loaded ? P.dur_ms : 0; }
bool     audio_is_loaded(void)    { return P.loaded; }
bool     audio_is_playing(void)   { play_check_end(); return P.loaded && P.playing; }
bool     audio_finished(void)     { play_check_end(); return P.finished; }
void     audio_set_volume(int pct)
{
    P.volume = pct < 0 ? 0 : pct > 100 ? 100 : pct;
    if (g_hw && g_hw->set_volume) g_hw->set_volume(P.volume);
}
int      audio_get_volume(void)   { return P.volume; }

// ---------- запись ----------
static struct {
    bool     active;
    char     path[128];
    uint32_t t0, written_ms, data_bytes;
    lv_timer_t *sim;          // без платы: сами пишем тихий синтетический звук, чтобы получился настоящий WAV
    int      level;
    uint32_t phase;
} R;

static void sim_tick(lv_timer_t *t)
{
    LV_UNUSED(t);
    uint32_t now_ms = lv_tick_get() - R.t0;
    if (now_ms <= R.written_ms) return;
    uint32_t add_ms = now_ms - R.written_ms;
    if (add_ms > 500) add_ms = 500;                                   // за один раз не больше полусекунды
    uint32_t samples = (uint32_t)((uint64_t)add_ms * AUDIO_REC_RATE / 1000);
    int16_t buf[400];
    while (samples) {
        uint32_t k = samples > 400 ? 400 : samples;
        // "голос": синус 220 Гц, громкость плавно меняется
        for (uint32_t i = 0; i < k; i++) {
            double ph = (double)(R.phase++) / AUDIO_REC_RATE;
            double env = 0.5 + 0.5 * sin(ph * 3.0);
            buf[i] = (int16_t)(sin(ph * 2 * M_PI * 220.0) * 8000.0 * env);
        }
        if (!fs_append_file(R.path, buf, k * 2)) break;
        R.data_bytes += k * 2;
        samples -= k;
    }
    R.written_ms = now_ms;
    R.level = (int)(30 + 55 * (0.5 + 0.5 * sin((double)now_ms / 400.0)));
}

bool audio_rec_start(const char *path)
{
    if (R.active) return false;
    memset(&R, 0, sizeof R);
    snprintf(R.path, sizeof R.path, "%s", path);
    R.t0 = lv_tick_get();
    if (g_hw && g_hw->rec_start) {
        if (!g_hw->rec_start(R.path)) return false;
    } else {
        uint8_t h[44];
        wav_header(h, 0);
        fs_remove(R.path);
        if (!fs_append_file(R.path, h, sizeof h)) return false;
        R.sim = lv_timer_create(sim_tick, 50, NULL);
    }
    R.active = true;
    return true;
}

void audio_rec_stop(void)
{
    if (!R.active) return;
    if (g_hw && g_hw->rec_stop) {
        g_hw->rec_stop();
    } else {
        if (R.sim) { sim_tick(R.sim); lv_timer_delete(R.sim); R.sim = NULL; }
        uint8_t b[4];
        wr32(b, 36 + R.data_bytes); fs_write_at(R.path, 4, b, 4);
        wr32(b, R.data_bytes);      fs_write_at(R.path, 40, b, 4);
    }
    R.active = false;
}

bool     audio_rec_active(void) { return R.active; }
uint32_t audio_rec_ms(void)     { return R.active ? lv_tick_get() - R.t0 : 0; }
int      audio_rec_level(void)
{
    if (!R.active) return 0;
    return (g_hw && g_hw->rec_level) ? g_hw->rec_level() : R.level;
}
