#include "installer.h"
#include "ws_style.h"
#include "app_manager.h"
#include "fs.h"
#include "net.h"
#include "sys_ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void watch_launcher_refresh(void);   // launcher.c

#define MANIFEST_MAX 2048
#define APP_MAX_BYTES 16384
#define MAX_SCREENS 16

#define FAIL(msg) do { snprintf(err, n, "%s", msg); return false; } while (0)

static const char *jstr(const cJSON *o, const char *k)
{
    const cJSON *i = cJSON_GetObjectItemCaseSensitive(o, k);
    return cJSON_IsString(i) ? i->valuestring : NULL;
}

static bool valid_id(const char *s)
{
    size_t len = s ? strlen(s) : 0;
    if (len < 1 || len >= 32 || strncmp(s, "sys_", 4) == 0) return false;
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    }
    return true;
}

bool installer_validate(const cJSON *pkg, char *err, size_t n)
{
    if (!cJSON_IsObject(pkg)) FAIL("Not an app package");
    if (!valid_id(jstr(pkg, "id"))) FAIL("Bad app id (a-z 0-9 _ -)");

    const cJSON *m = cJSON_GetObjectItemCaseSensitive(pkg, "manifest");
    const cJSON *app = cJSON_GetObjectItemCaseSensitive(pkg, "app");
    if (!cJSON_IsObject(m) || !cJSON_IsObject(app)) FAIL("Missing manifest or app");

    const char *name = jstr(m, "name");
    if (!name || !name[0] || strlen(name) >= sizeof(((app_info_t *)0)->name)) FAIL("Bad app name");
    const char *col = jstr(m, "color");
    if (col && !(col[0] == '#' && strlen(col) == 7 && strspn(col + 1, "0123456789abcdefABCDEF") == 6)) FAIL("Bad color");

    const cJSON *perms = cJSON_GetObjectItemCaseSensitive(m, "permissions");
    if (perms && !cJSON_IsArray(perms)) FAIL("Bad permissions");
    const cJSON *p;
    cJSON_ArrayForEach(p, perms) {
        if (!cJSON_IsString(p) || (strcmp(p->valuestring, "net") != 0 && strcmp(p->valuestring, "storage") != 0))
            FAIL("Unknown permission");
    }

    const cJSON *screens = cJSON_GetObjectItemCaseSensitive(app, "screens");
    const char *start = jstr(app, "start");
    if (!cJSON_IsObject(screens) || !start) FAIL("Missing start/screens");
    int count = 0;
    const cJSON *sc;
    cJSON_ArrayForEach(sc, screens) {
        if (++count > MAX_SCREENS) FAIL("Too many screens");
        if (!sc->string || strlen(sc->string) >= 24) FAIL("Screen name too long");
        if (!cJSON_IsObject(sc)) FAIL("Bad screen");
    }
    if (!cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(screens, start))) FAIL("Start screen not found");

    char *a = cJSON_PrintUnformatted(app);
    char *mm = cJSON_PrintUnformatted(m);
    bool big = (a && strlen(a) > APP_MAX_BYTES) || (mm && strlen(mm) > MANIFEST_MAX);
    free(a); free(mm);
    if (big) FAIL("App is too large");
    return true;
}

bool installer_fetch(const char *url, cJSON **out, char *err, size_t n)
{
    char *txt = net_get(url, PKG_MAX, err, n);
    if (!txt) return false;
    cJSON *p = cJSON_ParseWithLength(txt, strlen(txt));
    free(txt);
    if (!p) FAIL("Not valid JSON");
    if (!installer_validate(p, err, n)) { cJSON_Delete(p); return false; }
    *out = p;
    return true;
}

