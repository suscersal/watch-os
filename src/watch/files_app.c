#include "files_app.h"
#include "fs.h"
#include "plat.h"
#include "sys_ui.h"
#include "watch_fonts.h"
#include "ws_style.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef WATCH_ROOT
#define WATCH_ROOT ""
#endif

#define ENTRIES_MAX 64
#define NAME_MAX_LEN 63
#define PATH_MAX_LEN 192
#define PREVIEW_BYTES 700

typedef struct { char name[NAME_MAX_LEN + 1]; bool dir; long size; } entry_t;

static lv_obj_t *g_launcher, *g_scr, *g_body, *g_right;
static char      g_cwd[PATH_MAX_LEN];       // путь от корня данных: "" | "data" | "data/photos"
static entry_t   g_ent[ENTRIES_MAX];
static int       g_n;
static bool      g_truncated;
static char      g_del_path[PATH_MAX_LEN];  // что удаляем (для окна подтверждения)

static const char *const ROOTS[] = { "apps", "data", "packages" };
#define SD_NAME "sdcard"      // виртуальная папка в корне: показывает microSD, пока она вставлена

static void build_list(void);
static void build_async(void *ud) { LV_UNUSED(ud); build_list(); }

// ---------- пути ----------
static bool is_sd_rel(const char *rel)
{
    size_t k = strlen(SD_NAME);
    return strncmp(rel, SD_NAME, k) == 0 && (rel[k] == '\0' || rel[k] == '/');
}

static void real_path(const char *rel, char *out, size_t n)
{
    if (is_sd_rel(rel)) {                                  // "sdcard/music/a.wav" -> "<корень карты>/music/a.wav"
        const char *root = plat_sd_root();
        if (!root) { snprintf(out, n, "/.no-sd%s", rel + strlen(SD_NAME)); return; }   // карту вынули: пути не существует
        snprintf(out, n, "%s%s", root, rel + strlen(SD_NAME));
        return;
    }
    snprintf(out, n, "%s%s", WATCH_ROOT, rel);
}

static bool join(const char *dir, const char *name, char *out, size_t n)
{
    int k = dir[0] ? snprintf(out, n, "%s/%s", dir, name) : snprintf(out, n, "%s", name);
    return k > 0 && (size_t)k < n;
}

static void fmt_size(long b, char *out, size_t n)
{
    if (b < 0)               snprintf(out, n, "?");
    else if (b < 1024)       snprintf(out, n, "%ld B", b);
    else if (b < 1024 * 1024) snprintf(out, n, "%ld KB", (b + 512) / 1024);
    else                     snprintf(out, n, "%.1f MB", (double)b / (1024.0 * 1024.0));
}

static bool ends_with(const char *s, const char *suf)
{
    size_t a = strlen(s), b = strlen(suf);
    if (b > a) return false;
    for (size_t i = 0; i < b; i++) {
        char c = s[a - b + i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if (c != suf[i]) return false;
    }
    return true;
}

static bool is_text_name(const char *n)
{
    static const char *const ext[] = { ".txt", ".json", ".md", ".csv", ".log", ".c", ".h", ".ini", ".cfg" };
    for (size_t i = 0; i < sizeof ext / sizeof ext[0]; i++) if (ends_with(n, ext[i])) return true;
    return false;
}

// Прямо в data/ лежат PIN-хэш и settings.json с паролем Wi-Fi и ключом API открытым текстом: на экран их не выводим.
static bool is_private(const char *rel)
{
    return strncmp(rel, "data/", 5) == 0 && strchr(rel + 5, '/') == NULL;
}

// ---------- чтение каталога ----------
typedef struct { char dir[PATH_MAX_LEN]; } scan_t;

static void scan_cb(const char *name, void *ud)
{
    const scan_t *sc = ud;
    if (g_n >= ENTRIES_MAX) { g_truncated = true; return; }
    if (strlen(name) > NAME_MAX_LEN || ends_with(name, ".tmp")) return;
    char rel[PATH_MAX_LEN], real[PATH_MAX_LEN + 8];
    if (!join(sc->dir, name, rel, sizeof rel)) return;
    real_path(rel, real, sizeof real);
    entry_t *e = &g_ent[g_n++];
    snprintf(e->name, sizeof e->name, "%s", name);
    e->dir = fs_is_dir(real);
    e->size = e->dir ? 0 : fs_file_size(real);
}

static int cmp_ent(const void *a, const void *b)
{
    const entry_t *x = a, *y = b;
    if (x->dir != y->dir) return x->dir ? -1 : 1;
    return strcmp(x->name, y->name);
}

static void load_dir(void)
{
    g_n = 0;
    g_truncated = false;
    if (!g_cwd[0]) {                                   // корень: только известные папки
        for (size_t i = 0; i < sizeof ROOTS / sizeof ROOTS[0]; i++) {
            char real[PATH_MAX_LEN];
            real_path(ROOTS[i], real, sizeof real);
            if (!fs_is_dir(real)) continue;
            entry_t *e = &g_ent[g_n++];
            snprintf(e->name, sizeof e->name, "%s", ROOTS[i]);
            e->dir = true;
        }
        if (plat_sd_root() && g_n < ENTRIES_MAX) {         // карта вставлена: она видна как папка sdcard
            entry_t *e = &g_ent[g_n++];
            snprintf(e->name, sizeof e->name, "%s", SD_NAME);
            e->dir = true;
        }
        return;
    }
    scan_t sc;
    snprintf(sc.dir, sizeof sc.dir, "%s", g_cwd);
    char real[PATH_MAX_LEN + 8];
    real_path(g_cwd, real, sizeof real);
    fs_list_dir(real, scan_cb, &sc);
    qsort(g_ent, (size_t)g_n, sizeof g_ent[0], cmp_ent);
}

// ---------- виджеты ----------
static lv_obj_t *label(lv_obj_t *p, const char *t, uint32_t color, const lv_font_t *font)
{
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_text(l, t);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    if (font) lv_obj_set_style_text_font(l, font, 0);
    return l;
}

static lv_obj_t *row_btn(lv_obj_t *parent, uint32_t bg, lv_event_cb_t cb, void *ud)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, lv_pct(100), 52);
    lv_obj_set_style_radius(b, 14, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_pad_hor(b, 12, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    return b;
}

// ---------- сведения о файле ----------
static void detail_back_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_screen_load_anim(g_scr, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, 200, 0, true);   // экран файла удаляется
}

