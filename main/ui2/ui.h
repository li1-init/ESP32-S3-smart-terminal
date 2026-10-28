/*
 * ============================================================================
 *  ui.h  全新黑白 UI（纯黑底 + 纯白字零彩色）
 *
 *  设计规则（用户定稿）：
 *     屏幕分左右两区：左侧=功能内容区右侧=弧形滚动菜单区
 *     右侧弧形菜单【永远存在】 只有左侧内容区切换页面 
 *     左内容区内容【绝不能越过】分割线 
 *     菜单沿弧线滑动  触摸左右滑 + 惯性  选中项放大、其余缩小 
 *     页面切换必须淡入淡出/平移过渡  禁止硬切 
 *    ⑥ 息屏页是全屏独立页  进息屏时菜单被覆盖隐藏  唤醒后回主布局 
 *
 *  代码风格：
 *     每个页面一个初始化函数：ui_home_init() / ui_wifi_init() ...
 *     页面函数【只建 UI】 业务通过 ui_on_page_enter() 等钩子挂载 
 *     所有坐标基于屏幕尺寸动态计算  不写死 
 *
 *   弧形菜单选中判定位置：ui.c 里搜 "弧形选中判定"
 *   页面切换动画位置：     ui.c 里搜 "页面切换动画"
 *   息屏页面入口位置：     ui.c 里搜 "息屏入口"
 * ============================================================================
 */
#ifndef _UI_H_
#define _UI_H_

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- 页面 ID（顺序 = 右侧菜单顺序）---------------- */
typedef enum {
    UI_PAGE_HOME = 0,   /* 主菜单首页（上电默认） */
    UI_PAGE_MSG,        /* 消息页 */
    UI_PAGE_SET,        /* 设置页 */
    UI_PAGE_OTA,        /* 版本更新页 */
    UI_PAGE_CAL,        /* 日历页 */
    UI_PAGE_SCHED,      /* 日程页 */
    UI_PAGE_WEATHER,    /* 天气页 */
    UI_PAGE_AI,         /* 小智AI 页 */
    UI_PAGE_GAME,       /* 游戏页 */
    UI_PAGE_WIFI,       /* WiFi 配置页 */
    UI_PAGE_CLOUD,       /* 云端通信页 */
    UI_PAGE_SENSOR,      /* 光照温度页 */
    UI_PAGE_OFF,        /* 息屏页（全屏独立  不进弧形菜单）*/
    UI_PAGE_COUNT
} ui_page_t;

/* ---------------- 生命周期 ---------------- */
void      ui_init(void);                 /* 建屏 + 弧形菜单 + 首页  */
void      ui_goto(ui_page_t page);       /* 切页（带过渡动画 ）*/
ui_page_t ui_current_page(void);

/* ---------------- 页面对象句柄（给业务挂控件用 ）---------------- */
/* 返回该页面的根容器；还没创建过会【立即创建】 拿到后往里加控件即可  */
lv_obj_t *ui_page_root(ui_page_t page);

/* 左内容区根容器（一般用不到  调试用）*/
lv_obj_t *ui_content_area(void);

/* ---------------- 页面内容便捷设置（业务直接用 ）---------------- */
void ui_home_set_clock(const char *date, const char *time_str);   /* 首页 日期/时间  */
void ui_home_set_info(const char *txt);                          /* 首页 传感器一行  */
void ui_ota_set_versions(const char *cur, const char *newv, const char *status);

/* ---------------- 息屏 ---------------- */
/* 息屏入口
 *   bg 传 NULL 就是纯黑 
 *   bg 传你自己的图片描述符（lv_image_dsc_t*）就是那张背景图 
 *   ⚠️ 图片怎么来：用 LVGL 官方在线转换器 / GUI Guider 把 PNG 转成 C 数组 
 *      然后 ui_screenoff_set_bg(&your_img); 或直接调 ui_screenoff_show(&your_img); */
void ui_screenoff_show(const lv_image_dsc_t *bg);
void ui_screenoff_hide(void);            /* 也支持触摸自动唤醒  */

/* ---------------- 业务钩子（弱符号  你在别处给强实现）----------------
 *  ⚠️ 全部在【LVGL 任务】里被调用  可以安全操作控件 
 *  ⚠️ 但【不要】在这里做阻塞操作 （HTTP/MQTT 请起任务）
 * ------------------------------------------------------------------- */
void ui_on_page_enter(ui_page_t page);   /* 页面已经切进来（动画开始时）*/
void ui_on_page_leave(ui_page_t page);   /* 旧页面要切走时 */
void ui_on_tick(uint32_t ms);            /* 每 500ms 一次刷时钟用  */

#ifdef __cplusplus
}
#endif

#endif /* _UI_H_ */
