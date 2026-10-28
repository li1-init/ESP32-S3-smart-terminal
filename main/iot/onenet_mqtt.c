#include "onenet_mqtt.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "esp_log.h"
#include "cJSON.h"
#include "mqtt_client.h"
#include "onenet_token.h"
#include "onenet_dm.h"
#include "onenet_ota.h"
#include "task_message.h"
#include "task_begin.h"

#define TAG     "onenet_mqtt"

//连接成功标志位
static bool onenet_connected_flg = false;

//mqtt连接客户端
static esp_mqtt_client_handle_t s_onenet_client = NULL;


static void onenet_property_ack(const char* id,int code,const char* message);
static void onenet_ota_ack(const char* id,int code,const char* message);
static esp_err_t onenet_subscribe(void);

/**
 * mqtt连接事件处理函数
 * @param event 事件参数
 * @return 无
 */
static void onenet_mqtt_event_handler(void* event_handler_arg,
                                        esp_event_base_t event_base,
                                        int32_t event_id,
                                        void* event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:  //连接成功
            ESP_LOGI(TAG, "Onenet mqtt connected");
            onenet_connected_flg = true;
            /*  到这里才算【真的连上】 这时才置位 
               （原来是在 init.c 里 onenet_start() 之后立刻置位 
                 那时只是"发起了连接" MQTT 还没握手  是假消息 ）*/
            if (begin_event_group != NULL)
                xEventGroupSetBits(begin_event_group, onenet_connected_BIT);

            onenet_subscribe();
            cJSON* property_js = onenet_property_upload_dm();
            char* data = cJSON_PrintUnformatted(property_js);
            onenet_post_property_data(data);
            onenet_ota_upload_version();
            cJSON_free(data);
            cJSON_Delete(property_js);
            set_app_vaild(true);
            break;
        case MQTT_EVENT_DISCONNECTED:   //连接断开
            ESP_LOGI(TAG, "Onenet mqtt disconnected");
            onenet_connected_flg = false;
            break;

        case MQTT_EVENT_SUBSCRIBED:     //收到订阅消息ACK
            ESP_LOGI(TAG, "Onenet mqtt subscribed ack, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:   //收到解订阅消息ACK

            break;
        case MQTT_EVENT_PUBLISHED:      //收到发布消息ACK
            //ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            ESP_LOGI(TAG, "Onenet mqtt publish ack, msg_id=%d", event->msg_id);

            break;
        case MQTT_EVENT_DATA:
            /* \u2605\u2605 \u628a\u4e0b\u884c\u539f\u59cb\u62a5\u6587\u6253\u51fa\u6765 \u2713
               \u8fd9\u6837"\u6536\u4e0d\u5230\u6d88\u606f"\u65f6\u4e00\u770b\u65e5\u5fd7\u5c31\u77e5\u9053\u5e73\u53f0\u5230\u5e95\u53d1\u4ec0\u4e48\u4e86 \u2713 */
            ESP_LOGI(TAG, "\u4e0b\u884c\u62a5\u6587[%d]\uff1a%.*s",
                     event->data_len, event->data_len, event->data);
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);
            if(strstr(event->topic,"/property/set"))
            {
                cJSON *property_js = cJSON_Parse(event->data);
                cJSON *id_js = cJSON_GetObjectItem(property_js,"id");

                onenet_property_handle(property_js);
                onenet_property_ack(cJSON_GetStringValue(id_js),200,"success");
                cJSON_Delete(property_js);
            }
            else if(strstr(event->topic,"/ota/inform"))
            {
                cJSON *ota_js = cJSON_Parse(event->data);
                cJSON *id_js = cJSON_GetObjectItem(ota_js,"id");
                onenet_ota_ack(cJSON_GetStringValue(id_js),200,"success");
                cJSON_Delete(ota_js);
                onenet_ota_start();
            }
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");

            break;
        default:
            break;
    }
}

/**
 * 返回属性设置确认
 * @param code 错误码
 * @param message 信息
 * @return mqtt连接参数
 */
static void onenet_property_ack(const char* id,int code,const char* message)
{
    char topic[128];
    snprintf(topic,sizeof(topic),"$sys/%s/%s/thing/property/set_reply",ONENET_PRODUCT_ID,ONENET_DEVICE_NAME);

    cJSON *reply_js = cJSON_CreateObject();
    cJSON_AddStringToObject(reply_js,"id",id);
    cJSON_AddNumberToObject(reply_js,"code",code);
    cJSON_AddStringToObject(reply_js,"message",message);
    char* data = cJSON_PrintUnformatted(reply_js);
    esp_mqtt_client_publish(s_onenet_client,topic,data,strlen(data),1,0); 
    cJSON_free(data);
    cJSON_Delete(reply_js);
}

/**
 * 返回OTA确认
 * @param code 错误码
 * @param message 信息
 * @return mqtt连接参数
 */
