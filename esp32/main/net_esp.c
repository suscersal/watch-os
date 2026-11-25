// net_esp.c - net_get() для ESP32 на esp_http_client (замена net_curl.c с ПК).
#include "watch/net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "esp_crt_bundle.h"
#include "esp_err.h"
#include "esp_http_client.h"
#include "esp_netif.h"
#include "wifi.h"

typedef struct { char *buf; size_t len, max; bool over, nomem; bool all; } sink_t;   // all: копировать тело при любом статусе

static esp_err_t on_http_event(esp_http_client_event_t *evt)
{
    sink_t *s = evt->user_data;
    // Тело копируем только у успешного ответа: страницы редиректов и ошибок нам не нужны.
    bool ok_status = s->all || esp_http_client_get_status_code(evt->client) == 200;

    switch (evt->event_id) {
    case HTTP_EVENT_ON_HEADER:
        if (ok_status && evt->header_key && evt->header_value &&
            strcasecmp(evt->header_key, "Content-Length") == 0) {
            unsigned long long n = strtoull(evt->header_value, NULL, 10);
            if (n > s->max) s->over = true;
        }
        break;
    case HTTP_EVENT_ON_DATA:
        if (!ok_status || s->over || s->nomem || evt->data_len <= 0) break;
        if (s->len + (size_t)evt->data_len > s->max) { s->over = true; break; }
        char *nb = realloc(s->buf, s->len + (size_t)evt->data_len + 1);
        if (!nb) { s->nomem = true; break; }
        s->buf = nb;
        memcpy(s->buf + s->len, evt->data, (size_t)evt->data_len);
        s->len += (size_t)evt->data_len;
        s->buf[s->len] = '\0';
        break;
    default:
        break;
    }
    return ESP_OK;
}

char *net_get(const char *url, size_t max_size, char *err, size_t errn)
{
    if (strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0) {
        snprintf(err, errn, "Link must start with http:// or https://");
        return NULL;
    }
    if (!wifi_is_connected() && !wifi_wait_connected(4000)) {
        snprintf(err, errn, "No Wi-Fi connection");
        return NULL;
    }

    sink_t s = { NULL, 0, max_size, false, false };
    esp_http_client_config_t cfg = {
        .url = url,
        .event_handler = on_http_event,
        .user_data = &s,
        .timeout_ms = 10000,
        .max_redirection_count = 3,
        .user_agent = "watch-os/0.1",
        .crt_bundle_attach = esp_crt_bundle_attach,   // проверка HTTPS по набору корневых сертификатов
        .buffer_size = 2048,                          // длинные адреса в Location при редиректах
        .buffer_size_tx = 1024,
    };

    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) { snprintf(err, errn, "Network init failed"); return NULL; }
    esp_err_t e = esp_http_client_perform(c);
    int status = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);

    if (s.over)   { free(s.buf); snprintf(err, errn, "File is too large"); return NULL; }
    if (s.nomem)  { free(s.buf); snprintf(err, errn, "Out of memory");     return NULL; }
    if (e != ESP_OK) { free(s.buf); snprintf(err, errn, "%s", esp_err_to_name(e)); return NULL; }
    if (status != 200) { free(s.buf); snprintf(err, errn, "HTTP %d", status); return NULL; }

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
    if (!wifi_is_connected() && !wifi_wait_connected(4000)) {
        snprintf(err, errn, "No Wi-Fi connection");
        return NULL;
    }

    sink_t s = { NULL, 0, max_size, false, false, true };
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = on_http_event,
        .user_data = &s,
        .timeout_ms = timeout_s * 1000,
        .user_agent = "watch-os/0.1",
        .buffer_size = 1024,
        .buffer_size_tx = 1024,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) { snprintf(err, errn, "Network init failed"); return NULL; }
    esp_http_client_set_header(c, "Content-Type", "application/json");
    esp_http_client_set_post_field(c, json, (int)strlen(json));
    esp_err_t e = esp_http_client_perform(c);
    int code = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);

    if (s.over)  { free(s.buf); snprintf(err, errn, "Answer is too large"); return NULL; }
    if (s.nomem) { free(s.buf); snprintf(err, errn, "Out of memory");       return NULL; }
    if (e != ESP_OK) { free(s.buf); snprintf(err, errn, "%s", esp_err_to_name(e)); return NULL; }
    if (status) *status = code;
    if (!s.buf) s.buf = calloc(1, 1);
    return s.buf;
}

// Телефон раздаёт Wi-Fi и сам является шлюзом сети: его адрес = шлюз станции.
bool net_phone_host(char *host, size_t n)
{
    esp_netif_t *nif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip;
    if (!nif || esp_netif_get_ip_info(nif, &ip) != ESP_OK || ip.gw.addr == 0) return false;
    snprintf(host, n, IPSTR, IP2STR(&ip.gw));
    return true;
}
