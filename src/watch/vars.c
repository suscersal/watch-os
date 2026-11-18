#include "vars.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

typedef struct { char k[VAR_NAME]; char v[VAR_VAL]; } var_t;
static var_t tbl[VARS_MAX];
static int   nvars;

static bool is_name_char(char c) { return isalnum((unsigned char)c) || c == '_'; }

void vars_clear(void) { nvars = 0; }

bool vars_valid_name(const char *name)
{
    if (!name || !name[0]) return false;
    size_t n = strlen(name);
    if (n >= VAR_NAME) return false;
    for (size_t i = 0; i < n; i++) if (!is_name_char(name[i])) return false;
    return true;
}

void vars_set(const char *name, const char *val)
{
    if (!vars_valid_name(name) || !val) return;
    for (int i = 0; i < nvars; i++) {
        if (strcmp(tbl[i].k, name) == 0) { snprintf(tbl[i].v, VAR_VAL, "%s", val); return; }
    }
    if (nvars >= VARS_MAX) return;
    snprintf(tbl[nvars].k, VAR_NAME, "%s", name);
    snprintf(tbl[nvars].v, VAR_VAL, "%s", val);
    nvars++;
}

const char *vars_get(const char *name)
{
    for (int i = 0; i < nvars; i++) if (strcmp(tbl[i].k, name) == 0) return tbl[i].v;
    return "";
}

void vars_expand(const char *in, char *out, size_t cap)
{
    size_t o = 0;
    if (cap == 0) return;
    const char *p = in;
    while (*p && o + 1 < cap) {
        if (p[0] == '@' && p[1] == '@') { out[o++] = '@'; p += 2; continue; }
        if (p[0] == '@' && is_name_char(p[1])) {
            char name[VAR_NAME];
            size_t l = 0;
            p++;
            while (is_name_char(*p) && l < VAR_NAME - 1) name[l++] = *p++;
            name[l] = '\0';
            const char *v = vars_get(name);
            while (*v && o + 1 < cap) out[o++] = *v++;
            continue;
        }
        out[o++] = *p++;
    }
    out[o] = '\0';
}