static void onenet_ota_ack(const char* id,int code,const char* message)
{
    char topic[128];
    snprintf(topic,sizeof(topic),"$sys/%s/%s/ota/inform_reply",ONENET_PRODUCT_ID,ONENET_DEVICE_NAME);

    cJSON *reply_js = cJSON_CreateObject();
    cJSON_AddStringToObject(reply_js,"id",id);
    cJSON_AddNumberToObject(reply_js,"code",code);
    cJSON_AddStringToObject(reply_js,"message",message);
    char* data = cJSON_PrintUnformatted(reply_js);
    esp_mqtt_client_publish(s_onenet_client,topic,data,strlen(data),1,0); 
    cJSON_free(data);
    cJSON_Delete(reply_js);
}

/**
 * 启动mqtt连接
 * @param 无
 * @return 错误码
 */
esp_err_t onenet_start(void)
{
    esp_mqtt_client_config_t mqtt_client = {0};
    //onenet平台连接的mqtt地址
    mqtt_client.broker.address.uri = "mqtt://mqtts.heclouds.com";
    //onenet平台连接的mqtt端口号
    mqtt_client.broker.address.port = 1883;
    //client id用设备名称
    mqtt_client.credentials.client_id = ONENET_DEVICE_NAME;
    //用户名用产品id
    mqtt_client.credentials.username = ONENET_PRODUCT_ID;
    //token有效时间（2030年1月1日）
    #define TM_EXPIRE_TIME 1924833600
    //密码onenet平台使用token来作为密码token直接使用dev_token_generate计算即可
    static char token[256];
    dev_token_generate(token, SIG_METHOD_SHA256, TM_EXPIRE_TIME, ONENET_PRODUCT_ID,
                       NULL, ONENET_ACCESS_KEY);
    mqtt_client.credentials.authentication.password = token;
    /* ⚠️ 原来这里把 token（=密码）明文打印到串口 
       token 里有产品/设备的鉴权签名  有效期到 2030 年 
       泄露 = 别人能冒充这台设备   已删除  只留不带密的那部分 */
    ESP_LOGI(TAG, "onenet connect->clientId:%s", mqtt_client.credentials.client_id);
    //设置mqtt的配置返回一个mqtt句柄此句柄后续用来发送数据、注册事件、断开连接使用
    s_onenet_client = esp_mqtt_client_init(&mqtt_client);
    //注册mqtt事件
    esp_mqtt_client_register_event(s_onenet_client, ESP_EVENT_ANY_ID,
                                   onenet_mqtt_event_handler, s_onenet_client); // ESP_EVENT_ANY_ID  代表响应所有事件
    xEventGroupSetBits(begin_event_group, onenet_connected_BIT);
    // 启动mqtt连接注意此函数会创建一个mqtt任务并不会启动mqtt连接
    return esp_mqtt_client_start(s_onenet_client);
}

/**
 * 停止onenet连接
 * @param 无
 * @return 错误码
 */
esp_err_t onenet_stop(void)
{
    esp_err_t ret = ESP_OK;
    if(s_onenet_client)
    {
        ret = esp_mqtt_client_destroy(s_onenet_client);
        s_onenet_client = NULL;
    }
    return ret;
}

/**
 * 上报数据
 * @param data 数据
 * @return 错误
 */
esp_err_t onenet_post_property_data(const char* data)
{
    if (!onenet_connected_flg)
        return ESP_FAIL;
    char topic[128];
    snprintf(topic,sizeof(topic),"$sys/%s/%s/thing/property/post",
        ONENET_PRODUCT_ID,ONENET_DEVICE_NAME);
    ESP_LOGI(TAG,"Upload topic:%s,payload:%s",topic,data);
    return esp_mqtt_client_publish(s_onenet_client,topic,data,strlen(data),1,0);
}

/**
 * 订阅相关主题有要订阅的主题可以放在这个函数
 * @param 无
 * @return 错误
 */
esp_err_t onenet_subscribe(void)
{
    if (!onenet_connected_flg)
        return ESP_FAIL;
    char topic[128];
    //订阅上报属性回复主题
    snprintf(topic,sizeof(topic),"$sys/%s/%s/thing/property/post/reply",
        ONENET_PRODUCT_ID,ONENET_DEVICE_NAME);
    esp_mqtt_client_subscribe_single(s_onenet_client,topic,1);
    //订阅下行设置属性主题
    snprintf(topic,sizeof(topic),"$sys/%s/%s/thing/property/set",
        ONENET_PRODUCT_ID,ONENET_DEVICE_NAME);
    esp_mqtt_client_subscribe_single(s_onenet_client,topic,1);
    //订阅OTA主题
    snprintf(topic,sizeof(topic),"$sys/%s/%s/ota/inform",
        ONENET_PRODUCT_ID,ONENET_DEVICE_NAME);
    return esp_mqtt_client_subscribe_single(s_onenet_client,topic,1);
}
