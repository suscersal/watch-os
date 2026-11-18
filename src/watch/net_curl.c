#include "net.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char *buf; size_t len, max; bool over; } sink_t;

static size_t on_data(char *p, size_t sz, size_t nm, void *ud)
{
    sink_t *s = ud;
    size_t n = sz * nm;
    if (s->len + n > s->max) { s->over = true; return 0; }
    char *nb = realloc(s->buf, s->len + n + 1);
    if (!nb) return 0;
    s->buf = nb;
    memcpy(s->buf + s->len, p, n);
    s->len += n;
    s->buf[s->len] = '\0';
    return n;
}

char *net_get(const char *url, size_t max_size, char *err, size_t errn)
{
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
        snprintf(err, errn, "Link must start with http:// or https://");
        return NULL;
    }
    CURL *c = curl_easy_init();
    if (!c) { snprintf(err, errn, "Network init failed"); return NULL; }

    sink_t s = { NULL, 0, max_size, false };
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_data);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &s);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 6L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "watch-os/0.1");
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
    curl_easy_setopt(c, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
    curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
#endif
    CURLcode rc = curl_easy_perform(c);
    curl_easy_cleanup(c);

    if (rc != CURLE_OK) {
        snprintf(err, errn, "%s", s.over ? "File is too large" : curl_easy_strerror(rc));
        free(s.buf);
        return NULL;
    }
    if (!s.buf) s.buf = calloc(1, 1);
    return s.buf;
}
