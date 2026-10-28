/* ==========================================================================
 *  game2048.h  黑白 2048（LVGL 9）
 *  操作：在画布上上下左右滑动  滑一下走一步 
 *  显示：纯黑底 + 白色细框 + 白字（数值越大越亮  不引入彩色 ）
 * ========================================================================== */
#pragma once
#include "lvgl.h"
#ifdef __cplusplus
extern "C" {
#endif

/* 在 parent 里建一个 2048 游戏（自动铺满 parent ）*/
void game2048_create(lv_obj_t *parent);

/* 重新开局 */
void game2048_restart(void);

#ifdef __cplusplus
}
#endif
