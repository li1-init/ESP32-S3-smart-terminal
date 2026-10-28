/* ==========================================================================
 *  weather.c  心知天气（Seniverse）实现
 *
 *  接口：GET https://api.seniverse.com/v3/weather/daily.json
 *              ?key=<KEY>&location=wuhan&language=zh-Hans&unit=c&start=0&days=3
 *  返回：{"results":[{"location":{"name":"武汉"},
 *                     "daily":[{"date":"2026-10-28","text_day":"阴",
 *                               "high":"20","low":"15", ...}, ...]}]}
 *
 *   为什么换成心知：
 *     老实现要"先查 IP 城市再查天气" 两步 + 两台服务器  容易失败 
 *     心知【一步拿到未来 3 天】 key 免费 
 *
 *   三个注意点
 *     HTTPS 必须挂证书包  否则握手失败 
 *     high/low 是【字符串】 不是数字  要 atoi 
 *     本函数是【阻塞】的  只能在普通任务里调 
 *       UI 那边已经用独立任务包好了 
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
#include "weather.h"
#include "app_secrets.h"     /*  SENIVERSE_KEY 放在本机私密文件里，不上传 */

#define TAG_W "weather"

/* ---------------- 配置 ---------------- */
#define DEFAULT_CITY    "wuhan"                 /* 查询用拼音  */
#define DEFAULT_CITY_CN "武汉"                  /* 界面显示中文  */

#define URL_FMT "https://api.seniverse.com/v3/weather/daily.json" \
                "?key=%s&location=%s&language=zh-Hans&unit=c&start=0&days=3"

/* ---------------- 收响应 ---------------- */
typedef struct {
    char *buf;
    int   len;
    int   cap;
} resp_t;

static esp_err_t http_evt(esp_http_client_event_t *evt)
{
    resp_t *r = (resp_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && r) {
        int n = evt->data_len;
        if (r->len + n >= r->cap) n = r->cap - r->len - 1;
        if (n > 0) {
            memcpy(r->buf + r->len, evt->data, n);
            r->len += n;
            r->buf[r->len] = '\0';
        }
    }
    return ESP_OK;
}

/* 一次 GET返回 malloc 的 JSON（调用者 free ）*/
static char *http_get_json(void)
{
    char url[256];
    snprintf(url, sizeof(url), URL_FMT, SENIVERSE_KEY, DEFAULT_CITY);

    char *buf = calloc(1, 4096);
    if (buf == NULL) return NULL;

    resp_t r = { .buf = buf, .len = 0, .cap = 4096 };

    esp_http_client_config_t cfg = {
        .url               = url,
        .method            = HTTP_METHOD_GET,
        .timeout_ms        = 20000,
        .event_handler     = http_evt,
        .user_data         = &r,
        .crt_bundle_attach = esp_crt_bundle_attach,   /*  HTTPS 必需  */
    };

    esp_http_client_handle_t cli = esp_http_client_init(&cfg);
    if (cli == NULL) { free(buf); return NULL; }

    esp_err_t e = esp_http_client_perform(cli);
    int code = esp_http_client_get_status_code(cli);
    esp_http_client_cleanup(cli);

    if (e != ESP_OK || code != 200 || r.len == 0) {
        ESP_LOGE(TAG_W, "请求失败 e=0x%x code=%d len=%d ", e, code, r.len);
        free(buf);
        return NULL;
    }
    ESP_LOGI(TAG_W, "HTTP 200收到 %d 字节 ", r.len);
    return buf;
}

/* ---------------- 工具 ---------------- */
static void copy_str(char *dst, size_t dst_len, const char *src)
{
    if (dst == NULL || dst_len == 0) return;
    if (src == NULL) { dst[0] = '\0'; return; }
    snprintf(dst, dst_len, "%s", src);
}

static const char *jstr(cJSON *obj, const char *key)
{
    cJSON *it = cJSON_GetObjectItem(obj, key);
    return (it && cJSON_IsString(it) && it->valuestring) ? it->valuestring : NULL;
}

static cJSON *get_result(cJSON *root)
{
    cJSON *results = cJSON_GetObjectItem(root, "results");
    return cJSON_GetArrayItem(results, 0);
}

/* ==========================================================================
 *   城市名（心知不需要 IP 定位  直接返回显示名 ）
 * ========================================================================== */
