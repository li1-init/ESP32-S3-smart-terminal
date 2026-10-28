/*
 * lvgl_task.c  LVGL 主循环
 *
 * 流程：
 *   1) 建界面（setup_ui 建开机页custom_init 挂流程和回调） 要加 LVGL 锁
 *   2) 进主循环：反复调 lv_timer_handler()然后按它的返回值休眠
 *
 *  lv_timer_handler() 的返回值 = "下一次再来找我干活最多可以等多久"
 *   限幅在 5~20ms 之间的原因写在下面循环里。
 */
#include "freertos/FreeRTOS.h"
#include "wdt.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "lvgl.h"
#include "lv_port.h"
#include "ui.h"        /*  全新黑白 UI  */

#include "lvgl_task.h"

#define TAG_LVGL "lvgl"

/* LVGL 任务栈大小（字节）和优先级 */
#define LVGL_TASK_STACK 8192
#define LVGL_TASK_PRIO  8

static void lvgl_task(void *arg)
{
    (void)arg;

    /* 建界面：setup_ui() 只创建开机页其它界面是第一次切过去时才创建 */
    lv_port_lock();
    ui_init();                    /*  全新 UI 入口  */
    lv_port_unlock();
    ESP_LOGI(TAG_LVGL, "UI created, enter main loop");

    while (1)
    {
    
    keep_living(HB_LVGL);   /*  心跳：我活着  */
        lv_port_lock();
        uint32_t delay_ms = lv_timer_handler(); // 才是让它们【动起来】的那台发动机
        lv_port_unlock();

        /* 限幅兜底防止两种情况：
         *   太小  CPU 空转跑满（返回 0 时 vTaskDelay(0) 不阻塞）
         *   太大  触摸响应变慢（返回 100ms 就变成 100ms 才处理一次触摸） */
        if (delay_ms < 5)
            delay_ms = 5;
        if (delay_ms > 20)
            delay_ms = 20;

        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

void lvgl_task_start(void)
{
    xTaskCreatePinnedToCore(lvgl_task, "lvgl", LVGL_TASK_STACK, NULL,
                            LVGL_TASK_PRIO, NULL, 1);
    ESP_LOGI(TAG_LVGL, "lvgl task started");
}