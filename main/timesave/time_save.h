/* ==========================================================================
 *  time_save.h  把时间存到【掉电不丢】的地方（NVS）
 *
 *  为什么要它：
 *    设备一断网/重启系统时间会退回到 1970 （照片里就是 1970-01-01 ）
 *     开机先从 NVS 读回上次的时间  立刻可用 
 *     再尝试联网（SNTP）校准  成功后再存回去 
 * ========================================================================== */
#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

/* 开机最早调：从 NVS 读时间如果当前时间明显不对（<2024年）就套用  */
void time_save_init(void);

/* 起一个后台任务：等 SNTP 同步成功后保存之后每 10 分钟存一次  */
void time_save_start_auto(void);

/* 立刻保存当前时间（一般不用手动调 ）*/
void time_save_now(void);

#ifdef __cplusplus
}
#endif
