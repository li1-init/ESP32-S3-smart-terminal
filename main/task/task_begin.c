#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "task_begin.h"
#include "init.h"          /* begin_task 里调用了 init_sntp()需要它的声明 */
#include "freertos/event_groups.h"
#include <stdbool.h>
#include "onenet_ota.h"

#define TAG_begin "task_begin"

volatile bool wifi_success = false;
volatile bool sntp_success = false;
volatile bool onenet_success = false;
volatile bool begin_success = false;

/*  这个必须在这里定义（头文件里只写了 extern 声明）
   规则：声明可以有很多份定义必须有且只有一处 */
EventGroupHandle_t begin_event_group = NULL;

bool get_wifi_bool(void)
{
    return wifi_success;
}
bool get_sntp_bool(void)
{
    return sntp_success;
}
bool get_begin_bool(void)
{
    return begin_success;
}

void begin_event_create(void)
{
    begin_event_group = xEventGroupCreate();
}

bool get_onenet_state(void)
{
    return onenet_success;
}



void begin_task(void *arg)
{
    const TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(15000);

    while (1)
    {
        TickType_t now = xTaskGetTickCount();
        if (now >= deadline)
            break; /*  总超时到了  退出 */

        TickType_t remain = deadline - now; /*  只等"剩下的时间" */
        if (remain == 0)
            remain = 1; /* ⚠ 千万别传 0：0 = 非阻塞  会变轮询  */

        EventBits_t bits = xEventGroupWaitBits(begin_event_group,
                                               wifi_connected_BIT | sntp_connected_BIT,
                                               pdTRUE,  /* 清位  */
                                               pdFALSE, /* 来一个算一个  */
                                               remain); /*  剩余时间 */

        if (bits & wifi_connected_BIT)
            wifi_success = true;
        if (bits & sntp_connected_BIT)
            sntp_success = true;
        if (bits & onenet_connected_BIT)
            onenet_success = true;
        if (wifi_success && sntp_success && onenet_success && get_my_onenet_ota_upload_version())
            break; /*  都齐了  退出 */
    }

    /*  公共尾巴：不管走哪条路都放行（原来你写了 3 遍  容易漏改）*/
    begin_success = true;
    vTaskDelete(NULL);
}
