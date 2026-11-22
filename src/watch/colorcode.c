#include "colorcode.h"
#include <string.h>

#define DATA_N 15   // сторона области данных

static uint16_t crc16(const uint8_t *p, size_t n)   // CCITT-FALSE: poly 0x1021, init 0xFFFF
{
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        c ^= (uint16_t)p[i] << 8;
        for (int b = 0; b < 8; b++) c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
    }
    return c;
}

int cc_encode(uint8_t type, const uint8_t *data, size_t n, uint8_t cells[CC_CELLS])
{
    if (n > CC_MAX_DATA || (n && !data)) return -1;
    uint8_t buf[CC_DATA_BYTES];
    memset(buf, 0, sizeof buf);
    buf[0] = type;
    buf[1] = (uint8_t)n;
    if (n) memcpy(buf + 2, data, n);
    uint16_t c = crc16(buf, 2 + n);
    buf[2 + n] = (uint8_t)(c >> 8);
    buf[3 + n] = (uint8_t)(c & 0xFF);

    memset(cells, 0, CC_CELLS);
    for (int k = 0; k < DATA_N * DATA_N; k++) {
        int bit = 2 * k, v = 0;
        if (bit / 8 < CC_DATA_BYTES) v = (buf[bit / 8] >> (6 - bit % 8)) & 3;
        cells[(k / DATA_N) * CC_N + (k % DATA_N)] = (uint8_t)v;
    }
    for (int r = 0; r < DATA_N; r++) {                 // чётность строк -> столбец 15
        uint8_t x = 0;
        for (int c2 = 0; c2 < DATA_N; c2++) x ^= cells[r * CC_N + c2];
        cells[r * CC_N + 15] = x;
    }
    for (int c2 = 0; c2 < CC_N; c2++) {                // чётность столбцов -> строка 15 (включая угол)
        uint8_t x = 0;
        for (int r = 0; r < DATA_N; r++) x ^= cells[r * CC_N + c2];
        cells[15 * CC_N + c2] = x;
    }
    return 0;
}

cc_report_t cc_check(const uint8_t cells[CC_CELLS])
{
    cc_report_t rep;
    memset(&rep, 0, sizeof rep);
    for (int i = 0; i < CC_CELLS; i++) if (cells[i] > 3) rep.empty++;
    if (rep.empty) { rep.status = CC_EMPTY; return rep; }

    uint8_t sr[CC_N] = {0}, sc[CC_N] = {0};
    for (int r = 0; r < CC_N; r++)
        for (int c = 0; c < CC_N; c++) { sr[r] ^= cells[r * CC_N + c]; sc[c] ^= cells[r * CC_N + c]; }
    int nr = 0, nc = 0, r0 = -1, c0 = -1;
    for (int i = 0; i < CC_N; i++) {
        if (sr[i]) { nr++; r0 = i; }
        if (sc[i]) { nc++; c0 = i; }
    }
    if (nr == 0 && nc == 0) {
        uint8_t t, d[CC_MAX_DATA];
        size_t n;
        rep.status = cc_decode(cells, &t, d, &n) == 0 ? CC_OK : CC_BAD;
        return rep;
    }
    if (nr == 1 && nc == 1 && sr[r0] == sc[c0]) {
        rep.status = CC_FIXABLE;
        rep.row = r0;
        rep.col = c0;
        rep.fix = cells[r0 * CC_N + c0] ^ sr[r0];
        return rep;
    }
    rep.status = CC_BAD;
    rep.bad_rows = nr;
    rep.bad_cols = nc;
    return rep;
}

void cc_apply_fix(uint8_t cells[CC_CELLS], const cc_report_t *r)
{
    if (r->status == CC_FIXABLE) cells[r->row * CC_N + r->col] = r->fix;
}

int cc_decode(const uint8_t cells[CC_CELLS], uint8_t *type, uint8_t data[CC_MAX_DATA], size_t *n)
{
    uint8_t buf[CC_DATA_BYTES + 1];
    memset(buf, 0, sizeof buf);
    for (int k = 0; k < DATA_N * DATA_N; k++) {
        int bit = 2 * k;
        uint8_t v = cells[(k / DATA_N) * CC_N + (k % DATA_N)];
        if (v > 3) return -1;
        if (bit / 8 < CC_DATA_BYTES) buf[bit / 8] |= (uint8_t)(v << (6 - bit % 8));
    }
    size_t len = buf[1];
    if (len > CC_MAX_DATA) return -1;
    uint16_t c = crc16(buf, 2 + len);
    if (buf[2 + len] != (uint8_t)(c >> 8) || buf[3 + len] != (uint8_t)(c & 0xFF)) return -1;
    *type = buf[0];
    *n = len;
    memcpy(data, buf + 2, len);
    return 0;
}

int cc_wifi_encode(const char *ssid, const char *pass, uint8_t cells[CC_CELLS])
{
    size_t a = strlen(ssid), b = strlen(pass);
    if (a == 0 || a > 32 || b > 63 || a + 1 + b > CC_MAX_DATA) return -1;
    uint8_t d[CC_MAX_DATA];
    memcpy(d, ssid, a);
    d[a] = 0;
    memcpy(d + a + 1, pass, b);
    return cc_encode(CC_TYPE_WIFI, d, a + 1 + b, cells);
}

int cc_wifi_decode(const uint8_t cells[CC_CELLS], char *out_ssid, char *out_pass)
{
    uint8_t t, d[CC_MAX_DATA];
    size_t n;
    if (cc_decode(cells, &t, d, &n) != 0 || t != CC_TYPE_WIFI) return -1;
    size_t a = 0;
    while (a < n && d[a]) a++;
    if (a == 0 || a > 32 || a >= n) return -1;      // нет нуля-разделителя или пустое имя
    size_t b = n - a - 1;
    if (b > 63) return -1;
    memcpy(out_ssid, d, a);
    out_ssid[a] = 0;
    memcpy(out_pass, d + a + 1, b);
    out_pass[b] = 0;
    return 0;
}