bool installer_commit(const cJSON *pkg, char *err, size_t n)
{
    if (!installer_validate(pkg, err, n)) return false;
    const char *id = jstr(pkg, "id");
    const cJSON *m = cJSON_GetObjectItemCaseSensitive(pkg, "manifest");
    const cJSON *app = cJSON_GetObjectItemCaseSensitive(pkg, "app");

    // в manifest.json попадают только известные поля
    cJSON *clean = cJSON_CreateObject();
    cJSON_AddItemToObject(clean, "name", cJSON_Duplicate(cJSON_GetObjectItemCaseSensitive(m, "name"), 1));
    cJSON_AddStringToObject(clean, "icon", jstr(m, "icon") ? jstr(m, "icon") : "file");
    cJSON_AddStringToObject(clean, "color", jstr(m, "color") ? jstr(m, "color") : "#6B7280");
    const cJSON *perms = cJSON_GetObjectItemCaseSensitive(m, "permissions");
    cJSON_AddItemToObject(clean, "permissions", perms ? cJSON_Duplicate(perms, 1) : cJSON_CreateArray());
    cJSON_AddStringToObject(clean, "entry", "app.json");

    char *mtxt = cJSON_PrintUnformatted(clean);
    char *atxt = cJSON_PrintUnformatted(app);
    cJSON_Delete(clean);

    char dir[128], path[160];
    snprintf(dir, sizeof dir, "%s/%s", app_manager_root(), id);
    bool ok = mtxt && atxt && fs_mkdir(app_manager_root()) && fs_mkdir(dir);
    if (ok) { snprintf(path, sizeof path, "%s/app.json", dir);      ok = fs_write_file(path, atxt, strlen(atxt)); }
    if (ok) { snprintf(path, sizeof path, "%s/manifest.json", dir); ok = fs_write_file(path, mtxt, strlen(mtxt)); }
    free(mtxt); free(atxt);
    if (!ok) FAIL("Write failed");
    return true;
}

bool installer_uninstall(const char *id)
{
    if (!valid_id(id)) return false;
    char dir[128], path[160];
    snprintf(dir, sizeof dir, "%s/%s", app_manager_root(), id);
    snprintf(path, sizeof path, "%s/app.json", dir);      fs_remove(path);
    snprintf(path, sizeof path, "%s/manifest.json", dir); fs_remove(path);
    return fs_remove(dir);   // false, если в папке остались чужие файлы
}

// ------------------------------ UI ------------------------------
static lv_obj_t *g_launcher;
static lv_obj_t *url_ta, *status_lbl, *list_box;
static cJSON    *pending_pkg;
static char      pending_del[32];

static void set_status(const char *t, uint32_t color)
{
    if (!status_lbl) return;
    lv_label_set_text(status_lbl, t);
    lv_obj_set_style_text_color(status_lbl, lv_color_hex(color), 0);
}

static void screen_deleted_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    url_ta = status_lbl = list_box = NULL;
}

static void rebuild_list(void);

static void after_change(void)
{
    app_manager_scan(app_manager_root());
    watch_launcher_refresh();
    rebuild_list();
}

static void install_confirm_cb(bool ok, void *ud)
{
    LV_UNUSED(ud);
    cJSON *pkg = pending_pkg;
    pending_pkg = NULL;
    if (!pkg) return;
    if (ok) {
        char err[64];
        if (installer_commit(pkg, err, sizeof err)) {
            set_status("Installed", 0x22C55E);
            sys_toast("App installed");
            after_change();
        } else {
            set_status(err, 0xEF4444);
        }
    } else {
        set_status("Cancelled", 0x9CA3AF);
    }
    cJSON_Delete(pkg);
}

static void install_btn_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (!url_ta) return;
    sys_keyboard_hide();
    char url[256];
    snprintf(url, sizeof url, "%s", lv_textarea_get_text(url_ta));
    if (!url[0]) { set_status("Enter a link first", 0xEF4444); return; }

    set_status("Downloading...", 0x9CA3AF);
    lv_refr_now(NULL);   // net_get блокирующий: сначала рисуем статус

    char err[64];
    cJSON *pkg = NULL;
    if (!installer_fetch(url, &pkg, err, sizeof err)) { set_status(err, 0xEF4444); return; }

    // окно подтверждения: что ставим, что просит, откуда
    const cJSON *m = cJSON_GetObjectItemCaseSensitive(pkg, "manifest");
    const cJSON *perms = cJSON_GetObjectItemCaseSensitive(m, "permissions");
    char msg[320], plist[96] = "";
    const cJSON *p;
    cJSON_ArrayForEach(p, perms) {
        const char *human = strcmp(p->valuestring, "net") == 0 ? "Network" : "Storage";
        if (plist[0]) strncat(plist, ", ", sizeof plist - strlen(plist) - 1);
        strncat(plist, human, sizeof plist - strlen(plist) - 1);
    }
    char path[160];
    snprintf(path, sizeof path, "%s/%s/manifest.json", app_manager_root(), jstr(pkg, "id"));
    char *old = fs_read_file(path, MANIFEST_MAX);
    bool exists = old != NULL;
    free(old);
    snprintf(msg, sizeof msg, "%s\nAccess: %s\nFrom: %.80s",
             jstr(m, "name"), plist[0] ? plist : "none", url);

    if (pending_pkg) cJSON_Delete(pending_pkg);
    pending_pkg = pkg;
    set_status("Waiting for confirmation", 0x9CA3AF);
    sys_confirm(exists ? "Update app?" : "Install app?", msg, install_confirm_cb, NULL);
}