static void remove_one(const char *rel)
{
    char real[PATH_MAX_LEN + 8];
    real_path(rel, real, sizeof real);
    fs_remove(real);
}

static void delete_done(bool ok, void *ud)
{
    LV_UNUSED(ud);
    if (!ok) return;
    char real[PATH_MAX_LEN + 8];
    real_path(g_del_path, real, sizeof real);
    bool was_dir = fs_is_dir(real);
    if (!fs_remove(real)) { sys_toast(was_dir ? "Folder is not empty" : "Could not delete"); return; }

    // Фото в Gallery - пара файлов: полный кадр и миниатюра (<имя>.jpg и <имя>.t.jpg). Удаляем обе половины.
    size_t n = strlen(g_del_path);
    if ((strncmp(g_del_path, "data/photos/", 12) == 0 || strncmp(g_del_path, SD_NAME "/photos/", sizeof(SD_NAME "/photos/") - 1) == 0) && n > 6) {
        char other[PATH_MAX_LEN];
        if (ends_with(g_del_path, ".t.jpg")) snprintf(other, sizeof other, "%.*s.jpg", (int)(n - 6), g_del_path);
        else if (ends_with(g_del_path, ".jpg")) snprintf(other, sizeof other, "%.*s.t.jpg", (int)(n - 4), g_del_path);
        else other[0] = '\0';
        if (other[0]) remove_one(other);
    }
    sys_toast(was_dir ? "Folder deleted" : "Deleted");
    if (was_dir) {                                       // папки уже нет: поднимаемся на уровень выше
        char *s = strrchr(g_cwd, '/');
        if (s) *s = '\0'; else g_cwd[0] = '\0';
    }
    lv_screen_load_anim(g_scr, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT, 200, 0, true);   // экран файла удаляется
    lv_async_call(build_async, NULL);
}

static void delete_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    sys_confirm("Delete?", g_del_path, delete_done, NULL);
}

static void show_detail(const entry_t *en)
{
    char rel[PATH_MAX_LEN], real[PATH_MAX_LEN + 8];
    if (!join(g_cwd, en->name, rel, sizeof rel)) return;
    real_path(rel, real, sizeof real);
    snprintf(g_del_path, sizeof g_del_path, "%s", rel);

    lv_obj_t *body;
    lv_obj_t *scr = sys_screen_create(en->dir ? "Folder" : "File", detail_back_cb, &body);
    label(body, en->name, 0xFFFFFF, WATCH_FONT_LARGE);
    label(body, rel, 0x9CA3AF, WATCH_FONT_SMALL);
    if (!en->dir) {
        char sz[24];
        fmt_size(en->size, sz, sizeof sz);
        label(body, sz, 0x9CA3AF, NULL);
        if (is_private(rel)) {
            label(body, "System data: preview is hidden", 0x9CA3AF, WATCH_FONT_SMALL);
        } else if (is_text_name(en->name)) {
            char buf[PREVIEW_BYTES + 1];
            long got = fs_read_range(real, 0, buf, PREVIEW_BYTES);
            if (got > 0) {
                for (long i = 0; i < got; i++) {                 // только печатный текст; байты UTF-8 (русский) оставляем
                    unsigned char c = (unsigned char)buf[i];
                    if (c < 0x20 && c != '\n') buf[i] = ' ';
                }
                buf[got] = '\0';
                size_t k = (size_t)got;
                while (k > 0 && ((unsigned char)buf[k - 1] & 0xC0) == 0x80) k--;       // не оставляем половину символа
                if (k > 0 && (unsigned char)buf[k - 1] >= 0xC0) k--;
                if (got < (long)en->size) { if (k + 3 < sizeof buf) { memcpy(buf + k, "...", 3); k += 3; } }
                buf[k] = '\0';
                label(body, buf, 0xD1D5DB, WATCH_FONT_SMALL);
            }
        } else if (ends_with(en->name, ".jpg")) {
            label(body, "Photo: open it in Gallery", 0x9CA3AF, WATCH_FONT_SMALL);
        }
    }
    lv_obj_t *b = row_btn(body, 0xB91C1C, delete_cb, NULL);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, en->dir ? LV_SYMBOL_TRASH " Delete folder" : LV_SYMBOL_TRASH " Delete");
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_obj_center(l);
    lv_screen_load_anim(scr, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 200, 0, false);   // список не удаляем
}

