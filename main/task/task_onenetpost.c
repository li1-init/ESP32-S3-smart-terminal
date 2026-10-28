#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "wdt.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "task_onenetpost.h"
#include "task_begin.h"
#include "cJSON.h"        /*  托管组件的头就是 cJSON.h  不是 cJSON/cJSON.h  */
#include "onenet_mqtt.h"
#include "onenet_dm.h"

#define TAG_ONENETPOST_TASK "onenetpost_task"

/*  上报周期（毫秒） 想改快慢就改这一个数 
   1000 = 1 秒：云端折线图会比较"波浪"（点密 ）
   ⚠️ 但注意 OneNET 的消息条数额度 ：1 秒一次 = 8.6 万条/天 
      嫌额度费就改 2000 或 3000  */
#define ONENET_POST_PERIOD_MS   2000

void onenetpost_task(void *arg)
{
    (void)arg;
    while (1)
    {
    
    keep_living(HB_ONENETPOST);   /*  心跳：我活着  */
        if (!get_onenet_state())                 /* 没连上就不发  */
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        cJSON *js = onenet_property_upload_dm();       /*  拼 JSON  */
        if (js == NULL) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
        char *s = cJSON_PrintUnformatted(js);          /*  转字符串  */
        if (s == NULL)                                 /*  内存不够时会返回 NULL  */
        {                                              /*    不判空就会 strlen(NULL) 崩  */
            cJSON_Delete(js);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        onenet_post_property_data(s);                  /*  发  */
        cJSON_free(s);                                 /*  先放字符串  */
        cJSON_Delete(js);                              /*  再放 JSON  顺序别反  */
        vTaskDelay(pdMS_TO_TICKS(ONENET_POST_PERIOD_MS));
    }
}