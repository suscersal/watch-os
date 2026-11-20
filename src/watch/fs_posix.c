#include "fs.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>

bool fs_list_dir(const char *path, fs_dir_cb cb, void *ud)
{
    DIR *d = opendir(path);
    if (!d) return false;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        cb(e->d_name, ud);
    }
    closedir(d);
    return true;
}

char *fs_read_file_n(const char *path, size_t max_size, size_t *len_out)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0 || (size_t)sz > max_size) { fclose(f); return NULL; }
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';
    if (len_out) *len_out = rd;
    return buf;
}

char *fs_read_file(const char *path, size_t max_size)
{
    return fs_read_file_n(path, max_size, NULL);
}

#include <errno.h>
#include <sys/stat.h>

bool fs_mkdir(const char *path)
{
    return mkdir(path, 0755) == 0 || errno == EEXIST;
}

bool fs_write_file(const char *path, const char *data, size_t n)
{
    char tmp[256];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    bool ok = fwrite(data, 1, n, f) == n;
    ok = (fclose(f) == 0) && ok;
    if (!ok) { remove(tmp); return false; }
    return rename(tmp, path) == 0;
}

bool fs_remove(const char *path) { return remove(path) == 0; }
