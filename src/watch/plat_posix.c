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

void plat_haptic(const uint16_t *pattern_ms, int n)
{
    if (!pattern_ms || n <= 0) return;
    printf("[haptic]");
    for (int i = 0; i < n && i < 16; i++) printf(" %s %ums", i % 2 == 0 ? "on" : "off", (unsigned)pattern_ms[i]);
    printf("\n");
    fflush(stdout);
}

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

bool plat_battery_info(plat_batt_info_t *o)
{
    int pct; bool chg;
    if (!plat_battery(&pct, &chg)) return false;
    o->pct = pct;
    o->charging = chg && pct < 100;
    o->vbus = chg;
    o->full = chg && pct >= 100;
    const char *mv = getenv("WATCH_BATTERY_MV"), *tc = getenv("WATCH_BATTERY_TEMP");
    o->mv = (mv && *mv) ? atoi(mv) : 3300 + pct * 9;                     // 0% ~ 3.3 В, 100% ~ 4.2 В
    o->temp_c10 = (tc && *tc) ? (int)(atof(tc) * 10) : (chg ? 330 : 280);
    return true;
}

const char *plat_sd_root(void)
{
    const char *e = getenv("WATCH_SD");
    if (!e || !*e) return "data/sd";
    if (strcmp(e, "off") == 0 || strcmp(e, "0") == 0) return NULL;
    return e;
}

bool plat_sd_present(void) { return plat_sd_root() != NULL; }

#include <sys/statvfs.h>

#ifndef WATCH_VERSION
#define WATCH_VERSION "dev"
#endif

void plat_sysinfo(plat_sysinfo_t *o)
{
    memset(o, 0, sizeof *o);
    snprintf(o->version, sizeof o->version, "%s", WATCH_VERSION);
    snprintf(o->built, sizeof o->built, "%s %s", __DATE__, __TIME__);
    o->platform = "ПК (симулятор)";
    struct statvfs sv;
    if (statvfs(".", &sv) == 0) {
        o->store_total = (size_t)sv.f_blocks * sv.f_frsize;
        o->store_free  = (size_t)sv.f_bavail * sv.f_frsize;
    }
}
