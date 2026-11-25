#include "ai.h"
#include "net.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AI_ANSWER_MAX 4096
#define AI_TIMEOUT_S  60

// Копирует s в out не больше outn-1 байт, не разрезая символ UTF-8; если пришлось обрезать, в конце многоточие.
static void copy_trim(char *out, size_t outn, const char *s)
{
    while (*s == ' ' || *s == '\n' || *s == '\r' || *s == '\t') s++;
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == '\t')) n--;
    bool cut = n > outn - 1;
    size_t k = cut ? outn - 1 : n;
    if (cut) {
        if (k > 3) k -= 3;                                   // место под "..."
        while (k > 0 && ((unsigned char)s[k] & 0xC0) == 0x80) k--;   // не разрезать символ
    }
    for (size_t i = 0; i < k; i++) out[i] = (s[i] == '\n' || s[i] == '\r' || s[i] == '\t') ? ' ' : s[i];
    if (cut) { memcpy(out + k, "...", 3); k += 3; }
    out[k] = '\0';
}

bool ai_ask(const char *question, char *out, size_t outn, char *err, size_t errn)
{
    if (outn) out[0] = '\0';
    if (!question || !*question) { snprintf(err, errn, "Empty question"); return false; }

    char host[40];
    if (!net_phone_host(host, sizeof host)) { snprintf(err, errn, "No Wi-Fi connection"); return false; }
    char url[96];
    snprintf(url, sizeof url, "http://%s:%d/ask", host, AI_PORT);

    cJSON *j = cJSON_CreateObject();
    if (!j) { snprintf(err, errn, "Out of memory"); return false; }
    cJSON_AddStringToObject(j, "q", question);
    char *body = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    if (!body) { snprintf(err, errn, "Out of memory"); return false; }

    int status = 0;
    char *ans = net_post_json(url, body, AI_ANSWER_MAX, AI_TIMEOUT_S, &status, err, errn);
    cJSON_free(body);
    if (!ans) {
        if (strstr(err, "onnect") || strstr(err, "ESP_ERR_HTTP")) snprintf(err, errn, "Phone app not running");
        return false;
    }
    if (status != 200) {
        char msg[96];
        copy_trim(msg, sizeof msg, ans[0] ? ans : "Phone app error");
        snprintf(err, errn, "%s", msg);
        free(ans);
        return false;
    }
    copy_trim(out, outn, ans);
    free(ans);
    return true;
}
