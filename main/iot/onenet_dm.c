/* ==========================================================================
 *  OneNET 物模型：温度 + 光照（已按我们的 ADC 传感器改写 ）
 *   改写点：原来那份是【灯的】 依赖 led_ws2812（我们没有 ） 全部替换 
 *   上报格式（OneNET 物模型规定 ）：
 *      {"id":"1","version":"1.0","params":{"temp":{"value":25.3},"light":{"value":68}}}
 *   下发格式：
 *      {"id":"xxx","params":{"temp":{"value":30}}}
 * ========================================================================== */
#include <string.h>
#include <stdio.h>
#include "cJSON.h"
#include "esp_log.h"
#include "onenet_dm.h"
#include "adc.h"
#include "light.h"      /*  云端开关灯要用  */
#include "task_message.h"

#define TAG_DM         "onenet_dm"

/*  物模型标识符  必须和 OneNET 控制台上一字不差 （大小写敏感 ）*/
#define DM_KEY_TEMP    "temp"
#define DM_KEY_LIGHT   "light"

/*  云端页用的：最近一条下行消息 + 序号  */
char             g_cloud_last_msg[128] = "";
volatile int     g_cloud_msg_seq       = 0;
#define DM_KEY_ALARM   "alarm"    /*  报警状态  bool （和 OneNET 上的标识符一字不差 ）*/

void onenet_dm_init(void)
{
    ESP_LOGI(TAG_DM, "物模型就绪：%s(温度 ℃) + %s(光照 %%)", DM_KEY_TEMP, DM_KEY_LIGHT);
}

/*  上报：拼 JSON （onenet_mqtt 连上时会自动调它 
   你也可以在定时任务里自己调  见 onenet_mqtt.h 的注释）*/
cJSON* onenet_property_upload_dm(void)
{
    float t = adc_temp_c();          /* 取当前温度  */
    int   l = adc_light_pct();       /* 取当前光照  */

    cJSON *root   = cJSON_CreateObject();
    cJSON *params = cJSON_AddObjectToObject(root, "params");

    cJSON *jt = cJSON_AddObjectToObject(params, DM_KEY_TEMP);
    /*  只留 1 位小数 （原来 29.542552947998047 太长了  浪费流量 ）*/
    cJSON_AddNumberToObject(jt, "value", (double)((int)(t * 10 + (t >= 0 ? 0.5f : -0.5f))) / 10.0);

    cJSON *jl = cJSON_AddObjectToObject(params, DM_KEY_LIGHT);
    cJSON_AddNumberToObject(jl, "value", l);

    /*  报警状态也上报 （云端/网页/规则引擎才能知道 
       以后想做"报警了就发短信"  靠的就是这个属性 ）*/
    cJSON *ja = cJSON_AddObjectToObject(params, DM_KEY_ALARM);
    cJSON_AddBoolToObject(ja, "value", adc_temp_alarm());

    cJSON_AddStringToObject(root, "id", "1");
    cJSON_AddStringToObject(root, "version", "1.0");

    ESP_LOGI(TAG_DM, "上报数据：temp=%.1f light=%d", t, l);
    return root;
}
/*
  {
  "id": "4",
  "version": "1.0",
  "params": {
    "temp":  { "value": 27.5, "time": 1792591800000 },
    "light": { "value": 80,   "time": 1792591800000 }
  }
}
 *///onenet下发的信息格式
/*  下发处理  【if/else 分发】就在这里 
   ⚠️ 这个函数由 onenet_mqtt.c 收到 /property/set 后自动调用 
      你只要往下面加 else if 就能扩展新功能  */
