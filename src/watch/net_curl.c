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

char *net_post_json(const char *url, const char *json, size_t max_size, int timeout_s, int *status, char *err, size_t errn)
{
    if (status) *status = 0;
    if (strncmp(url, "http://", 7) != 0) {
        snprintf(err, errn, "Link must start with http://");
        return NULL;
    }
    CURL *c = curl_easy_init();
    if (!c) { snprintf(err, errn, "Network init failed"); return NULL; }

    sink_t s = { NULL, 0, max_size, false };
    struct curl_slist *h = curl_slist_append(NULL, "Content-Type: application/json");
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, json);
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)strlen(json));
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_data);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &s);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 6L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, (long)timeout_s);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "watch-os/0.1");
    CURLcode rc = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(h);
    curl_easy_cleanup(c);

    if (rc != CURLE_OK) {
        snprintf(err, errn, "%s", s.over ? "Answer is too large" : curl_easy_strerror(rc));
        free(s.buf);
        return NULL;
    }
    if (status) *status = (int)code;
    if (!s.buf) s.buf = calloc(1, 1);
    return s.buf;
}

bool net_phone_host(char *host, size_t n)
{
    const char *e = getenv("WATCH_AI_HOST");
    snprintf(host, n, "%s", e && *e ? e : "127.0.0.1");
    return true;
}
