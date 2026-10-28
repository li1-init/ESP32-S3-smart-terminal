#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "wdt.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "living.h"

#define TAG "living_task"
void living_task(void *arg)
{
    static char s_task_buf[2048];
    while (1)
    {
        ESP_LOGI(TAG, "living!!!");
        /*  原来是 vTaskDelay(60 秒) 一次   这 60 秒里一个心跳都没有 
       窗口就只能开到 180 秒  反应太慢 
       现在拆成 12 段  5 秒  每段喂一次心跳   窗口 15 秒就能用  */
        /*  原来 12 段  5 秒 = 每 5 秒喂一次  正好卡在窗口边上  偶尔会漏 
           改成 30 段  2 秒 = 每 2 秒喂一次  余量充足  */
        for (int i = 0; i < 30; i++)
        {
            keep_living(HB_LIVING);
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
        vTaskList(s_task_buf);
        ESP_LOGI(TAG, "\nTask            State  Prio  Stack  Num  Core\n%s", s_task_buf);
        vTaskGetRunTimeStats(s_task_buf);
    }
}
