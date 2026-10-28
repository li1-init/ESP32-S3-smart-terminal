/* ==========================================================================
 *  ai_chat.c  DeepSeek 大模型问答（HTTPS）
 *
 *  接口：POST https://api.deepseek.com/chat/completions
 *  认证：Authorization: Bearer sk-xxxx
 *  请求：{"model":"deepseek-chat","messages":[{"role":"user","content":"..."}],
 *         "max_tokens":200,"stream":false}
 *  响应：{"choices":[{"message":{"content":"答案"}}]}
 *
 *   三个必须注意的点
 *     网络请求绝对不能放 LVGL 任务 （会卡界面 + 饿死空闲任务 ）
 *        这里自己起任务  结果写静态缓冲  UI 轮询 
 *     HTTPS 必须挂证书包  否则握手失败 
 *        .crt_bundle_attach = esp_crt_bundle_attach 
 *     Key 不要写死在代码里（会被提交到 git）
 *        放在 main/app_secrets.h，该文件已被 .gitignore 忽略
 * ========================================================================== */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"

#include "cJSON.h"
#include "app_secrets.h"     /*  API KEY（本机私密文件，已被 .gitignore 忽略） */
#include "ai_chat.h"

#define TAG_AI "ai"

#define AI_URL      "https://api.deepseek.com/chat/completions"
#define AI_MAX_ANS  900      /* 答案缓冲（屏幕只显示一部分  够用 ）*/

/* ---------------- 静态状态 ---------------- */
static volatile int s_state = 0;             /* 0 空闲 1 请求中 2 完成 3 失败 */
static char         s_answer[AI_MAX_ANS];
static char         s_error[128];
static bool         s_busy = false;

/* ---------------- HTTP 收集响应 ---------------- */
typedef struct {
    char *buf;
    int   len;
    int   cap;
} resp_t;

static esp_err_t http_evt_cb(esp_http_client_event_t *evt)
{
    resp_t *r = (resp_t *)evt->user_data;

    if (evt->event_id == HTTP_EVENT_ON_DATA && r) {
        int copy = evt->data_len;
        if (r->len + copy >= r->cap) copy = r->cap - r->len - 1;   /* 防止溢出  */
        if (copy > 0) {
            memcpy(r->buf + r->len, evt->data, copy);
            r->len += copy;
            r->buf[r->len] = '\0';
        }
    }
    return ESP_OK;
}

/* ---------------- 解析 DeepSeek 响应 ---------------- */
static bool parse_answer(const char *json, char *out, size_t out_len, char *err, size_t err_len)
{
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        snprintf(err, err_len, "返回不是 JSON");
        return false;
    }

    /* 先看有没有 error 字段 （key 错/余额不足都会走这里 ）*/
    cJSON *e = cJSON_GetObjectItem(root, "error");
    if (e != NULL) {
        cJSON *msg = cJSON_GetObjectItem(e, "message");
        snprintf(err, err_len, "云端错误：%s",
                 (msg && cJSON_IsString(msg)) ? msg->valuestring : "未知");
        cJSON_Delete(root);
        return false;
    }

    cJSON *choices = cJSON_GetObjectItem(root, "choices");
    cJSON *first   = cJSON_GetArrayItem(choices, 0);
    cJSON *message = cJSON_GetObjectItem(first, "message");
    cJSON *content = cJSON_GetObjectItem(message, "content");

    bool ok = false;
    if (content && cJSON_IsString(content) && content->valuestring) {
        snprintf(out, out_len, "%s", content->valuestring);
        ok = true;
    } else {
        snprintf(err, err_len, "响应里没有答案");
    }
    cJSON_Delete(root);
    return ok;
}

/* ---------------- 请求任务 ---------------- */
static void ai_task(void *arg)
{
    char q[240];
    snprintf(q, sizeof(q), "%s", (const char *)arg);
    free(arg);                         /*  参数是 malloc 的  用完释放  */

    s_state = 1;
    s_answer[0] = '\0';
    s_error[0]  = '\0';

    /* ---- 组装请求体（ 一定要转义引号  否则 JSON 非法 ）---- */
    char *body = malloc(768);
    if (body == NULL) {
        snprintf(s_error, sizeof(s_error), "内存不足");
        s_state = 3; s_busy = false; vTaskDelete(NULL); return;
    }
    snprintf(body, 768,
             "{\"model\":\"deepseek-chat\",\"max_tokens\":200,\"stream\":false,"
             "\"messages\":[{\"role\":\"system\",\"content\":\"用中文回答限40字以内\"},"
             "{\"role\":\"user\",\"content\":\"%s\"}]}", q);

    /* ---- 响应缓冲 ---- */
    char *rbuf = calloc(1, 4096);
    if (rbuf == NULL) {
        free(body);
        snprintf(s_error, sizeof(s_error), "内存不足");
        s_state = 3; s_busy = false; vTaskDelete(NULL); return;
    }
    resp_t r = { .buf = rbuf, .len = 0, .cap = 4096 };

    esp_http_client_config_t cfg = {
        .url             = AI_URL,
        .method          = HTTP_METHOD_POST,
        .timeout_ms      = 25000,
        .event_handler   = http_evt_cb,
        .user_data       = &r,
        .crt_bundle_attach = esp_crt_bundle_attach,   /*  HTTPS 必需  */
    };

    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (cli == NULL) {
        snprintf(s_error, sizeof(s_error), "HTTP 初始化失败");
        s_state = 3;
        goto done;
    }

    char auth[96];
    snprintf(auth, sizeof(auth), "Bearer %s", DEEPSEEK_API_KEY);
    esp_http_client_set_header(cli, "Content-Type",  "application/json");
    esp_http_client_set_header(cli, "Authorization", auth);
    esp_http_client_set_post_field(cli, body, (int)strlen(body));

    esp_err_t err = esp_http_client_perform(cli);

    if (err != ESP_OK) {
        snprintf(s_error, sizeof(s_error), "请求失败：%s", esp_err_to_name(err));
        s_state = 3;
    } else {
        int code = esp_http_client_get_status_code(cli);
        ESP_LOGI(TAG_AI, "HTTP %d收到 %d 字节", code, r.len);

        if (code == 200 && parse_answer(rbuf, s_answer, sizeof(s_answer),
                                        s_error, sizeof(s_error))) {
            s_state = 2;
            ESP_LOGI(TAG_AI, "答案：%s", s_answer);
        } else {
            if (s_error[0] == '\0') snprintf(s_error, sizeof(s_error), "HTTP %d", code);
            s_state = 3;
            ESP_LOGE(TAG_AI, "%s", s_error);
        }
    }

    esp_http_client_cleanup(cli);

done:
    free(rbuf);
    free(body);
    s_busy = false;
    vTaskDelete(NULL);
}

/* ---------------- 对外接口 ---------------- */
void ai_chat_ask(const char *question)
{
    if (question == NULL || question[0] == '\0') return;
    if (s_busy) {
        ESP_LOGW(TAG_AI, "上一个问题还没回来  忽略这次 ");
        return;
    }

    char *arg = strdup(question);
    if (arg == NULL) return;

    s_busy = true;
    if (xTaskCreate(ai_task, "ai_chat", 8192, arg, 4, NULL) != pdPASS) {
        free(arg);
        s_busy = false;
        snprintf(s_error, sizeof(s_error), "任务创建失败");
        s_state = 3;
    }
}

int         ai_chat_state(void)  { return s_state; }
const char *ai_chat_answer(void) { return s_answer; }
const char *ai_chat_error(void)  { return s_error; }