// ---------- список ----------
static void set_cwd(const char *rel)
{
    snprintf(g_cwd, sizeof g_cwd, "%s", rel);
    lv_async_call(build_async, NULL);
}

static void up_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    char *s = strrchr(g_cwd, '/');
    if (s) *s = '\0'; else g_cwd[0] = '\0';
    lv_async_call(build_async, NULL);
}

static void item_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= g_n) return;
    entry_t en = g_ent[i];
    if (en.dir && !g_cwd[0]) { set_cwd(en.name); return; }
    if (en.dir) {
        char rel[PATH_MAX_LEN];
        if (join(g_cwd, en.name, rel, sizeof rel)) set_cwd(rel);
        return;
    }
    show_detail(&en);
}

static void rmdir_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    snprintf(g_del_path, sizeof g_del_path, "%s", g_cwd);
    sys_confirm("Delete folder?", g_del_path, delete_done, NULL);
}

static void build_list(void)
{
    if (!g_body) return;
    load_dir();
    lv_obj_clean(g_body);
    if (g_right) lv_label_set_text(g_right, g_cwd[0] ? g_cwd : "/");

    if (g_cwd[0]) {
        lv_obj_t *b = row_btn(g_body, WS_FILL2, up_cb, NULL);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, LV_SYMBOL_LEFT "  Back");
        lv_obj_set_style_text_color(l, lv_color_white(), 0);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
    }
    for (int i = 0; i < g_n; i++) {
        const entry_t *en = &g_ent[i];
        lv_obj_t *b = row_btn(g_body, WS_CARD, item_cb, (void *)(intptr_t)i);
        lv_obj_t *nm = lv_label_create(b);
        lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
        lv_obj_set_width(nm, en->dir ? 280 : 200);
        lv_label_set_text(nm, en->name);
        lv_obj_set_style_text_color(nm, lv_color_white(), 0);
        lv_obj_align(nm, LV_ALIGN_LEFT_MID, 34, 0);

        lv_obj_t *ic = lv_label_create(b);
        lv_label_set_text(ic, en->dir ? LV_SYMBOL_DIRECTORY : ends_with(en->name, ".jpg") ? LV_SYMBOL_IMAGE : LV_SYMBOL_FILE);
        lv_obj_set_style_text_color(ic, lv_color_hex(en->dir ? WS_ORANGE : WS_TEXT2), 0);
        lv_obj_align(ic, LV_ALIGN_LEFT_MID, 0, 0);

        if (!en->dir) {
            char sz[24];
            fmt_size(en->size, sz, sizeof sz);
            lv_obj_t *s = lv_label_create(b);
            lv_label_set_text(s, sz);
            lv_obj_set_style_text_color(s, lv_color_hex(WS_TEXT2), 0);
            lv_obj_set_style_text_font(s, WATCH_FONT_SMALL, 0);
            lv_obj_align(s, LV_ALIGN_RIGHT_MID, 0, 0);
        }
    }
    if (g_n == 0) label(g_body, g_cwd[0] ? "Empty folder" : "No folders", 0x9CA3AF, NULL);
    if (g_truncated) label(g_body, "Only the first 64 items are shown", 0x9CA3AF, WATCH_FONT_SMALL);
    if (g_cwd[0] && g_n == 0 && strchr(g_cwd, '/')) {          // пустую вложенную папку можно удалить; корневые apps/data/packages - нет
        lv_obj_t *b = row_btn(g_body, 0xB91C1C, rmdir_cb, NULL);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, LV_SYMBOL_TRASH " Delete folder");
        lv_obj_set_style_text_color(l, lv_color_white(), 0);
        lv_obj_center(l);
    }
}

static void gone_cb(lv_event_t *e) { LV_UNUSED(e); g_scr = g_body = g_right = NULL; }

static void exit_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_screen_load_anim(g_launcher, sys_exit_anim(), 200, 0, true);
}

const char *files_cwd(void) { return g_cwd; }

void files_enter(const char *rel)
{
    snprintf(g_cwd, sizeof g_cwd, "%s", rel ? rel : "");
    build_list();
}

void files_open(lv_obj_t *launcher)
{
    g_launcher = launcher;
    g_cwd[0] = '\0';
    g_scr = sys_screen_create_root_text("Files", exit_cb, &g_body, "/", &g_right);
    lv_obj_add_event_cb(g_scr, gone_cb, LV_EVENT_DELETE, NULL);
    lv_obj_set_flex_flow(g_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(g_body, 8, 0);
    build_list();
    lv_screen_load_anim(g_scr, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
}
