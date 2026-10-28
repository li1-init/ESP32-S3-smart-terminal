/*
 * init.h  初始化总入口
 *
 * 设计原则：
 *   main.c 只负责"按顺序调用 + 起任务"具体初始化分门别类放在这里。
 *   每加一个模块（传感器/消息/低功耗）就在 init_all() 里加一行。
 */
#ifndef _INIT_H_
#define _INIT_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 板级 IO：I2C 总线 + XL9555 扩展芯片
 *  背光/LCD复位/触摸复位/功放使能都挂在 XL9555 上。
 *  ⚠ 必须最先调用后面很多脚靠它 */
void init_board(void);

/** 按键：4 个按键挂在 XL9555 上（IO0_1 ~ IO0_4）
 *  短按打日志长按启动 AP 配网 */
void init_button(void);

/** WiFi：NVS + 连接 STA连上后自动启动 SNTP */
void init_wifi(void);

/** SNTP 时间同步（时区已设为东八区）
 *  同步成功后 time(NULL) 才是准确的当前时间 */
void init_sntp(void);

/** 一次性做完上面全部（按正确顺序） */
void init_all(void);

/** 时间是否已通过 SNTP 校准 */
bool init_sntp_is_synced(void);

/** WiFi 是否已连上路由器。
 *  这是给 UI 层（main/ui/custom/custom.c）用的【强符号】
 *  覆盖那边的弱实现  开机界面就靠它判断"能不能放行" */
bool boot_wifi_connected(void);

#ifdef __cplusplus
}
#endif

#endif /* _INIT_H_ */