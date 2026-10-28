#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>
#include "ws_server.h"

static const char *TAG = "WebSocket Server";

static const char *http_html = NULL;
static ws_receive_cb ws_receive_fn = NULL;
static httpd_handle_t http_ws_server = NULL;
static int client_sockfd = -1;

static char *cached_ws_data = NULL;
static size_t cached_ws_len = 0;
static SemaphoreHandle_t cached_ws_lock = NULL;

static void ws_cache_payload(const uint8_t *data, size_t len)
{
    if (cached_ws_lock == NULL) {
        cached_ws_lock = xSemaphoreCreateMutex();
        if (cached_ws_lock == NULL) {
            return;
        }
    }

    xSemaphoreTake(cached_ws_lock, portMAX_DELAY);
    free(cached_ws_data);
    cached_ws_data = malloc(len + 1);
    if (cached_ws_data) {
        memcpy(cached_ws_data, data, len);
        cached_ws_data[len] = '\0';
        cached_ws_len = len;
    } else {
        cached_ws_len = 0;
    }
    xSemaphoreGive(cached_ws_lock);
}

static void ws_send_cached_on_open(httpd_req_t *req)
{
    if (cached_ws_lock == NULL) {
        return;
    }

    xSemaphoreTake(cached_ws_lock, portMAX_DELAY);
    if (cached_ws_data) {
        httpd_ws_frame_t pkt;
        memset(&pkt, 0, sizeof(pkt));
        pkt.type = HTTPD_WS_TYPE_TEXT;
        pkt.payload = (uint8_t *)cached_ws_data;
        pkt.len = cached_ws_len;

        esp_err_t ret = httpd_ws_send_frame(req, &pkt);
        ESP_LOGI(TAG, "Send cached scan result on reconnect: %s", ret == ESP_OK ? "OK" : "FAIL");
    }
    xSemaphoreGive(cached_ws_lock);
}

static esp_err_t handle_ws_req(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        client_sockfd = httpd_req_to_sockfd(req);
        ESP_LOGI(TAG, "Handshake done, fd=%d", client_sockfd);
        ws_send_cached_on_open(req);
        return ESP_OK;
    }

    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(ws_pkt));

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK) {
        return ret;
    }

    if (ws_pkt.len) {
        buf = calloc(1, ws_pkt.len + 1);
        if (buf == NULL) {
            return ESP_ERR_NO_MEM;
        }

        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            free(buf);
            return ret;
        }

        ESP_LOGI(TAG, "Got packet: %s", ws_pkt.payload);
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_TEXT && ws_receive_fn) {
        ws_receive_fn(ws_pkt.payload, ws_pkt.len);
    }

    free(buf);
    return ESP_OK;
}

static esp_err_t get_req_handler(httpd_req_t *req)
{
    if (http_html == NULL) {
        return ESP_FAIL;
    }

    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    return httpd_resp_send(req, http_html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t get_wifi_list_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");

    if (cached_ws_lock == NULL) {
        return httpd_resp_send(req, "{\"wifi_list\":[]}", HTTPD_RESP_USE_STRLEN);
    }

    xSemaphoreTake(cached_ws_lock, portMAX_DELAY);
    esp_err_t ret;
    if (cached_ws_data) {
        ret = httpd_resp_send(req, cached_ws_data, HTTPD_RESP_USE_STRLEN);
    } else {
        ret = httpd_resp_send(req, "{\"wifi_list\":[]}", HTTPD_RESP_USE_STRLEN);
    }
    xSemaphoreGive(cached_ws_lock);
    return ret;
}

esp_err_t web_ws_send(uint8_t *data, int len)
{
    ws_cache_payload(data, len);

    if (http_ws_server == NULL || client_sockfd < 0) {
        return ESP_ERR_INVALID_STATE;
    }

    if (httpd_ws_get_fd_info(http_ws_server, client_sockfd)
        != HTTPD_WS_CLIENT_WEBSOCKET) {
        ESP_LOGW(TAG, "WebSocket client disconnected, cached message");
        client_sockfd = -1;
        return ESP_ERR_INVALID_STATE;
    }

    httpd_ws_frame_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.payload = data;
    pkt.len = len;
    pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_send_data(http_ws_server, client_sockfd, &pkt);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WebSocket send failed: %d, cached message", ret);
        client_sockfd = -1;
    }
    return ret;
}

esp_err_t web_ws_start(ws_cfg_t *cfg)
{
    if (cfg == NULL) {
        return ESP_FAIL;
    }

    if (cached_ws_lock == NULL) {
        cached_ws_lock = xSemaphoreCreateMutex();
    }

    http_html = cfg->html_code;
    ws_receive_fn = cfg->receive_fn;
    client_sockfd = -1;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    httpd_uri_t uri_get = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = get_req_handler,
    };

    httpd_uri_t uri_wifi_list = {
        .uri = "/wifi_list",
        .method = HTTP_GET,
        .handler = get_wifi_list_handler,
    };

    httpd_uri_t uri_ws = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = handle_ws_req,
        .is_websocket = true,
    };

    if (httpd_start(&http_ws_server, &config) != ESP_OK) {
        http_ws_server = NULL;
        return ESP_FAIL;
    }

    httpd_register_uri_handler(http_ws_server, &uri_get);
    httpd_register_uri_handler(http_ws_server, &uri_wifi_list);
    httpd_register_uri_handler(http_ws_server, &uri_ws);
    return ESP_OK;
}

esp_err_t web_ws_stop(void)
{
    if (http_ws_server) {
        httpd_stop(http_ws_server);
        http_ws_server = NULL;
    }
    client_sockfd = -1;

    if (cached_ws_lock) {
        xSemaphoreTake(cached_ws_lock, portMAX_DELAY);
    }
    free(cached_ws_data);
    cached_ws_data = NULL;
    cached_ws_len = 0;
    if (cached_ws_lock) {
        xSemaphoreGive(cached_ws_lock);
    }
    return ESP_OK;
}

