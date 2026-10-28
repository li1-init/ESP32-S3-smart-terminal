/*
 * ESP32-S3 智能终端  主程序
 *
 * 这里只做三件事：
 *   1) 调 init_all() 完成板级/外设初始化（细节在 main/init/init.c）
 *   2) 初始化 LVGL 显示和触摸（main/port/lv_port.c）
 *   3) 把 LVGL 主循环和生命任务交给独立任务跑（main/task/）
 *
 *  目录约定：
 *     board/  板级引脚定义        port/   LVGL 移植层（屏幕/触摸）
 *     init/   初始化              task/   FreeRTOS 任务
 *     ui/     GUI Guider 界面 + custom.c
 *     以后加模块（传感器/消息/低功耗）都在 main 下新建文件夹
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "lvgl.h"
#include "board_config.h"
#include "lv_port.h"
#include "xl9555.h"

#include "init.h"
#include "lvgl_task.h"
#include "living.h"
#include "task_begin.h"
#include "task_message.h"
#include "adc.h"
#include "esp_app_desc.h"      /*  查版本号用  */
#include "task_schedule.h"
#include "task_onenetpost.h"
#include "adc.h"



static const char *TAG = "app";

/* ==================== 入口 ==================== */
void app_main(void)
{
    /*  开机先把【版本号】打出来 
       以后再遇到"我烧了但没生效" 一眼就能看出是不是新固件 
        version     = git describe 生成的（2d87439-dirty 里 dirty = 有未提交改动 ）
        compile_time= 编译时间  比它新就是新固件 
        idf_ver     = IDF 版本  */
    {
        const esp_app_desc_t *app = esp_app_get_description();
        ESP_LOGI(TAG, "=== esp32s3 智能终端 启动 ===");
        ESP_LOGI(TAG, "版本: %s", app->version);
        ESP_LOGI(TAG, "编译时间: %s %s", app->date, app->time);   /*  字段叫 date/time  不是 compile_time  */
        ESP_LOGI(TAG, "IDF: %s", app->idf_ver);
    }
    begin_event_create();
    task_message_createqueue();
    /* 1. 板级 + 按键 + WiFi（连上后自动 SNTP） */
    init_all();
    adc_init();             /*  F1：ADC 采样（IO8=热敏 IO9=光敏）每 2 秒串口打印  */
    /* 2. LCD + 触摸 + LVGL（只初始化不跑循环） */
    lv_port_init();
    /* 3. 到这里 LCD 已经清屏完成才开背光避免开机闪一下 */
    xl9555_pin_write(BSP_LCD_BL_IO, 1);
    /* 4. LVGL 主循环交给独立任务（栈 8KB）生命任务做心跳 */
    lvgl_task_start();
    /* 5. 生命任务做心跳 */
    xTaskCreatePinnedToCore(living_task, "living", 4096, NULL, 1, NULL, 1);
    /* 6. 开机任务 */
    xTaskCreatePinnedToCore(begin_task, "begin", 4096, NULL, 2, NULL, 1);
    /* 7. 消息任务 */
    xTaskCreatePinnedToCore(task_message_task, "task_message", 4096, NULL, 2, NULL, 1);
    /* 8. 日程计划任务 */
    xTaskCreatePinnedToCore(schedule_task, "schedule", 4096, NULL, 1, NULL, 1);
    /* 9. 上传属性任务 */
    xTaskCreatePinnedToCore(onenetpost_task, "onenetpost", 4096, NULL, 4, NULL, 1);
    /* 10. ADC 打印任务（C 里只能写具名函数  没有 lambda ）*/
    xTaskCreatePinnedToCore(adc_task, "adc", 3072, NULL, 2, NULL, 1);
    /* app_main 到此结束即可main 任务会自动删除 */
}