void onenet_property_handle(cJSON* property_js)
{
    if (property_js == NULL) return;

    cJSON *params = cJSON_GetObjectItem(property_js, "params"); // 从根 JSON 里取出 key 为`params`的子对象
    if (params == NULL) params = property_js;          /* 有的版本直接给 params  */

    cJSON *item = NULL;
    cJSON_ArrayForEach(item, params) // cJSON 提供的宏遍历`params`对象里**所有子成员**（不是数组遍历是遍历对象的键值对）每次循环`item`指向当前一个子项（temp /light/msg）。
    {
        if (item->string == NULL) continue;

        /* OneNET 的值是嵌套的 {"value":xx}  但也兼容直接给数字  */
        cJSON *val = cJSON_GetObjectItem(item, "value");
        double num = (val != NULL) ? cJSON_GetNumberValue(val) : cJSON_GetNumberValue(item);

        if (strcmp(item->string, DM_KEY_TEMP) == 0)
        {
            ESP_LOGI(TAG_DM, " 云端下发 温度 = %.1f", num);
            char buf[80];
            snprintf(buf, sizeof(buf), "云端下发 温度 %.1f℃", num);
            msg_post(msg_src_cloud, buf);              /*  屏幕上就能看到 （F3/F4 的回报 ）*/
        }
        else if (strcmp(item->string, DM_KEY_LIGHT) == 0)
        {
            ESP_LOGI(TAG_DM, " 云端下发 光照 = %.0f", num);
            char buf[80];
            snprintf(buf, sizeof(buf), "云端下发 光照 %.0f%%", num);
            msg_post(msg_src_cloud, buf);
        }
        /*  云端开关灯（LightSwitch = true/false ）
           OneNET 上建了这个属性  之前设备没接  一直报"未定义"  */
        else if (strcmp(item->string, "LightSwitch") == 0)
        {
            /*  关键修正（照参考工程 template001 的写法 ）
               OneNET 下发的是【LightSwitch: true】原始值 
               不是 {"value":true}   我之前只读 value 字段 
                v 永远为 NULL  on 永远 false  你点"开"日志却打印"关" */
            bool on = false;

            if (cJSON_IsBool(item))          on = cJSON_IsTrue(item);        /* true/false  */
            else if (cJSON_IsNumber(item))   on = (item->valueint != 0);     /* 1/0  */
            else if (cJSON_IsString(item))   on = (strcmp(item->valuestring, "true") == 0 ||
                                                   strcmp(item->valuestring, "1") == 0);
            else {                                                           /* {"value":...}  */
                cJSON *v = cJSON_GetObjectItem(item, "value");
                if (v != NULL) {
                    if (cJSON_IsBool(v))        on = cJSON_IsTrue(v);
                    else if (cJSON_IsNumber(v)) on = (v->valueint != 0);
                    else if (cJSON_IsString(v)) on = (strcmp(v->valuestring, "true") == 0);
                }
            }
            ESP_LOGI(TAG_DM, "云端开关灯 %s", on ? "开" : "关");
            light_cloud_set(on);
        }
        /*  云端调亮度 0~100  */
        else if (strcmp(item->string, "Brightness") == 0)
        {
            cJSON *v = cJSON_GetObjectItem(item, "value");
            cJSON *src = (v != NULL) ? v : item;
            int br = cJSON_IsNumber(src) ? src->valueint : 50;
            if (br < 0) br = 0;
            if (br > 100) br = 100;
            ESP_LOGI(TAG_DM, "云端亮度：%d%%", br);
            light_cloud_set_brightness(br);
        }
        /*  云端 RGB 调色  */
        else if (strcmp(item->string, "RGBColor") == 0)
        {
            cJSON *c = cJSON_GetObjectItem(item, "value");
            if (c == NULL) c = item;
            int r = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(c, "Red"));
            int g = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(c, "Green"));
            int b = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(c, "Blue"));
            if (r < 0) r = 0;
            if (r > 255) r = 255;
            if (g < 0) g = 0;
            if (g > 255) g = 255;
            if (b < 0) b = 0;
            if (b > 255) b = 255;
            ESP_LOGI(TAG_DM, "云端调色：R%d G%d B%d", r, g, b);
            light_cloud_set_rgb((uint8_t)r, (uint8_t)g, (uint8_t)b);
        }
        else if (strcmp(item->string, "msg") == 0) /*  新加的  */
        {
            /* ⚠️ string 类型的值是【文本】 要用 GetStringValue  不是 GetNumberValue  */
            const char *txt = val ? cJSON_GetStringValue(val)
                                  : cJSON_GetStringValue(item);
            if (txt != NULL) {
                msg_post(msg_src_cloud, txt);
                /*  供【云端页】显示最近一条下行消息  */
                snprintf(g_cloud_last_msg, sizeof(g_cloud_last_msg), "%s", txt);
                g_cloud_msg_seq++;
            } /*  原样弹到屏幕  */
        }
        else
        {
            ESP_LOGW(TAG_DM, "云端下发了未定义的属性：%s", item->string);
        }
    }
}