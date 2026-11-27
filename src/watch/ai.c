#include "ai.h"
#include "b64.h"
#include "net.h"
#include "tables.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AI_ANSWER_MAX 4096
#define AI_TIMEOUT_S  60

// Копирует s в out не больше outn-1 байт, не разрезая символ UTF-8; если пришлось обрезать, в конце многоточие.
static void copy_trim(char *out, size_t outn, const char *s, bool keep_nl)
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
    for (size_t i = 0; i < k; i++) {
        char c = s[i];
        out[i] = (c == '\r' || c == '\t' || (c == '\n' && !keep_nl)) ? ' ' : c;
    }
    if (cut) { memcpy(out + k, "...", 3); k += 3; }
    out[k] = '\0';
}

// Отправляет готовое тело JSON телефону. Ответ 200 -> malloc-текст, иначе NULL и причина в err.
static char *phone_post(const char *body, size_t max_answer, int timeout_s, char *err, size_t errn)
{
    char host[40];
    if (!net_phone_host(host, sizeof host)) { snprintf(err, errn, "No Wi-Fi connection"); return NULL; }
    char url[96];
    snprintf(url, sizeof url, "http://%s:%d/ask", host, AI_PORT);

    int status = 0;
    char *ans = net_post_json(url, body, max_answer, timeout_s, &status, err, errn);
    if (!ans) {
        if (strstr(err, "onnect") || strstr(err, "ESP_ERR_HTTP")) snprintf(err, errn, "Phone app not running");
        return NULL;
    }
    if (status != 200) {
        char msg[96];
        copy_trim(msg, sizeof msg, ans[0] ? ans : "Ошибка приложения на телефоне", false);
        snprintf(err, errn, "%s", msg);
        free(ans);
        return NULL;
    }
    return ans;
}

bool ai_ask(const char *question, char *out, size_t outn, char *err, size_t errn)
{
    if (outn) out[0] = '\0';
    if (!question || !*question) { snprintf(err, errn, "Empty question"); return false; }

    cJSON *j = cJSON_CreateObject();
    if (!j) { snprintf(err, errn, "Out of memory"); return false; }
    cJSON_AddStringToObject(j, "q", question);
    char *body = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    if (!body) { snprintf(err, errn, "Out of memory"); return false; }

    char *ans = phone_post(body, AI_ANSWER_MAX, AI_TIMEOUT_S, err, errn);
    cJSON_free(body);
    if (!ans) return false;
    copy_trim(out, outn, ans, true);   // переводы строк сохраняем: шаги решения, списки
    free(ans);
    return true;
}

bool ai_ask_image(const char *question, const uint8_t *jpeg, size_t n, char *out, size_t outn, char *err, size_t errn)
{
    if (outn) out[0] = '\0';
    if (!jpeg || n == 0) { snprintf(err, errn, "No photo"); return false; }
    const char *q = (question && *question) ? question : "Что на этом фото?";

    // JSON собираем вручную: base64 фото занимает сотни КБ, копировать его через cJSON незачем.
    cJSON *qs = cJSON_CreateString(q);
    char *qj = qs ? cJSON_PrintUnformatted(qs) : NULL;      // вопрос в кавычках и с экранированием
    cJSON_Delete(qs);
    if (!qj) { snprintf(err, errn, "Out of memory"); return false; }

    size_t ql = strlen(qj), bl = B64_LEN(n);
    char *body = malloc(ql + bl + 32);
    if (!body) { cJSON_free(qj); snprintf(err, errn, "Out of memory"); return false; }
    size_t o = (size_t)sprintf(body, "{\"q\":%s,\"img\":\"", qj);
    cJSON_free(qj);
    o += b64_encode(jpeg, n, body + o);
    memcpy(body + o, "\"}", 3);

    char *ans = phone_post(body, AI_ANSWER_MAX, AI_TIMEOUT_S, err, errn);
    free(body);
    if (!ans) return false;
    copy_trim(out, outn, ans, true);
    free(ans);
    return true;
}

bool ai_ask_table(const char *question, const char *name, char *err, size_t errn)
{
    if (!question || !*question) { snprintf(err, errn, "Empty question"); return false; }
    cJSON *j = cJSON_CreateObject();
    if (!j) { snprintf(err, errn, "Out of memory"); return false; }
    cJSON_AddStringToObject(j, "q", question);
    cJSON_AddBoolToObject(j, "table", 1);
    char *body = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    if (!body) { snprintf(err, errn, "Out of memory"); return false; }

    char *ans = phone_post(body, AI_ANSWER_MAX, AI_TIMEOUT_S, err, errn);
    cJSON_free(body);
    if (!ans) return false;
    bool ok = tables_set_json(name, ans);
    if (!ok) snprintf(err, errn, "AI sent no table");
    free(ans);
    return ok;
}
