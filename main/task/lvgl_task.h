/*
 * lvgl_task.h  LVGL 主循环任务
 *
 * 为什么必须单独一个任务：
 *   main 任务默认栈只有 CONFIG_ESP_MAIN_TASK_STACK_SIZE(3584) 字节
 *   在里面直接跑 LVGL 会在刷新/回调时栈溢出  看门狗重启。
 *   所以给 LVGL 单独开一个 8KB 栈的任务。
 */
#ifndef _LVGL_TASK_H_
#define _LVGL_TASK_H_

#ifdef __cplusplus
extern "C" {
#endif

/** 创建并启动 LVGL 任务。
 *  ⚠ 必须先调用 lv_port_init()（LVGL 内核 + 显示 + 触摸初始化完成） */
void lvgl_task_start(void);

#ifdef __cplusplus
}
#endif

#endif /* _LVGL_TASK_H_ */