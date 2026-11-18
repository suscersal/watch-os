#include "plat.h"
#include <stdio.h>

bool plat_random(void *buf, size_t n)
{
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) return false;
    size_t rd = fread(buf, 1, n, f);
    fclose(f);
    return rd == n;
}
