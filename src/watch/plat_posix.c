#include "plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

bool plat_random(void *buf, size_t n)
{
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) return false;
    size_t rd = fread(buf, 1, n, f);
    fclose(f);
    return rd == n;
}

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <string.h>

bool plat_local_ip(char *buf, size_t n)
{
    struct ifaddrs *ifa = NULL, *p;
    if (getifaddrs(&ifa) != 0) return false;
    bool found = false;
    for (p = ifa; p; p = p->ifa_next) {
        if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET || (p->ifa_flags & IFF_LOOPBACK) || !(p->ifa_flags & IFF_UP)) continue;
        struct in_addr a = ((struct sockaddr_in *)p->ifa_addr)->sin_addr;
        if (inet_ntop(AF_INET, &a, buf, (socklen_t)n)) { found = true; break; }
    }
    freeifaddrs(ifa);
    return found;
}

bool plat_rtc_read(int64_t *epoch_utc)
{
    time_t t = time(NULL);
    if (t < 1500000000) return false;
    *epoch_utc = (int64_t)t;
    return true;
}

void plat_rtc_write(int64_t epoch_utc) { (void)epoch_utc; }   // системное время на ПК не трогаем

bool plat_battery(int *pct, bool *charging)
{
    const char *e = getenv("WATCH_BATTERY");
    if (!e) e = "87";                                   // по умолчанию в симуляторе показываем заряд, чтобы проверять интерфейс
    if (!*e || strcmp(e, "off") == 0) return false;     // WATCH_BATTERY=off - как устройство без батареи
    int v = atoi(e);
    if (v < 0) v = 0;
    if (v > 100) v = 100;
    *pct = v;
    *charging = strchr(e, '+') != NULL;
    return true;
}
