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
