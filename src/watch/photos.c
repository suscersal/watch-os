#include "photos.h"
#include "fs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char g_dir[96] = "data/photos";

typedef struct { uint32_t taken, id; } entry_t;
typedef struct { entry_t *e; int n, cap; } list_t;

static void path_of(uint32_t taken, uint32_t id, bool thumb, char *out, size_t n)
{
    snprintf(out, n, "%s/%08x_%08x%s.jpg", g_dir, (unsigned)taken, (unsigned)id, thumb ? ".t" : "");
}

void photos_init(const char *dir)
{
    snprintf(g_dir, sizeof g_dir, "%s", dir);
    // создаём все уровни пути ("data" и "data/photos")
    char tmp[96];
    snprintf(tmp, sizeof tmp, "%s", g_dir);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') { *p = '\0'; fs_mkdir(tmp); *p = '/'; }
    }
    fs_mkdir(g_dir);
}

static void scan_cb(const char *name, void *ud)
{
    list_t *l = ud;
    size_t len = strlen(name);
    if (len < 4 || strcmp(name + len - 4, ".jpg") != 0) return;        // .tmp и прочее пропускаем
    if (len > 6 && strcmp(name + len - 6, ".t.jpg") == 0) return;      // миниатюры отдельно не перечисляем
    unsigned taken, id;
    if (len != 8 + 1 + 8 + 4 || sscanf(name, "%8x_%8x", &taken, &id) != 2) return;
    if (l->n == l->cap) {
        int nc = l->cap ? l->cap * 2 : 32;
        entry_t *ne = realloc(l->e, (size_t)nc * sizeof *ne);
        if (!ne) return;
        l->e = ne; l->cap = nc;
    }
    l->e[l->n].taken = taken; l->e[l->n].id = id; l->n++;
}

static int cmp_desc(const void *a, const void *b)
{
    const entry_t *x = a, *y = b;
    if (x->taken != y->taken) return x->taken < y->taken ? 1 : -1;
    if (x->id != y->id) return x->id < y->id ? 1 : -1;
    return 0;
}

static void scan(list_t *l)
{
    memset(l, 0, sizeof *l);
    fs_list_dir(g_dir, scan_cb, l);
    if (l->n > 1) qsort(l->e, (size_t)l->n, sizeof l->e[0], cmp_desc);
}

size_t photos_count(void)
{
    list_t l; scan(&l);
    size_t n = (size_t)l.n;
    free(l.e);
    return n;
}

int photos_list(uint32_t *ids, uint32_t *taken, int max)
{
    list_t l; scan(&l);
    int n = l.n < max ? l.n : max;
    for (int i = 0; i < n; i++) { ids[i] = l.e[i].id; if (taken) taken[i] = l.e[i].taken; }
    free(l.e);
    return n;
}

static bool find(uint32_t id, uint32_t *taken)
{
    list_t l; scan(&l);
    bool found = false;
    for (int i = 0; i < l.n; i++) if (l.e[i].id == id) { found = true; if (taken) *taken = l.e[i].taken; break; }
    free(l.e);
    return found;
}

bool photos_exists(uint32_t id) { return find(id, NULL); }

bool photos_store(uint32_t id, uint32_t taken, const uint8_t *thumb, size_t tn, const uint8_t *full, size_t fn)
{
    if (find(id, NULL)) return false;
    char p[160], pt[160];
    path_of(taken, id, false, p, sizeof p);
    path_of(taken, id, true, pt, sizeof pt);
    // сначала миниатюра, потом полный кадр: фото появляется в списке только когда записан полный файл
    if (!fs_write_file(pt, (const char *)thumb, tn)) return false;
    if (!fs_write_file(p, (const char *)full, fn)) { fs_remove(pt); return false; }
    return true;
}

uint8_t *photos_load(uint32_t id, bool thumb, size_t *len)
{
    uint32_t taken;
    if (!find(id, &taken)) return NULL;
    char p[160];
    path_of(taken, id, thumb, p, sizeof p);
    return (uint8_t *)fs_read_file_n(p, thumb ? PHOTO_THUMB_MAX_BYTES : PHOTO_FULL_MAX_BYTES, len);
}

bool photos_delete(uint32_t id)
{
    uint32_t taken;
    if (!find(id, &taken)) return false;
    char p[160], pt[160];
    path_of(taken, id, false, p, sizeof p);
    path_of(taken, id, true, pt, sizeof pt);
    bool ok = fs_remove(p);
    fs_remove(pt);
    return ok;
}

bool photos_check_jpeg(const uint8_t *d, size_t n, int max_w, int max_h, int *w, int *h)
{
    // Декодер LVGL (TJpgDec) узнаёт JPEG в памяти только по заголовку JFIF: FF D8 FF E0 00 10 'J' 'F' 'I' 'F'
    static const uint8_t jfif[10] = { 0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 'J', 'F', 'I', 'F' };
    if (n < 12 || memcmp(d, jfif, sizeof jfif) != 0) return false;
    size_t i = 2;
    while (i + 4 <= n) {
        if (d[i] != 0xFF) return false;
        uint8_t m = d[i + 1];
        if (m == 0xFF) { i++; continue; }                                  // заполнитель
        if (m == 0x01 || (m >= 0xD0 && m <= 0xD8)) { i += 2; continue; }   // маркеры без длины
        size_t seg = ((size_t)d[i + 2] << 8) | d[i + 3];
        if (seg < 2 || i + 2 + seg > n) return false;
        if (m == 0xC0) {                                                   // SOF0: baseline
            if (seg < 8) return false;
            int hh = (d[i + 5] << 8) | d[i + 6], ww = (d[i + 7] << 8) | d[i + 8];
            if (ww < 1 || hh < 1 || ww > max_w || hh > max_h) return false;
            if (w) *w = ww;
            if (h) *h = hh;
            return true;
        }
        if (m == 0xC1 || m == 0xC2 || (m >= 0xC3 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC)) return false;   // не baseline
        if (m == 0xDA) return false;                                       // начались данные, а размера не было
        i += 2 + seg;
    }
    return false;
}
