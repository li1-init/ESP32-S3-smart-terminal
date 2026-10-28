#ifndef _LV_PORT_H_
#define _LV_PORT_H_

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化 LVGL：ST7789 显示 + FT6336U 触摸 + 定时器 tick
 * 注意：调用它之前请先完成 XL9555 的初始化（复位脚/背光挂在 XL9555 上） */
void lv_port_init(void);

/* LVGL 不是线程安全的多任务中访问界面要成对加锁 */
void lv_port_lock(void);
void lv_port_unlock(void);

#ifdef __cplusplus
}
#endif

#endif