// colorcode_test.c - проверка цветного кода: эталонные сетки из tools/colorcode.py, круг туда и обратно,
// исправление любой одной ошибочной клетки, обнаружение двух ошибок, границы размера.
//   cmake --build build --target colorcode_test
#include "../src/watch/colorcode.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static const char *REF1[16] = {   // python3 tools/colorcode.py --ssid Home --password pass1234 --hex
    "RRRGRRYGGRBRGBYR",
    "YGBYGGBGGRRRRGYY",
    "RRGBRGGYRYGYRYRB",
    "YRGRYRBRYRYRYGRG",
    "RBGRBRGYRRRRRRRY",
    "RRRRRRRRRRRRRRRR",
    "RRRRRRRRRRRRRRRR",
    "RRRRRRRRRRRRRRRR",
    "RRRRRRRRRRRRRRRR",
    "RRRRRRRRRRRRRRRR",
    "RRRRRRRRRRRRRRRR",
    "RRRRRRRRRRRRRRRR",
    "RRRRRRRRRRRRRRRR",
    "RRRRRRRRRRRRRRRR",
    "RRRRRRRRRRRRRRRR",
    "RYYRRRYRYYRYBGRY"
};
static const char *REF2[16] = {   // python3 tools/colorcode.py --ssid "Кафе Wi-Fi" --password "пароль12345678" --hex
    "RRRGRBRYYGRRBGBY",
    "BYGRRBYRRYGRGBRR",
    "GRYGRRBYGGRBRRGG",
    "GGYGBBGRBYGGRGBG",
    "GBBGRRRRYGRRBYYR",
    "YYGRRBYRRYGRGBRG",
    "RRYGRRBYYBYGRRBB",
    "YBYYGRGBRYRRYRGB",
    "RYRBRYRYRYGRRYGG",
    "GRYGBRYGYRYBRBBG",
    "GBGYGBRRRRRRRRRB",
    "RRRRRRRRRRRRRRRR",
    "RRRRRRRRRRRRRRRR",
    "RRRRRRRRRRRRRRRR",
    "RRRRRRRRRRRRRRRR",
    "YRRBRGYYYRRRYBBR"
};

static void from_rows(const char *const rows[16], uint8_t cells[CC_CELLS])
{
    for (int r = 0; r < 16; r++)
        for (int c = 0; c < 16; c++) cells[r * 16 + c] = (uint8_t)(strchr("RGBY", rows[r][c]) - "RGBY");
}

static void same_as_ref(const char *ssid, const char *pass, const char *const rows[16])
{
    uint8_t got[CC_CELLS], ref[CC_CELLS];
    CHECK(cc_wifi_encode(ssid, pass, got) == 0);
    from_rows(rows, ref);
    CHECK(memcmp(got, ref, CC_CELLS) == 0);
    CHECK(cc_check(ref).status == CC_OK);
    char s[33], p[64];
    CHECK(cc_wifi_decode(ref, s, p) == 0);
    CHECK(strcmp(s, ssid) == 0 && strcmp(p, pass) == 0);
}

int main(void)
{
    same_as_ref("Home", "pass1234", REF1);
    same_as_ref("\xD0\x9A\xD0\xB0\xD1\x84\xD0\xB5 Wi-Fi", "\xD0\xBF\xD0\xB0\xD1\x80\xD0\xBE\xD0\xBB\xD1\x8C" "12345678", REF2);

    // одна неверная клетка: находится и исправляется (любая клетка, любое неверное значение)
    uint8_t good[CC_CELLS], bad[CC_CELLS];
    from_rows(REF1, good);
    int fixed = 0;
    for (int i = 0; i < CC_CELLS; i++)
        for (int d = 1; d <= 3; d++) {
            memcpy(bad, good, sizeof bad);
            bad[i] = (uint8_t)(bad[i] ^ d);
            cc_report_t r = cc_check(bad);
            if (r.status == CC_FIXABLE && r.row * 16 + r.col == i && r.fix == good[i]) {
                cc_apply_fix(bad, &r);
                if (memcmp(bad, good, sizeof bad) == 0 && cc_check(bad).status == CC_OK) fixed++;
            }
        }
    CHECK(fixed == CC_CELLS * 3);

    // две ошибки: код не принимается и "исправлением" не становится верным
    int accepted = 0;
    for (int i = 0; i < CC_CELLS; i++)
        for (int j = i + 1; j < CC_CELLS; j++) {
            memcpy(bad, good, sizeof bad);
            bad[i] ^= 1; bad[j] ^= 2;
            cc_report_t r = cc_check(bad);
            if (r.status == CC_OK) accepted++;
            if (r.status == CC_FIXABLE) { cc_apply_fix(bad, &r); if (cc_check(bad).status == CC_OK) accepted++; }
        }
    CHECK(accepted == 0);

    // незакрашенные клетки
    memcpy(bad, good, sizeof bad);
    bad[5] = bad[200] = CC_UNSET;
    cc_report_t r = cc_check(bad);
    CHECK(r.status == CC_EMPTY && r.empty == 2);

    // размеры: ровно 52 байта помещаются, 53 - нет
    uint8_t cells[CC_CELLS];
    char ssid[33], pass[64], s2[33], p2[64];
    memset(ssid, 'a', 10); ssid[10] = 0;
    memset(pass, 'b', 41); pass[41] = 0;                 /* 10 + 1 + 41 = 52 */
    CHECK(cc_wifi_encode(ssid, pass, cells) == 0 && cc_check(cells).status == CC_OK);
    CHECK(cc_wifi_decode(cells, s2, p2) == 0 && strcmp(s2, ssid) == 0 && strcmp(p2, pass) == 0);
    pass[41] = 'b'; pass[42] = 0;                        /* 53 */
    CHECK(cc_wifi_encode(ssid, pass, cells) == -1);
    CHECK(cc_wifi_encode("", "x", cells) == -1);         /* пустое имя */
    CHECK(cc_wifi_encode("net", "", cells) == 0 && cc_wifi_decode(cells, s2, p2) == 0 && strcmp(s2, "net") == 0 && p2[0] == 0);   /* открытая сеть */

    // чужой тип кадра - не Wi-Fi
    uint8_t d[3] = { 1, 2, 3 };
    CHECK(cc_encode(7, d, 3, cells) == 0 && cc_check(cells).status == CC_OK && cc_wifi_decode(cells, s2, p2) == -1);

    if (fails) printf("colorcode: %d failed\n", fails); else printf("colorcode: all passed\n");
    return fails != 0;
}