static void delete_confirm_cb(bool ok, void *ud)
{
    LV_UNUSED(ud);
    if (!ok) return;
    if (installer_uninstall(pending_del)) sys_toast("App removed");
    else sys_toast("Could not remove all files");
    after_change();
}

static void trash_cb(lv_event_t *e)
{
    const app_info_t *a = lv_event_get_user_data(e);
    snprintf(pending_del, sizeof pending_del, "%s", a->id);
    char msg[64];
    snprintf(msg, sizeof msg, "Remove %s and its data?", a->name);
    sys_confirm("Delete app?", msg, delete_confirm_cb, NULL);
}

static void rebuild_list(void)
{
    if (!list_box) return;
    lv_obj_clean(list_box);
    for (size_t i = 0; i < app_manager_count(); i++) {
        const app_info_t *a = app_manager_get(i);
        if (a->builtin) continue;
        lv_obj_t *row = lv_obj_create(list_box);
        lv_obj_remove_style_all(row);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_t *l = lv_label_create(row);
        lv_label_set_text(l, a->name);
        lv_obj_set_style_text_color(l, lv_color_white(), 0);
        lv_obj_t *b = lv_button_create(row);
        lv_obj_set_size(b, 44, 40);
        lv_obj_set_style_radius(b, 12, 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(0xEF4444), 0);
        lv_obj_t *bl = lv_label_create(b);
        lv_label_set_text(bl, LV_SYMBOL_TRASH);
        lv_obj_center(bl);
        lv_obj_add_event_cb(b, trash_cb, LV_EVENT_CLICKED, (void *)a);
    }
}

static void input_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_FOCUSED) sys_keyboard_attach(lv_event_get_target(e));
}

static void back_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    sys_keyboard_hide();
    sys_confirm_close();
    if (pending_pkg) { cJSON_Delete(pending_pkg); pending_pkg = NULL; }
    lv_screen_load_anim(g_launcher, sys_exit_anim(), 200, 0, true);   // экран установщика удаляется
}

void installer_open(lv_obj_t *launcher)
{
    g_launcher = launcher;
    lv_obj_t *body;
    lv_obj_t *scr = sys_screen_create_root("Install app", back_cb, &body);
    lv_obj_add_event_cb(scr, screen_deleted_cb, LV_EVENT_DELETE, NULL);

    lv_obj_t *hint = lv_label_create(body);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, lv_pct(100));
    lv_label_set_text(hint, "Link to an app package (.json):");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x9CA3AF), 0);

    url_ta = lv_textarea_create(body);
    lv_textarea_set_one_line(url_ta, true);
    lv_textarea_set_max_length(url_ta, 200);
    lv_textarea_set_placeholder_text(url_ta, "https://...");
    lv_obj_set_width(url_ta, lv_pct(100));
    lv_obj_set_style_bg_color(url_ta, lv_color_hex(WS_CARD), 0);
    lv_obj_set_style_text_color(url_ta, lv_color_white(), 0);
    lv_obj_set_style_border_width(url_ta, 0, 0);
    lv_obj_set_style_radius(url_ta, 12, 0);
    lv_obj_add_event_cb(url_ta, input_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *btn = lv_button_create(body);
    lv_obj_set_size(btn, lv_pct(100), 52);
    lv_obj_set_style_radius(btn, 14, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x0EA5E9), 0);
    lv_obj_t *bl = lv_label_create(btn);
    lv_label_set_text(bl, "Install");
    lv_obj_center(bl);
    lv_obj_add_event_cb(btn, install_btn_cb, LV_EVENT_CLICKED, NULL);

    status_lbl = lv_label_create(body);
    lv_label_set_long_mode(status_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(status_lbl, lv_pct(100));
    lv_label_set_text(status_lbl, "");

    lv_obj_t *h2 = lv_label_create(body);
    lv_label_set_text(h2, "Installed apps");
    lv_obj_set_style_text_color(h2, lv_color_hex(0x9CA3AF), 0);

    list_box = lv_obj_create(body);
    lv_obj_remove_style_all(list_box);
    lv_obj_remove_flag(list_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(list_box, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(list_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list_box, 8, 0);
    rebuild_list();

    lv_screen_load_anim(scr, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 200, 0, false);   // лаунчер не удаляем
}
