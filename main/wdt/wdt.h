#ifndef _WDT_H_
#define _WDT_H_

#include <stdint.h>

/* ==========================================================================
 *  心跳计数看门狗（软件级）
 *
 *   为什么不用 esp_task_wdt（硬件 Task WDT）
 *       它超时就 panic  而 OTA 下载 / flash 擦写要几十秒 
 *         升级到一半被自己重启  轻则升级失败  重则变砖 
 *       本方案是"看计数器有没有变"  有 3 次容错  可控 
 *
 *   监控谁、不监控谁（很重要 ）
 *      监控：lvgl / message / onenetpost / schedule / adc   都是长驻循环 
 *       不监控 begin_task：它开机跑完就 vTaskDelete(NULL) 自杀了 
 *        监控它 = 15 秒后必被误判死机 
 *
 *   时间参数怎么定（你问的 ）
 *     窗口 = 检查周期  容错次数
 *     规则：窗口  任务正常周期  3
 *     这里：5 秒  3 = 15 秒窗口 
 *      所有被监控任务的【喂心跳间隔】必须  5 秒 
 *         adc      2 秒  
 *         lvgl     几十毫秒 
 *         onenet   1~2 秒 
 *         schedule 1 秒 
 *         message  队列阻塞  改成 1 秒超时轮询 （见 task_message.c）
 *         living   原本 60 秒   拆成 12 段  5 秒 （见 living.c）
 * ========================================================================== */

typedef enum {
    HB_LVGL = 0,        /*  不能叫 lvgl_task  会和函数名 lvgl_task() 撞  */
    HB_MESSAGE,
    HB_ONENETPOST,
    HB_SCHEDULE,
    HB_ADC,
    HB_LIVING,          /* living：60 秒一轮  已拆成 12 段  5 秒  */
    HB_MAX              /* 数组长度哨兵  */
} wdt_task_t;

#define WDT_CHECK_PERIOD_S   5      /* 检查周期（秒）*/
#define WDT_MISS_MAX         4      /*  34：窗口 15s20s  给 5 秒级任务留余量  不再偶尔误报  */

extern volatile uint32_t g_hb[HB_MAX];

/* 初始化：建周期性检查定时器 + 打印上次重启原因  */
void wdt_init(void);

/*  任务里每轮调一次：keep_living(HB_ADC); */
void keep_living(wdt_task_t task);

/* 记录"现在走到哪一步"（存在 RTC 内存重启后还能读  黑匣子 ）*/
void wdt_mark_stage(uint32_t stage);

/* 重启前：先打日志 + 记阶段 + 等串口吐完 + 重启  */
void save_reset_reason_and_restart(uint32_t stage);

/* 当前已知的 stage 值（外部只用于打印 ）*/
uint32_t wdt_get_last_stage(void);

#endif /* _WDT_H_ */
