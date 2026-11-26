// paired_test.c - проверка постоянных токенов (paired.c) без LVGL: выдача, проверка, замена, лимит, удаление, сохранение на диск.
//   cmake --build build --target paired_test
#include "watch/fs.h"
#include "watch/paired.h"
#include "watch/plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s\n", msg); fails++; } else printf("ok:   %s\n", msg); } while (0)

static unsigned rnd_state = 12345;
bool plat_random(void *buf, size_t n) { uint8_t *p = buf; for (size_t i = 0; i < n; i++) { rnd_state = rnd_state * 1103515245u + 12345u; p[i] = (uint8_t)(rnd_state >> 16); } return true; }

int main(void)
{
    if (system("rm -rf paired_dir")) return 2;
    paired_init("paired_dir");
    CHECK(paired_count() == 0, "empty at start");

    uint8_t a[PAIRED_TOKEN_LEN], b[PAIRED_TOKEN_LEN], bad[PAIRED_TOKEN_LEN] = {0};
    CHECK(paired_add("Pixel", a), "add Pixel");
    CHECK(paired_add("Galaxy", b), "add Galaxy");
    CHECK(paired_count() == 2, "two devices");
    CHECK(paired_verify(a) && paired_verify(b), "both tokens valid");
    CHECK(!paired_verify(bad), "wrong token rejected");

    uint8_t a2[PAIRED_TOKEN_LEN];
    CHECK(paired_add("Pixel", a2), "pair Pixel again");
    CHECK(paired_count() == 2, "same name replaces, no duplicate");
    CHECK(!paired_verify(a) && paired_verify(a2), "old Pixel token dead, new works");

    paired_init("paired_dir");                       // как после перезапуска часов
    char name[PAIRED_NAME_MAX + 1];
    CHECK(paired_count() == 2 && paired_info(0, name, sizeof name) && strcmp(name, "Galaxy") == 0, "list survives restart");
    CHECK(paired_verify(a2) && paired_verify(b), "tokens survive restart");
    { char *txt = fs_read_file("paired_dir/paired.json", 4096);
      CHECK(txt && !strstr(txt, "\"token\""), "file holds hashes only");
      free(txt); }

    CHECK(paired_remove(0), "forget Galaxy");
    CHECK(!paired_verify(b) && paired_verify(a2), "forgotten token rejected");
    CHECK(!paired_remove(5), "bad index ignored");

    uint8_t t[PAIRED_TOKEN_LEN], first[PAIRED_TOKEN_LEN];
    for (int i = 0; i < PAIRED_MAX + 2; i++) { char n[16]; snprintf(n, sizeof n, "dev%d", i); paired_add(n, t); if (i == 0) memcpy(first, t, sizeof t); }
    CHECK(paired_count() == PAIRED_MAX, "limit holds");
    CHECK(!paired_verify(first) && paired_verify(t), "oldest evicted, newest works");

    paired_clear();
    CHECK(paired_count() == 0 && !paired_verify(t), "clear");
    printf(fails ? "FAILED\n" : "ALL OK\n");
    return fails ? 1 : 0;
}