esp_err_t weather_get_ip_city(char *city, size_t city_len)
{
    if (city == NULL || city_len == 0) return ESP_ERR_INVALID_ARG;
    copy_str(city, city_len, DEFAULT_CITY_CN);
    return ESP_OK;
}

/* ==========================================================================
 *   今日天气
 * ========================================================================== */
esp_err_t weather_get_by_city(const char *city, weather_data_t *data)
{
    (void)city;
    if (data == NULL) return ESP_ERR_INVALID_ARG;
    memset(data, 0, sizeof(*data));

    char *json = http_get_json();
    if (json == NULL) return ESP_FAIL;

    cJSON *root = cJSON_Parse(json);
    free(json);
    if (root == NULL) { ESP_LOGE(TAG_W, "JSON 解析失败 "); return ESP_FAIL; }

    esp_err_t ret = ESP_FAIL;
    cJSON *res = get_result(root);
    if (res != NULL) {
        cJSON *loc = cJSON_GetObjectItem(res, "location");
        const char *name = jstr(loc, "name");
        copy_str(data->city, sizeof(data->city), name ? name : DEFAULT_CITY_CN);

        cJSON *d0 = cJSON_GetArrayItem(cJSON_GetObjectItem(res, "daily"), 0);
        if (d0 != NULL) {
            const char *hi = jstr(d0, "high");
            const char *lo = jstr(d0, "low");
            const char *tx = jstr(d0, "text_day");
            snprintf(data->temperature, sizeof(data->temperature), "%s~%s",
                     lo ? lo : "--", hi ? hi : "--");
            copy_str(data->text, sizeof(data->text), tx ? tx : "--");
            ret = ESP_OK;
        }
    }
    cJSON_Delete(root);
    return ret;
}

esp_err_t weather_get(weather_data_t *data)
{
    return weather_get_by_city(DEFAULT_CITY, data);
}

/* ==========================================================================
 *   未来 3 天
 * ========================================================================== */
esp_err_t weather_get_forecast_by_city(const char *city, weather_forecast_t *forecast)
{
    (void)city;
    if (forecast == NULL) return ESP_ERR_INVALID_ARG;
    memset(forecast, 0, sizeof(*forecast));

    char *json = http_get_json();
    if (json == NULL) return ESP_FAIL;

    cJSON *root = cJSON_Parse(json);
    free(json);
    if (root == NULL) { ESP_LOGE(TAG_W, "JSON 解析失败 "); return ESP_FAIL; }

    esp_err_t ret = ESP_FAIL;
    cJSON *res = get_result(root);
    if (res != NULL) {
        cJSON *loc = cJSON_GetObjectItem(res, "location");
        const char *name = jstr(loc, "name");
        copy_str(forecast->city, sizeof(forecast->city), name ? name : DEFAULT_CITY_CN);

        cJSON *daily = cJSON_GetObjectItem(res, "daily");
        int n = cJSON_GetArraySize(daily);
        if (n > WEATHER_FORECAST_DAYS) n = WEATHER_FORECAST_DAYS;

        for (int i = 0; i < n; i++) {
            cJSON *d = cJSON_GetArrayItem(daily, i);
            if (d == NULL) continue;

            weather_day_t *day = &forecast->days[i];
            copy_str(day->date,     sizeof(day->date),     jstr(d, "date"));
            copy_str(day->text_day, sizeof(day->text_day), jstr(d, "text_day"));
            copy_str(day->code_day, sizeof(day->code_day), jstr(d, "code_day"));

            const char *hi = jstr(d, "high");
            const char *lo = jstr(d, "low");
            day->high = hi ? atoi(hi) : 0;      /*  是字符串  要 atoi  */
            day->low  = lo ? atoi(lo) : 0;
        }
        ret = ESP_OK;
    }
    cJSON_Delete(root);
    return ret;
}

esp_err_t weather_get_forecast(weather_forecast_t *forecast)
{
    return weather_get_forecast_by_city(DEFAULT_CITY, forecast);
}

/* ==========================================================================
 *   图标路径（当前 UI 纯黑白  不用图标  空实现防链接错误 ）
 * ========================================================================== */
esp_err_t weather_day_get_icon_path(const weather_day_t *day, char *path, size_t path_len)
{
    (void)day;
    if (path && path_len) path[0] = '\0';
    return ESP_OK;
}
