/*
 * ============================================================================
 *  ui.c  全新黑白 UI 实现（纯黑底 / 纯白字 / 无彩色）
 *
 *  整体结构：
 *
 *      ┌───────────────────┬──────────────┐
 *      │                   │   ╭──────╮   │   弧形菜单（永远存在 ）
 *      │   左内容区         │   │ 主页 │   │
 *      │  （可切页 ）      │   ╰──────╯   │
 *      │                   │     ╭──╮     │
 *      │                   │     │消息│    │
 *      │                   │     ╰──╯     │
 *      └───────────────────┴──────────────┘
 *             竖虚线（仅布局参考界面不渲染 ）
 *
 *   三条硬规则（写在注释里改代码时别破坏）：
 *     右侧菜单是本文件里 s_menu 这个对象  任何页面都不删它 
 *     左内容区 = s_content  尺寸就是 LEFT_W子控件超出会被裁掉 
 *     页面切换只动 s_pages[] 里的对象  不动 s_menu 
 * ============================================================================
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "lvgl.h"
#include "board_config.h"          /* BSP_LCD_WIDTH / BSP_LCD_HEIGHT */
#include "init.h"                   /* boot_wifi_connected  */
#include "weather.h"                /*  天气模块（template001 移植 ）*/
#include "game2048.h"               /*  2048 游戏  */
#include "ai_chat.h"                /*  DeepSeek 问答  */
#include "task_begin.h"
#include "esp_system.h"          /* esp_get_free_heap_size  */
#include "adc.h"                /*  首页显示真实温度/光照  */
#include "esp_wifi.h"           /*  WiFi 页扫描  */
#include <stdlib.h>             /* calloc/free  */
#include <stdint.h>
#include "ota_app.h"
#include "onenet_ota.h"
#include "task_schedule.h"
#include "task_message.h"
#include "ui.h"

/* AI 页用到的变量（提前声明，示例按钮回调要用）*/
static lv_obj_t *s_ai_state = NULL;
static lv_obj_t *s_ai_ans   = NULL;
static int       s_ai_last  = -1;

static void sched_rebuild(void);   /* 前向声明：日程列表重建 */
static void nw_sched_add(lv_event_t *e);
static void on_wifi_pick(lv_event_t *e);
static void nw_make_kb(lv_obj_t *parent, lv_obj_t *ta);

/* 前向声明 */
static void nw_dot_create(lv_obj_t *parent);
static void nw_dot_update(void);

#include "onenet_mqtt.h"     /* onenet_post_property_data */

/* 前置声明（定义在后面）*/
static lv_obj_t *page_scaffold(lv_obj_t *root, const char *title, const char *hint);


#define TAG_UI "ui"

/* ==========================================================================
 *  0. 外部字体（GUI Guider 生成的已链接进工程 ）
 * ========================================================================== */
extern const lv_font_t lv_font_ZiTiQuanWeiJunHeiW22_18;
extern const lv_font_t lv_font_ZiTiQuanWeiJunHeiW22_40;

/*  菜单图标（从旧 UI 的 git 提交里恢复的  全白单色 ）*/
LV_IMAGE_DECLARE(icon_message);
LV_IMAGE_DECLARE(_Set_White_RGB565A8_48x48);
LV_IMAGE_DECLARE(_Cal_Released_RGB565A8_48x48);
LV_IMAGE_DECLARE(_Weather_Released_RGB565A8_48x48);
LV_IMAGE_DECLARE(_GPT_Released_RGB565A8_48x48);
LV_IMAGE_DECLARE(_Game_Released_RGB565A8_48x48);
LV_IMAGE_DECLARE(_wifi_RGB565A8_24x24);
LV_IMAGE_DECLARE(_terminal_RGB565A8_94x93);
LV_IMAGE_DECLARE(screen_off_bg);   /*  息屏页背景（用户给的图转的 ）*/
LV_IMAGE_DECLARE(home_pet);
/*  菜单卡片边框（我生成的 3 套  你挑 ）*/
LV_IMAGE_DECLARE(menu_frame_round);
LV_IMAGE_DECLARE(menu_frame_corner);
LV_IMAGE_DECLARE(menu_frame_bar);
/*  菜单图标（我生成的统一套  40x40 白线透底 ）*/
LV_IMAGE_DECLARE(ic_home);
LV_IMAGE_DECLARE(ic_msg);
LV_IMAGE_DECLARE(ic_set);
LV_IMAGE_DECLARE(ic_ota);
LV_IMAGE_DECLARE(ic_cal);
LV_IMAGE_DECLARE(ic_sched);
LV_IMAGE_DECLARE(ic_weather);
LV_IMAGE_DECLARE(ic_ai);
LV_IMAGE_DECLARE(ic_game);
LV_IMAGE_DECLARE(ic_wifi);
LV_IMAGE_DECLARE(ic_cloud);
LV_IMAGE_DECLARE(ic_temp);
        /*  首页左区卡通图（用户给的图 ）*/

#define FONT_SMALL  (&lv_font_ZiTiQuanWeiJunHeiW22_18)
#define FONT_BIG    (&lv_font_ZiTiQuanWeiJunHeiW22_40)

/* ==========================================================================
 *  1. 几何参数（全部由屏幕尺寸推导  不写死坐标 ）
 * ========================================================================== */
#define SCR_W        BSP_LCD_WIDTH                  /* 320 */
#define SCR_H        BSP_LCD_HEIGHT                 /* 240 */

/* 竖虚线位置：左内容区占 64%右侧留给菜单  想改比例改这一个数  */
#define DIVIDER_RATE 64
#define DIVIDER_X    ((SCR_W) * DIVIDER_RATE / 100) /* ≈ 204 */
#define LEFT_W       (DIVIDER_X)                    /* 左内容区宽  */
#define MENU_X0      (DIVIDER_X + 4)                /* 菜单基准 x  */
#define MENU_W       (SCR_W - MENU_X0)              /* 菜单区宽 ≈ 112 */

/* 菜单项的视觉尺寸 */
#define ITEM_W       (MENU_W - 16)   /* 留边放大时不出界  */                  /* ≈ 102 */
#define ITEM_H       72        /*  图标上 + 文字下  需要更高  */
#define ITEM_SPACING 78                             /*  配合 72 高的条目  3 个可见  */
#define ARC_BULGE    34                             /*  1634：弯曲更明显  之前太小几乎看不出弧  */
#define ARC_MAX_U    3.0f                           /* 超过 3 项就不再更弯  */

/* 缩放/透明度随距离衰减 */
#define SCALE_BASE   256                            /* LVGL: 256 = 100%  */
#define SCALE_STEP   34                             /* 每远离 1 项缩小 34/256  */
#define OPA_STEP     58

/* 页面切换动画时长（毫秒） */
#define PAGE_ANIM_MS   180
#define PAGE_ANIM_DX   18

/* 菜单物理参数 */
#define IDLE_OFF_MS     30000      /*  闲置多久自动息屏（毫秒） 10 秒太短  一开机就黑屏看不见菜单  改 30 秒  */

#define PHYS_PERIOD_MS  16                          /* ~60fps  */
#define FRICTION        0.78f                       /*  0.900.78：惯性衰减更快  滑不远  切换变慢  */
#define MIN_VEL         0.0025f                     /* 低于这个速度就停  */
#define MAX_VEL         0.045f                      /*  速度上限：一次最多滑这么快  防止"甩一下飞出好几格" */
#define SEL_THRESHOLD   0.62f                       /*  选判定阈值：0.5=一半  改成 0.62 更"迟钝" 切换更慢  */
#define UI_BOOT_GUARD_MS 4000                       /*  开机 4 秒内【忽略所有触摸】 触摸面板上电有噪声  会误切页  */
#define UI_DRAG_DEADZONE 8                          /*  拖动死区：累计移动不足 8px 不算滑动  防抖  */                       /*  选判定阈值：0.5=一半  改成 0.62 更"迟钝" 切换更慢  */
#define RUBBER           0.35f                       /* 越界回弹强度  */

/* ==========================================================================
 *  2. 静态状态
 * ========================================================================== */
static lv_obj_t  *s_scr       = NULL;      /* 整个屏幕  */
static lv_obj_t  *s_content   = NULL;      /* 左内容区  */
static lv_obj_t  *s_menu      = NULL;      /* 右弧形菜单容器  永不删除  */

static lv_obj_t  *s_pages[UI_PAGE_COUNT] = {0};   /* 各页面根容器（懒创建 ）*/
static ui_page_t  s_cur       = UI_PAGE_HOME;
static bool       s_switching = false;

/* 菜单项对象 + 它们对应的页面  */
static lv_obj_t  *s_items[UI_PAGE_COUNT] = {0};
static ui_page_t  s_menu_pages[UI_PAGE_COUNT] = {0};
static int        s_menu_cnt = 0;
static int        s_card_style = 4;   /* 开机默认风格5（四角括号），按钮仍可循环切换 */
static void       menu_apply_style(void);   /*  前置声明（放在最前面 ）*/   /*  卡片风格 0/1/2  设置页可切  */

/* 菜单滚动状态（单位：项  可以是小数  才能平滑 ）*/
static float      s_scroll = 0.0f;
static float      s_vel    = 0.0f;
static int        s_sel    = 0;
static int        s_page_raw = 0;   /*  已经切到哪一页（原始索引） 防止重复切页  */
static bool       s_drag   = false;
static bool       s_layout_done = false;   /* 布局是否已就绪  用于空闲早退  */
static lv_point_t s_drag_last;
static int32_t    s_drag_move = 0;   /* 本次按下累计移动的像素（配合死区 ）*/

/* 息屏页 */
static lv_obj_t  *s_off        = NULL;
static lv_obj_t  *s_off_date   = NULL;
static lv_obj_t  *s_off_time   = NULL;

/* ---------------- 弱符号业务钩子  ---------------- */
__attribute__((weak)) void ui_on_page_enter(ui_page_t page) { (void)page; }
__attribute__((weak)) void ui_on_page_leave(ui_page_t page) { (void)page; }
__attribute__((weak)) void ui_on_tick(uint32_t ms)          { (void)ms; }

/* ==========================================================================
 *  3. 小工具
 * ========================================================================== */

/* 把一个对象刷成"纯黑底 + 无边框 + 白字"的干净样式  */
static void style_black(lv_obj_t *o, bool transparent)
{
    lv_obj_remove_style_all(o);
    lv_obj_set_style_bg_opa(o, transparent ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    if (!transparent) lv_obj_set_style_bg_color(o, lv_color_black(), 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_color(o, lv_color_white(), 0);
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *txt, const lv_font_t *font)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_label_set_text(l, txt);
    return l;
}

/* 动画执行回调（LVGL 9 里 lv_obj_set_style_opa 有 3 个参数  不能直接当回调 ）*/
static void anim_set_opa(void *var, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}
static void anim_set_x(void *var, int32_t v)
{
    lv_obj_set_x((lv_obj_t *)var, v);
}

static void anim_start(lv_obj_t *obj, lv_anim_exec_xcb_t cb,
                       int32_t from, int32_t to, uint32_t ms,
                       lv_anim_ready_cb_t ready)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    if (ready) lv_anim_set_ready_cb(&a, ready);
    lv_anim_start(&a);
}

/* ==========================================================================
 *  4. 弧线数学（ 菜单"弧形"就靠这三个函数 ）
 *
 *     u = 该菜单项与"当前滚动位置"的距离（单位：项  可以是小数）
 *        u = 0       正好在中心  最大最亮
 *        u = 1      上/下一项 
 *
 *     横向：x = 基准 + 弯曲量  (|u| / 3)²
 *         离中心越远越往右缩  视觉上就是一段【圆弧】
 *     纵向：y = 屏幕中心 + u  间距
 *         这是"沿弧线排布"的纵向部分 
 * ========================================================================== */
static int32_t arc_x(float u)
{
    float au = fabsf(u);
    if (au > ARC_MAX_U) au = ARC_MAX_U;
    /*  条目现在直接挂在【屏幕】上  所以这里返回【屏幕绝对坐标】
       之前挂在菜单容器里、却给了绝对坐标   飘出屏幕 
       后来改成相对坐标又被容器的布局/裁剪吃掉 
        最终方案：条目挂屏幕 + 绝对坐标  实测最稳  */
    return (int32_t)(MENU_X0 + 4 + ARC_BULGE * (au * au) / (ARC_MAX_U * ARC_MAX_U));
}

static int32_t arc_y(float u)
{
    return (int32_t)((SCR_H / 2) + u * ITEM_SPACING - ITEM_H / 2);
}

static int32_t arc_scale(float u)
{
    float au = fabsf(u);
    if (au > ARC_MAX_U) au = ARC_MAX_U;
    int32_t s = SCALE_BASE - (int32_t)(au * SCALE_STEP);
    return (s < 90) ? 90 : s;                 /* 最小别小于 35%  否则看不清  */
}

static lv_opa_t arc_opa(float u)
{
    float au = fabsf(u);
    if (au > ARC_MAX_U) au = ARC_MAX_U;
    int32_t o = 255 - (int32_t)(au * OPA_STEP);
    if (o < 70) o = 70;
    return (lv_opa_t)o;
}

/* 把菜单所有项按当前 s_scroll 重新摆一遍 （每 16ms 调一次  就是平滑动画 ）*/
static void menu_layout(void)
{
    for (int i = 0; i < s_menu_cnt; i++) {
        lv_obj_t *it = s_items[i];
        if (it == NULL) continue;

        /*  循环菜单：把偏移折到 [-n/2, n/2] 
            滚到最后一项继续滑会从第一项接上来  无限循环  */
        float u = (float)i - s_scroll;
        float half = (float)s_menu_cnt / 2.0f;
        while (u >  half) u -= (float)s_menu_cnt;
        while (u < -half) u += (float)s_menu_cnt;

        /*  必须用一次 set_pos 
           分开写 set_x / set_y 会互相覆盖 
           （set_x 内部是 set_pos(x, 旧y)  set_y 又是 set_pos(旧x, y) ）
            实测结果是 y 永远为 0  条目全叠在顶部  */
        lv_obj_set_pos(it, arc_x(u), arc_y(u));

        /*  只有选中项最大  其余按距离缩小  而且是"连续变化"不是突变  */
        lv_obj_set_style_transform_scale_x(it, arc_scale(u), 0);
        lv_obj_set_style_transform_scale_y(it, arc_scale(u), 0);
        lv_obj_set_style_opa(it, arc_opa(u), 0);
    }
    s_layout_done = true;      /*  摆完一帧就标记  */
}

/* ==========================================================================
 *  5. 页面切换动画（页面切换动画  核心就在这里 ）
 *
 *  做法：新页从右侧 +18px 淡入  旧页向左 -18px 淡出 
 *        两个动画同时跑  视觉上是"推拉过渡"  不是硬切 
 *   注意：只动 s_pages[] 里的对象  绝不动 s_menu 
 * ========================================================================== */
static void on_old_faded(lv_anim_t *a)
{
    lv_obj_t *old = (lv_obj_t *)a->var;
    if (old) lv_obj_add_flag(old, LV_OBJ_FLAG_HIDDEN);   /* 淡出完就藏起来  不删  下次复用  */
    s_switching = false;
}

static void page_switch(ui_page_t page)
{
    if (page >= UI_PAGE_COUNT) return;
    if (page == s_cur && s_pages[page] != NULL && !s_switching) return;

    lv_obj_t *old = s_pages[s_cur];
    lv_obj_t *nw  = ui_page_root(page);          /* 懒创建  */
    if (nw == NULL) return;

    s_switching = true;

    /* ----  旧页：通知业务 + 淡出左移 ---- */
    if (old != NULL && old != nw) {
        ui_on_page_leave(s_cur);
        anim_start(old, (lv_anim_exec_xcb_t)anim_set_opa, 255, 0, PAGE_ANIM_MS, on_old_faded);
        anim_start(old, (lv_anim_exec_xcb_t)anim_set_x, 0, -PAGE_ANIM_DX, PAGE_ANIM_MS, NULL);
    } else {
        s_switching = false;
    }

    /* ----  新页：从右侧淡入 ---- */
    lv_obj_remove_flag(nw, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_x(nw, PAGE_ANIM_DX);
    lv_obj_set_style_opa(nw, LV_OPA_TRANSP, 0);
    anim_start(nw, (lv_anim_exec_xcb_t)anim_set_opa, 0, 255, PAGE_ANIM_MS, NULL);
    anim_start(nw, (lv_anim_exec_xcb_t)anim_set_x, PAGE_ANIM_DX, 0, PAGE_ANIM_MS, NULL);

    s_cur = page;
    ui_on_page_enter(page);                      /*  业务挂载点  */

    ESP_LOGI(TAG_UI, "切页  %d", (int)page);
}

void ui_goto(ui_page_t page)
{
    if (page >= UI_PAGE_COUNT) return;

    /* 同步弧形菜单到该页 （点菜单进来的和代码调进来的表现一致 ）*/
    for (int i = 0; i < s_menu_cnt; i++) {
        if (s_menu_pages[i] == page) {
            s_sel = i;
            s_scroll = (float)i;
            s_vel = 0.0f;
            menu_apply_style();          /*  应用卡片风格  */
    menu_layout();
            break;
        }
    }
    page_switch(page);
}

ui_page_t ui_current_page(void) { return s_cur; }
lv_obj_t *ui_content_area(void) { return s_content; }

/* ==========================================================================
 *  6. 菜单物理（弧形选中判定  就在这个定时器里 ）
 * ========================================================================== */
static void on_selection_changed(int new_sel);

static void menu_physics_cb(lv_timer_t *t)
{
    (void)t;
    if (s_menu_cnt <= 0) return;

    /*  关键性能修复（解决自动重启 ）
       原来每 16ms 都给 10 个对象 set_pos + set_scale + set_opa 
        LVGL 判定"脏了" 每帧整片重绘   CPU1 跑满 100% 
        空闲任务被饿死  task_wdt 报警   最终触发重启 
       现在：静止（没拖、没速度、已归位） 立刻 return  一帧都不画  */
    {
        float snap = floorf(s_scroll + 0.5f);
        bool idle = (!s_drag) && (fabsf(s_vel) < MIN_VEL) &&
                    (fabsf(s_scroll - snap) < 0.02f);
        if (idle && s_layout_done) return;
    }

    /* ----------  惯性推进 ---------- */
    if (!s_drag && fabsf(s_vel) > MIN_VEL) {
        if (s_vel >  MAX_VEL) s_vel =  MAX_VEL;      /*  速度上限  */
        if (s_vel < -MAX_VEL) s_vel = -MAX_VEL;
        s_scroll += s_vel;
        s_vel *= FRICTION;
    } else if (!s_drag) {
        s_vel = 0.0f;
    }

    /* ----------  循环菜单：不做越界回弹 
       原来这里把 s_scroll 夹在 [0, n-1]   滑到最后一项就动不了了 
        你从"主页"往上滑永远到不了 "WiFi" 
       现在：s_scroll 允许无限增长  显示时用取模折返  = 真正的循环  */

    /* ----------  松手后吸附到最近项（有惯性时等速度降下来 ）---------- */
    if (!s_drag && fabsf(s_vel) <= MIN_VEL) {
        float near = floorf(s_scroll + 0.5f);
        float d    = near - s_scroll;
        if (fabsf(d) > 0.001f) s_scroll += d * 0.25f;    /* 平滑归位  */
        else s_scroll = near;
    }

    menu_layout();                                        /* 重摆  视觉平滑  */

    /* ============================================================
     * 弧形选中判定 
     *  规则（用户定）：一个菜单模块【有一半跨过】中心判定线  选中它 
     *  实现：
     *     s_scroll 是"连续滚动位置"（单位=项）
     *     当 |s_scroll - s_sel|  0.5    说明选中的那项已经跨过一半了 
     *      把 s_sel 往那个方向挪一格并切页 
     *  ⚠️ 用 0.5 而不是 1.0 就是"一半"这个规则的字面实现 
     * ============================================================ */
    /* ============================================================
     *  滑动重启的真凶就在这段 
     *
     *  原来：滚动过程中一超过 0.62 项【立刻切页】
     *         快速一甩会连续触发 5~10 次切页 
     *         每次 page_switch 都【现场创建新页面 + 两个动画】
     *         内存/CPU 瞬间冲高  崩溃重启 
     *
     *  现在：把"选中"和"切页"拆开 
     *         滚动中：只移动高亮（s_sel 跟着变 ）不切页 
     *         松手静止后：只按最终落点切【一次】页 
     *         一次滑动 = 一次切页  又顺又稳 
     * ============================================================ */
    float nearest = floorf(s_scroll + 0.5f);
    int   ni_raw  = (int)nearest;

    /* 滚动中只更新"当前高亮"绝不切页  */
    if (ni_raw != s_sel) {
        s_sel = ni_raw;
    }

    /* 只有在【松手 + 速度归零 + 已吸附就位】时才真正切页  */
    bool settled = (!s_drag) && (fabsf(s_vel) < MIN_VEL) &&
                   (fabsf(s_scroll - nearest) < 0.06f);
    if (settled && ni_raw != s_page_raw) {
        s_page_raw = ni_raw;                 /* 记住已切到哪一页  防重复切  */
        on_selection_changed(ni_raw);
    }
}

static void on_selection_changed(int new_sel)
{
    if (s_menu_cnt <= 0) return;

    /*  循环菜单：把可能为负、可能超界的索引折回 [0, n-1] 
       C 语言的 % 对负数会得到负数  所以要多加一次 n 再取模  */
    int idx = ((new_sel % s_menu_cnt) + s_menu_cnt) % s_menu_cnt;

    ui_page_t p = s_menu_pages[idx];
    ESP_LOGI(TAG_UI, "弧形菜单选中 %d(第 %d 项)  页面 %d", new_sel, idx, (int)p);
    page_switch(p);
}

/* ==========================================================================
 *  7. 菜单触摸：左右/上下滑动 + 松手惯性
 * ========================================================================== */
static void menu_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED) {
        /*  开机保护期：面板上电瞬间会报假触摸  直接丢弃  */
        if (lv_tick_get() < UI_BOOT_GUARD_MS) return;
        s_drag      = true;
        s_vel       = 0.0f;
        s_drag_move = 0;
        lv_indev_t *indev = lv_indev_active();
        if (indev) lv_indev_get_point(indev, &s_drag_last);

    } else if (code == LV_EVENT_PRESSING) {
        lv_indev_t *indev = lv_indev_active();
        if (indev == NULL) return;

        lv_point_t p;
        lv_indev_get_point(indev, &p);

        if (!s_drag) return;                     /* 没按下就不算  */

        int32_t dy = p.y - s_drag_last.y;
        s_drag_last = p;

        /*  死区：累计位移不到 UI_DRAG_DEADZONE 之前【不动】
              手指轻放/面板抖动都会被吃掉  不会误切页  */
        s_drag_move += (dy < 0 ? -dy : dy);
        if (s_drag_move < UI_DRAG_DEADZONE) return;

        /* 手指往上滑  内容往上走  选中项往下一个  */
        float d = -(float)dy / (float)ITEM_SPACING;
        s_scroll += d;
        s_vel = d;                         /* 记录瞬时速度松手后当惯性用  */

    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        s_drag = false;
        /* 松手时把最后一帧速度放大一点看起来"甩出去"更有惯性  */
        s_vel *= 0.55f;   /*  1.60.55：松手不再"甩飞"  */
    }
}

/* ==========================================================================
 *  8. 页面容器（懒创建 ）
 * ========================================================================== */
static void build_home(lv_obj_t *root);
static void build_msg(lv_obj_t *root);
/*  切换菜单卡片风格（3 种循环  立即生效  你挑一个告诉我就固化 ）*/
static void on_style_switch(lv_event_t *e)
{
    lv_obj_t *lbl = (lv_obj_t *)lv_event_get_user_data(e);
    s_card_style = (s_card_style + 1) % 6;   /* 0~5 共 6 种，点一次换一种 */
    menu_apply_style();
    if (lbl) {
        lv_label_set_text_fmt(lbl, "风格%d", s_card_style + 1);
    }
    ESP_LOGI(TAG_UI, "菜单卡片风格  %d", s_card_style);
}

static void build_set(lv_obj_t *root);
static void set_refresh(void);          /*  前置声明  */
static void build_ota(lv_obj_t *root);
static void build_cal(lv_obj_t *root);
/* ==========================================================================
 *  日程「新增」面板（宽松排版 + 1/10 分钟 + 理由选择）
 *  ┌────────────────────────────────┐
 *  │ 新日程              理由：喝水   │
 *  │      08:30                     │
 *  │  [-10] [+10]  [-1]  [+1]       │
 *  │  [请假] [喝水] [运动] [吃药]     │
 *  │  [每天] [保存] [取消]            │
 *  └────────────────────────────────┘
 * ========================================================================== */
static lv_obj_t *nw_sh      = NULL;
static lv_obj_t *nw_sh_t    = NULL;
static lv_obj_t *nw_sh_rea  = NULL;
static lv_obj_t *nw_sh_rulelb = NULL;   /* 规则文字 */
static const char *NW_RULES[3] = { "工作日", "每天", "周末" };
static int       nw_sh_h    = 8;
static int       nw_sh_m    = 0;
static int       nw_sh_rule = 0;          /* 0 每天 1 工作日 2 指定日 */
static char      nw_sh_txt[16] = "提醒";

static void nw_sh_refresh(void)
{
    if (nw_sh_t)   lv_label_set_text_fmt(nw_sh_t, "%02d:%02d", nw_sh_h, nw_sh_m);
    if (nw_sh_rea) lv_label_set_text(nw_sh_rea, nw_sh_txt);
}

static void nw_sh_min(lv_event_t *e)
{
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    nw_sh_m += d;
    while (nw_sh_m < 0)  { nw_sh_m += 60; nw_sh_h = (nw_sh_h + 23) % 24; }
    while (nw_sh_m > 59) { nw_sh_m -= 60; nw_sh_h = (nw_sh_h + 1) % 24; }
    nw_sh_refresh();
}

static void nw_sh_reason(lv_event_t *e)
{
    const char *t = (const char *)lv_event_get_user_data(e);
    if (t) snprintf(nw_sh_txt, sizeof(nw_sh_txt), "%s", t);
    nw_sh_refresh();
}

static void nw_sh_rule_cb(lv_event_t *e)
{
    (void)e;
    nw_sh_rule = (nw_sh_rule + 1) % 3;            /* 工作日 → 每天 → 周末 */
    if (nw_sh_rulelb) lv_label_set_text(nw_sh_rulelb, NW_RULES[nw_sh_rule]);
}

static void nw_sh_close(void)
{
    if (nw_sh) { lv_obj_del(nw_sh); nw_sh = NULL; }
    nw_sh_t = nw_sh_rea = NULL;
}

static void nw_sh_cancel(lv_event_t *e) { (void)e; nw_sh_close(); }

static void nw_sh_save(lv_event_t *e)
{
    (void)e;
    Create_schedule_task_message(nw_sh_txt, 0, nw_sh_h, nw_sh_m, nw_sh_rule);
    ESP_LOGI(TAG_UI, "新增日程: %s %02d:%02d rule=%d", nw_sh_txt, nw_sh_h, nw_sh_m, nw_sh_rule);
    nw_sh_close();
    sched_rebuild();
}

static void nw_sh_btn(lv_obj_t *p, const char *txt, int x, int y, int w,
                      lv_event_cb_t cb, void *ud)
{
    lv_obj_t *b = lv_button_create(p);
    lv_obj_set_size(b, w, 26);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_bg_color(b, lv_color_black(), 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, lv_color_white(), 0);
    lv_obj_set_style_radius(b, 6, 0);
    lv_obj_center(make_label(b, txt, FONT_SMALL));
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
}

static void nw_sched_add(lv_event_t *e)
{
    (void)e;
    nw_sh_close();

    time_t now = time(NULL);
    struct tm tm_n;
    localtime_r(&now, &tm_n);
    nw_sh_h = (tm_n.tm_hour + 1) % 24;
    nw_sh_m = 0;
    nw_sh_rule = 0;      /* 默认工作日 */
    snprintf(nw_sh_txt, sizeof(nw_sh_txt), "提醒");

    nw_sh = lv_obj_create(lv_layer_top());
    style_black(nw_sh, false);
    lv_obj_set_size(nw_sh, 268, 172);
    lv_obj_center(nw_sh);
    lv_obj_set_style_border_width(nw_sh, 1, 0);
    lv_obj_set_style_border_color(nw_sh, lv_color_white(), 0);
    lv_obj_set_style_radius(nw_sh, 8, 0);

    lv_obj_t *tt = make_label(nw_sh, "新日程", FONT_SMALL);
    lv_obj_set_pos(tt, 10, 6);

    lv_obj_t *rl = make_label(nw_sh, "理由", FONT_SMALL);
    lv_obj_set_style_opa(rl, LV_OPA_50, 0);
    lv_obj_set_pos(rl, 176, 6);

    nw_sh_rea = make_label(nw_sh, nw_sh_txt, FONT_SMALL);
    lv_obj_set_pos(nw_sh_rea, 176, 24);

    nw_sh_t = make_label(nw_sh, "08:00", FONT_BIG);
    lv_obj_set_pos(nw_sh_t, 10, 22);

    /* 时间微调：10 分钟 + 1 分钟 */
    nw_sh_btn(nw_sh, "-10", 10,  66, 58, nw_sh_min, (void *)(intptr_t)-10);
    nw_sh_btn(nw_sh, "+10", 74,  66, 58, nw_sh_min, (void *)(intptr_t)10);
    nw_sh_btn(nw_sh, "-1",  138, 66, 58, nw_sh_min, (void *)(intptr_t)-1);
    nw_sh_btn(nw_sh, "+1",  202, 66, 58, nw_sh_min, (void *)(intptr_t)1);

    /* 理由 */
    nw_sh_btn(nw_sh, "请假", 10,  102, 58, nw_sh_reason, (void *)"请假");
    nw_sh_btn(nw_sh, "喝水", 74,  102, 58, nw_sh_reason, (void *)"喝水");
    nw_sh_btn(nw_sh, "运动", 138, 102, 58, nw_sh_reason, (void *)"运动");

    /* 规则 / 保存 / 取消 */
    nw_sh_btn(nw_sh, "规则", 10,  136, 58, nw_sh_rule_cb, NULL);
    nw_sh_rulelb = make_label(nw_sh, NW_RULES[nw_sh_rule], FONT_SMALL);
    lv_obj_set_pos(nw_sh_rulelb, 202, 142);
    nw_sh_btn(nw_sh, "保存", 74,  136, 58, nw_sh_save, NULL);
    nw_sh_btn(nw_sh, "取消", 138, 136, 58, nw_sh_cancel, NULL);

    nw_sh_refresh();
}

static void build_sched(lv_obj_t *root);
static void on_sched_del(lv_event_t *e);   /*  前置声明  */
static void on_sched_edit(lv_event_t *e);  /*  前置声明  */
static void sched_rebuild(void);          /*  前置声明  编辑面板要用  */
static void build_weather(lv_obj_t *root);
/* AI 示例问题（点一下直接问） */
static void nw_ask_cb(lv_event_t *e)
{
    const char *q = (const char *)lv_event_get_user_data(e);
    if (q == NULL) return;
    if (s_ai_ans)   lv_label_set_text(s_ai_ans, "...思考中...");
    if (s_ai_state) lv_label_set_text(s_ai_state, "请求中...");
    ai_chat_ask(q);
    s_ai_last = -1;
}

static void build_ai(lv_obj_t *root);
static void build_game(lv_obj_t *root);
static void build_wifi(lv_obj_t *root);
/* ==========================================================================
 *   新模块（云端 / 传感）+ 小键盘
 *  全部用 nw_ 前缀  避免和旧代码撞名 
 *  ⚠️ 放在这里（build_off 之前）：page_scaffold / make_label / style_black 都已定义 
 * ========================================================================== */
extern char         g_cloud_last_msg[];
extern volatile int g_cloud_msg_seq;

/* ---------------- 小键盘（210x92 + 右上「关」）---------------- */
static void nw_kb_hide_cb(lv_event_t *e)
{
    lv_obj_t *kb = (lv_obj_t *)lv_event_get_user_data(e);
    if (kb == NULL) return;
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *x = (lv_obj_t *)lv_obj_get_user_data(kb);
    if (x) lv_obj_add_flag(x, LV_OBJ_FLAG_HIDDEN);
}

static void nw_ta_click_cb(lv_event_t *e)
{
    lv_obj_t *kb = (lv_obj_t *)lv_event_get_user_data(e);
    lv_obj_t *ta = lv_event_get_target(e);
    if (kb == NULL) return;
    lv_keyboard_set_textarea(kb, ta);
    lv_obj_remove_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(kb);
    lv_obj_t *x = (lv_obj_t *)lv_obj_get_user_data(kb);
    if (x) { lv_obj_remove_flag(x, LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(x); }
}

static void nw_make_kb(lv_obj_t *parent, lv_obj_t *ta)
{
    lv_obj_t *kb = lv_keyboard_create(parent);
    lv_obj_set_size(kb, 210, 92);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_bg_color(kb, lv_color_black(), 0);
    lv_obj_set_style_text_color(kb, lv_color_white(), 0);
    lv_obj_set_style_border_width(kb, 1, 0);
    lv_obj_set_style_border_color(kb, lv_color_white(), 0);
    lv_keyboard_set_textarea(kb, ta);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *x = lv_button_create(parent);
    lv_obj_set_size(x, 30, 20);
    lv_obj_align_to(x, kb, LV_ALIGN_OUT_TOP_RIGHT, 0, -2);
    lv_obj_set_style_bg_color(x, lv_color_black(), 0);
    lv_obj_set_style_border_width(x, 1, 0);
    lv_obj_set_style_border_color(x, lv_color_white(), 0);
    lv_obj_set_style_radius(x, 5, 0);
    lv_obj_center(make_label(x, "\u5173", FONT_SMALL));
    lv_obj_add_flag(x, LV_OBJ_FLAG_HIDDEN);

    /* 拼音输入法（能打中文 ） */
    if (LV_USE_IME_PINYIN != 0) {
        lv_obj_t *ime = lv_ime_pinyin_create(parent);
        lv_ime_pinyin_set_keyboard(ime, kb);
        lv_obj_set_style_text_font(ime, FONT_SMALL, 0);   /* 候选字用我们的全字库 */
        lv_obj_set_user_data(kb, x);
    } else {
        lv_obj_set_user_data(kb, x);
    }
    lv_obj_add_event_cb(x, nw_kb_hide_cb, LV_EVENT_CLICKED, kb);
    lv_obj_add_event_cb(kb, nw_kb_hide_cb, LV_EVENT_READY,  kb);
    lv_obj_add_event_cb(kb, nw_kb_hide_cb, LV_EVENT_CANCEL, kb);
    lv_obj_add_event_cb(ta, nw_ta_click_cb, LV_EVENT_CLICKED, kb);
}

/* ---------------- 云端页 ---------------- */
#define NW_LINES 5
static char      nw_line[NW_LINES][76];
static int       nw_n = 0;
static lv_obj_t *nw_cl_state = NULL;
static lv_obj_t *nw_cl_ta    = NULL;
static lv_obj_t *nw_cl_box   = NULL;
static int       nw_seq      = 0;

static void nw_add(const char *t)
{
    if (nw_n < NW_LINES) {
        snprintf(nw_line[nw_n], sizeof(nw_line[0]), "%s", t);
        nw_n++;
    } else {
        for (int i = 1; i < NW_LINES; i++) memcpy(nw_line[i-1], nw_line[i], sizeof(nw_line[0]));
        snprintf(nw_line[NW_LINES-1], sizeof(nw_line[0]), "%s", t);
    }
}

static void nw_render(void)
{
    if (nw_cl_box == NULL) return;
    lv_obj_clean(nw_cl_box);
    for (int i = 0; i < nw_n; i++) {
        lv_obj_t *l = make_label(nw_cl_box, nw_line[i], FONT_SMALL);
        lv_obj_set_width(l, LEFT_W - 22);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        if (i < nw_n - 1) lv_obj_set_style_opa(l, LV_OPA_60, 0);
    }
}

static void nw_cloud_send(const char *txt)
{
    if (txt == NULL || txt[0] == 0) return;
    if (!get_onenet_state()) { nw_add("\u672a\u8fde\u63a5\uff0c\u53d1\u4e0d\u51fa\u53bb"); nw_render(); return; }

    char esc[160];
    int  k = 0;
    const char *p = txt;
    while (*p != 0 && k < (int)sizeof(esc) - 3) {
        if (*p == 0x22 || *p == 0x5C) esc[k++] = 0x5C;
        esc[k++] = *p++;
    }
    esc[k] = 0;

    char json[256];
    snprintf(json, sizeof(json),
             "{\"id\":\"1\",\"version\":\"1.0\",\"params\":{\"msg\":{\"value\":\"%s\"}}}", esc);
    onenet_post_property_data(json);

    char line[76];
    snprintf(line, sizeof(line), "\u53d1 %s", txt);
    nw_add(line);
    nw_render();
}

static void nw_send_cb(lv_event_t *e)
{
    (void)e;
    const char *t = nw_cl_ta ? lv_textarea_get_text(nw_cl_ta) : "";
    nw_cloud_send(t);
    if (nw_cl_ta) lv_textarea_set_text(nw_cl_ta, "");
}

static void nw_chip_cb(lv_event_t *e)
{
    const char *t = (const char *)lv_event_get_user_data(e);
    nw_cloud_send(t);
}

static void nw_cloud_tick(void)
{
    if (nw_cl_state) {
        static bool last = false;
        bool now = get_onenet_state();
        if (now != last) { last = now; lv_label_set_text(nw_cl_state, now ? "\u5df2\u8fde\u63a5" : "\u672a\u8fde\u63a5"); }
    }
    if (g_cloud_msg_seq != nw_seq) {
        nw_seq = g_cloud_msg_seq;
        char line[76];
        snprintf(line, sizeof(line), "\u6536 %s", g_cloud_last_msg);
        nw_add(line);
        if (nw_cl_box) nw_render();
    }
}

static void build_nw_cloud(lv_obj_t *root)
{
    lv_obj_t *body = page_scaffold(root, "\u4e91\u7aef", NULL);

    nw_cl_state = make_label(root, get_onenet_state() ? "\u5df2\u8fde\u63a5" : "\u672a\u8fde\u63a5", FONT_SMALL);
    lv_obj_align(nw_cl_state, LV_ALIGN_TOP_RIGHT, -6, 0);

    /* 4 个中文快捷按钮 */
    static const char *CH[4] = { "\u4f60\u597d", "\u6536\u5230", "\u5f00\u706f", "\u5173\u706f" };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *b = lv_button_create(body);
        lv_obj_set_size(b, 44, 22);
        lv_obj_set_pos(b, 6 + i * 47, 4);
        lv_obj_set_style_bg_color(b, lv_color_black(), 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_border_color(b, lv_color_white(), 0);
        lv_obj_set_style_border_opa(b, LV_OPA_40, 0);
        lv_obj_set_style_radius(b, 11, 0);
        lv_obj_center(make_label(b, CH[i], FONT_SMALL));
        lv_obj_add_event_cb(b, nw_chip_cb, LV_EVENT_CLICKED, (void *)CH[i]);
    }

    /* 输入框 + 发送 */
    nw_cl_ta = lv_textarea_create(body);
    lv_obj_set_size(nw_cl_ta, LEFT_W - 78, 30);
    lv_obj_set_pos(nw_cl_ta, 6, 32);
    lv_textarea_set_one_line(nw_cl_ta, true);
    lv_textarea_set_max_length(nw_cl_ta, 40);
    lv_obj_set_style_bg_color(nw_cl_ta, lv_color_black(), 0);
    lv_obj_set_style_text_color(nw_cl_ta, lv_color_white(), 0);
    lv_obj_set_style_border_width(nw_cl_ta, 1, 0);
    lv_obj_set_style_border_color(nw_cl_ta, lv_color_white(), 0);
    lv_obj_set_style_radius(nw_cl_ta, 6, 0);

    lv_obj_t *b = lv_button_create(body);
    lv_obj_set_size(b, 60, 30);
    lv_obj_set_pos(b, LEFT_W - 66, 32);
    lv_obj_set_style_bg_color(b, lv_color_black(), 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, lv_color_white(), 0);
    lv_obj_set_style_radius(b, 6, 0);
    lv_obj_center(make_label(b, "\u53d1\u9001", FONT_SMALL));
    lv_obj_add_event_cb(b, nw_send_cb, LV_EVENT_CLICKED, NULL);

    nw_make_kb(body, nw_cl_ta);

    /* 收发列表 */
    nw_cl_box = lv_obj_create(body);
    style_black(nw_cl_box, true);
    lv_obj_set_size(nw_cl_box, LEFT_W - 12, SCR_H - 76);
    lv_obj_set_pos(nw_cl_box, 6, 70);
    lv_obj_set_flex_flow(nw_cl_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(nw_cl_box, 8, 0);
    lv_obj_add_flag(nw_cl_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(nw_cl_box, LV_DIR_VER);

    nw_seq = g_cloud_msg_seq;
    nw_render();
}

/* ---------------- 传感页 ---------------- */
static lv_obj_t *nw_tbig = NULL, *nw_bar_t = NULL, *nw_bar_l = NULL;
static lv_obj_t *nw_tv = NULL, *nw_lv = NULL;

static void nw_sensor_tick(void)
{
    float t = adc_temp_c();
    int   l = adc_light_pct();
    if (nw_tbig)  lv_label_set_text_fmt(nw_tbig, "%.1f\u2103", (double)t);
    if (nw_tv)    lv_label_set_text_fmt(nw_tv, "%.1f\u2103", (double)t);
    if (nw_lv)    lv_label_set_text_fmt(nw_lv, "%d%%", l);
    if (nw_bar_l) lv_bar_set_value(nw_bar_l, l, LV_ANIM_OFF);
    if (nw_bar_t) {
        int v = (int)((t + 10.0f) * 2.0f);
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        lv_bar_set_value(nw_bar_t, v, LV_ANIM_OFF);
    }
}

static void build_nw_sensor(lv_obj_t *root)
{
    lv_obj_t *body = page_scaffold(root, "\u4f20\u611f", NULL);

    nw_tbig = make_label(body, "--.-", FONT_BIG);
    lv_obj_align(nw_tbig, LV_ALIGN_TOP_MID, 0, 6);

    lv_obj_t *t1 = make_label(body, "\u6e29\u5ea6", FONT_SMALL);
    lv_obj_set_pos(t1, 6, 62);
    nw_bar_t = lv_bar_create(body);
    lv_obj_set_size(nw_bar_t, LEFT_W - 80, 12);
    lv_obj_set_pos(nw_bar_t, 44, 64);
    lv_obj_set_style_bg_color(nw_bar_t, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(nw_bar_t, LV_OPA_20, 0);
    lv_obj_set_style_bg_color(nw_bar_t, lv_color_white(), LV_PART_INDICATOR);
    lv_bar_set_range(nw_bar_t, 0, 100);
    nw_tv = make_label(body, "--.-", FONT_SMALL);
    lv_obj_set_pos(nw_tv, 6, 82);

    lv_obj_t *t2 = make_label(body, "\u5149\u7167", FONT_SMALL);
    lv_obj_set_pos(t2, 6, 110);
    nw_bar_l = lv_bar_create(body);
    lv_obj_set_size(nw_bar_l, LEFT_W - 80, 12);
    lv_obj_set_pos(nw_bar_l, 44, 112);
    lv_obj_set_style_bg_color(nw_bar_l, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(nw_bar_l, LV_OPA_20, 0);
    lv_obj_set_style_bg_color(nw_bar_l, lv_color_white(), LV_PART_INDICATOR);
    lv_bar_set_range(nw_bar_l, 0, 100);
    nw_lv = make_label(body, "--", FONT_SMALL);
    lv_obj_set_pos(nw_lv, 6, 130);

    lv_obj_t *hint = make_label(body, "\u6bcf\u79d2\u5237\u65b0", FONT_SMALL);
    lv_obj_set_style_opa(hint, LV_OPA_40, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -6);

    nw_sensor_tick();
}

static void build_off(lv_obj_t *root);

/* 统一给每页加一个标题栏 + 内容容器  返回内容容器给业务挂控件  */
static lv_obj_t *page_scaffold(lv_obj_t *root, const char *title, const char *hint)
{
    lv_obj_t *bar = lv_obj_create(root);
    style_black(bar, true);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_size(bar, LEFT_W, 30);

    lv_obj_t *t = make_label(bar, title, FONT_SMALL);
    lv_obj_set_pos(t, 8, 4);

    if (hint) {
        lv_obj_t *h = make_label(bar, hint, FONT_SMALL);
        lv_obj_set_style_text_color(h, lv_color_white(), 0);
        lv_obj_set_style_opa(h, LV_OPA_50, 0);
        lv_obj_align(h, LV_ALIGN_RIGHT_MID, -6, 0);
    }

    lv_obj_t *body = lv_obj_create(root);
    style_black(body, true);
    lv_obj_set_pos(body, 0, 32);
    lv_obj_set_size(body, LEFT_W, SCR_H - 34);
    return body;
}

static void build_nw_cloud(lv_obj_t *root);
static void build_nw_sensor(lv_obj_t *root);

lv_obj_t *ui_page_root(ui_page_t page)
{
    if (page >= UI_PAGE_COUNT) return NULL;
    if (s_pages[page] != NULL) return s_pages[page];

    lv_obj_t *root = lv_obj_create(s_content);
    style_black(root, true);                     /* 透明  黑底由 s_content 提供  */
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_size(root, LEFT_W, SCR_H);
    lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);   /* 先藏  切过去时再显示  */

    s_pages[page] = root;

    switch (page) {
    case UI_PAGE_HOME:    build_home(root);    break;
    case UI_PAGE_MSG:     build_msg(root);     break;
    case UI_PAGE_SET:     build_set(root);     break;
    case UI_PAGE_OTA:     build_ota(root);     break;
    case UI_PAGE_CAL:     build_cal(root);     break;
    case UI_PAGE_SCHED:   build_sched(root);   break;
    case UI_PAGE_WEATHER: build_weather(root); break;
    case UI_PAGE_AI:      build_ai(root);      break;
    case UI_PAGE_GAME:    build_game(root);    break;
    case UI_PAGE_WIFI:    build_wifi(root);    break;
    case UI_PAGE_CLOUD:   build_nw_cloud(root);  break;
    case UI_PAGE_SENSOR:  build_nw_sensor(root); break;
    case UI_PAGE_OFF:     build_off(root);     break;
    default: break;
    }
    return root;
}

/* ==========================================================================
 *  9. 各页面 UI（只搭架子  业务请用 ui_page_root() 拿到容器后自己加 ）
 * ========================================================================== */

/* ---------- 首页（上电默认  按规划图：年月日 + 大时钟）---------- */
static lv_obj_t *s_home_date = NULL;
static lv_obj_t *s_home_time = NULL;
static lv_obj_t *s_home_info = NULL;

/* 首页用的浮动动画回调（lv_obj_set_y 正好两个参数  可以直接当动画回调 ）*/
static lv_obj_t *s_home_pet = NULL;

/* 卡通图"动起来"的两个动画回调（LVGL 9：旋转单位是 0.1 度  缩放 256=100% ）*/
/*  translate_x 有 3 个参数  不能直接当动画回调  包一层  */
static void anim_set_tx(void *var, int32_t v)
{
    lv_obj_set_style_translate_x((lv_obj_t *)var, v, 0);
}

static void anim_set_ty(void *var, int32_t v)
{
    lv_obj_set_style_translate_y((lv_obj_t *)var, v, 0);
}

static void anim_set_rot(void *var, int32_t v)   { lv_obj_set_style_transform_rotation((lv_obj_t *)var, v, 0); }
static void anim_set_scale(void *var, int32_t v)
{
    lv_obj_set_style_transform_scale_x((lv_obj_t *)var, v, 0);
    lv_obj_set_style_transform_scale_y((lv_obj_t *)var, v, 0);
}

static void build_home(lv_obj_t *root)
{
    /*  布局按你要求重排（左区 204 宽全部【水平居中】 上下分散 ）
          ┌──────────────┐
          │  2026年10月27日 │  顶部居中 y=6
          │   23:47:12    │  大时钟居中 y=34
          │     [小人]     │  中部居中 y=96（会摆头 ）
          │ 温度xx.x℃ 光照x% │  【最下面】y=216
          └──────────────┘
       ⚠️ 不用 make_label 的居中 = lv_obj_align(obj, ALIGN_TOP_MID, 0, y)  */

    s_home_date = make_label(root, "----年--月--日", FONT_SMALL);
    lv_obj_align(s_home_date, LV_ALIGN_TOP_MID, 0, 4);

    s_home_time = make_label(root, "--:--:--", FONT_BIG);
    lv_obj_align(s_home_time, LV_ALIGN_TOP_MID, 0, 30);

    /* 卡通图：水平居中垂直放在中间偏下  */
    s_home_pet = lv_image_create(root);
    lv_image_set_src(s_home_pet, &home_pet);
    lv_obj_align(s_home_pet, LV_ALIGN_TOP_MID, 0, 100);

    /*  表情动起来：改成【左右摆头 + 呼吸缩放】（比上下移动更像"活着"）
       以图片中心为轴  否则会绕着左上角转  */
    lv_obj_set_style_transform_pivot_x(s_home_pet, lv_pct(50), 0);
    lv_obj_set_style_transform_pivot_y(s_home_pet, lv_pct(50), 0);

    /*  动作换成【小圆周晃动】（x/y 两个整数平移叠起来 = 绕小圈 ）
       为什么这么选：
          纯整数像素平移  不缩放  不旋转   几乎零开销 
           （缩放/旋转每帧要重采样  之前把 CPU 吃满过两次 ）
          视觉上是"活蹦乱跳" 比单纯左右摆更像活的  */
    lv_anim_t a;
    lv_anim_init(&a);

    lv_anim_t c;                             /* 纵向分量：周期不同  合成椭圆  */
    lv_anim_init(&c);

    /*  删掉"呼吸缩放"动画 
       原因：缩放是【非整数倍】（256286 = 1.117 ）
             LVGL 每帧要做图像重采样  开销极大 
             CPU1 被吃满  IDLE 饿死  task_wdt 报警 （实测已触发 ）
       只保留【左右平移】 整数像素  开销极小  一样是"活着"的感觉  */


    /* 温度/光照：【最下面】居中  */
    s_home_info = make_label(root, "温度 --.-℃   光照 --%", FONT_SMALL);
    lv_obj_align(s_home_info, LV_ALIGN_BOTTOM_MID, 0, -14);
}

/* 业务可以直接改这三行文字  */
void ui_home_set_clock(const char *date, const char *time_)
{
    if (s_home_date && date) lv_label_set_text(s_home_date, date);
    if (s_home_time && time_) lv_label_set_text(s_home_time, time_);
}
void ui_home_set_info(const char *txt)
{
    if (s_home_info && txt) lv_label_set_text(s_home_info, txt);
}

/* ---------- 消息 ---------- */
/* ===== 消息页：真的列表（数据来自 task_message 的环形队列 ）===== */
static lv_obj_t *s_msg_list  = NULL;
static lv_obj_t *s_msg_badge = NULL;
static int        s_msg_shown = -1;      /* 上次画了几条  变了才重建  */

static void msg_list_rebuild(void)
{
    if (s_msg_list == NULL) return;
    lv_obj_clean(s_msg_list);

    int n = msg_count();
    int unread = msg_unread();

    if (s_msg_badge) {
        if (unread > 0) {
            lv_label_set_text_fmt(s_msg_badge, "未读 %d", unread);
            lv_obj_remove_flag(s_msg_badge, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_msg_badge, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (n == 0) {
        lv_obj_t *e = make_label(s_msg_list, "暂无消息", FONT_SMALL);
        lv_obj_set_style_opa(e, LV_OPA_40, 0);
        lv_obj_align(e, LV_ALIGN_TOP_MID, 0, 40);
        return;
    }
    if (n > 30) n = 30;                  /* 只显示最新 30 条  够用又不卡  */

    for (int i = msg_count() - 1; i >= msg_count() - n && i >= 0; i--) {
        task_message_t m;
        msg_get(i, &m);

        lv_obj_t *row = lv_obj_create(s_msg_list);
        style_black(row, true);
        lv_obj_set_size(row, LEFT_W - 20, 58);   /* 两行正文 + 一行信息 */
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, lv_color_white(), 0);
        lv_obj_set_style_border_opa(row, LV_OPA_20, 0);

        /* 正文：占前两行（2 ~ 38px）*/
        lv_obj_t *tx = make_label(row, m.text, FONT_SMALL);
        lv_obj_set_pos(tx, 4, 2);
        lv_obj_set_size(tx, LEFT_W - 24, 36);              /* 固定两行高 */
        lv_label_set_long_mode(tx, LV_LABEL_LONG_DOT);     /* 超长省略 不撑高 */
        lv_obj_set_style_text_align(tx, LV_TEXT_ALIGN_LEFT, 0);

        lv_obj_t *meta = make_label(row, "", FONT_SMALL);
        lv_label_set_text_fmt(meta, "%s  %s  %s", msg_src_name(m.src), msg_ts_to_time(m.ts), m.state_read ? "已读" : "未读");
        lv_obj_set_style_opa(meta, LV_OPA_50, 0);
        lv_obj_set_pos(meta, 4, 40);                       /* 第三行 不压正文 */
        lv_obj_set_size(meta, LEFT_W - 24, 18);
        lv_label_set_long_mode(meta, LV_LABEL_LONG_DOT);
    }
}

static void on_msg_all_read(lv_event_t *e)
{
    (void)e;
    msg_mark_all_read();       /*  业务 API  我不知道内部实现  直接调  */
    msg_list_rebuild();
}

static void build_msg(lv_obj_t *root)
{
    /* 顶部：标题 + 未读数 + 全部已读  */
    s_msg_badge = make_label(root, "", FONT_SMALL);
    lv_obj_align(s_msg_badge, LV_ALIGN_TOP_RIGHT, -6, 4);
    lv_obj_add_flag(s_msg_badge, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *btn = lv_button_create(root);
    lv_obj_set_size(btn, 76, 22);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 4, 2);
    lv_obj_set_style_bg_color(btn, lv_color_black(), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_white(), 0);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_t *bl = make_label(btn, "全部已读", FONT_SMALL);
    lv_obj_center(bl);
    lv_obj_add_event_cb(btn, on_msg_all_read, LV_EVENT_CLICKED, NULL);

    /* 列表容器（可上下滑 ）*/
    s_msg_list = lv_obj_create(root);
    style_black(s_msg_list, true);
    lv_obj_set_pos(s_msg_list, 0, 28);
    lv_obj_set_size(s_msg_list, LEFT_W, SCR_H - 30);
    lv_obj_set_flex_flow(s_msg_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(s_msg_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_msg_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_msg_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_row(s_msg_list, 6, 0);   /* 行间距 */

    s_msg_shown = -1;
    msg_list_rebuild();
}

/* ---------- 设置 ---------- */
/* ===== 设置页：设备信息（都是真实数据 ）===== */
static lv_obj_t *s_set_ver   = NULL;
static lv_obj_t *s_set_wifi  = NULL;
static lv_obj_t *s_set_msg   = NULL;
static lv_obj_t *s_set_sched = NULL;
static lv_obj_t *s_set_heap  = NULL;

static void set_refresh(void)
{
    if (s_set_ver)   lv_label_set_text(s_set_ver, get_app_verion());
    if (s_set_wifi)  lv_label_set_text(s_set_wifi, boot_wifi_connected() ? "已连接 " : "未连接");
    if (s_set_msg)   lv_label_set_text_fmt(s_set_msg, "共%d条%d条已读", msg_count(), msg_count() - msg_unread());
    if (s_set_sched) lv_label_set_text_fmt(s_set_sched, "%d 条", get_schedule_task_message_sum());
    if (s_set_heap)  lv_label_set_text_fmt(s_set_heap, "%u KB", (unsigned)(esp_get_free_heap_size() / 1024));
}

/* 一行 "标题 ...... 值"  */
static lv_obj_t *set_row(lv_obj_t *parent, const char *k, int y)
{
    lv_obj_t *kl = make_label(parent, k, FONT_SMALL);
    lv_obj_set_pos(kl, 6, y);

    lv_obj_t *vl = make_label(parent, "--", FONT_SMALL);
    lv_obj_set_style_opa(vl, LV_OPA_60, 0);
    lv_obj_align(vl, LV_ALIGN_TOP_RIGHT, -6, y);
    return vl;
}

static void build_set(lv_obj_t *root)
{
    lv_obj_t *body = page_scaffold(root, "设置", NULL);
    s_set_ver   = set_row(body, "固件版本",  4);
    s_set_wifi  = set_row(body, "WiFi",     26);
    s_set_msg   = set_row(body, "消息",     48);
    s_set_sched = set_row(body, "日程",     70);
    s_set_heap  = set_row(body, "剩余内存", 92);
    set_refresh();

    /*  菜单卡片风格切换（点一下换一种  你试用后告诉我编号 ）*/
    {
        lv_obj_t *kl = make_label(body, "菜单风格", FONT_SMALL);
        lv_obj_set_pos(kl, 6, 118);

        lv_obj_t *btn = lv_button_create(body);
        lv_obj_set_size(btn, 66, 26);
        lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, -6, 114);
        lv_obj_set_style_bg_color(btn, lv_color_black(), 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_border_color(btn, lv_color_white(), 0);
        lv_obj_set_style_radius(btn, 6, 0);

        lv_obj_t *bl = make_label(btn, "风格1", FONT_SMALL);
        lv_obj_center(bl);
        lv_obj_add_event_cb(btn, on_style_switch, LV_EVENT_CLICKED, bl);
    }
}

/* ---------- 版本更新（复用已有 OTA 逻辑  这里只放容器）---------- */
static lv_obj_t *s_ota_cur = NULL;
static lv_obj_t *s_ota_new = NULL;
static lv_obj_t *s_ota_status = NULL;

static void on_ota_check(lv_event_t *e)
{
    (void)e;
    ui_ota_set_versions(get_app_verion(), "--", "正在检测新版本");
    ota_app_check();                 /*  复用已验证的 OTA 模块  */
}
static void on_ota_update(lv_event_t *e)
{
    (void)e;
    ui_ota_set_versions(get_app_verion(), "--", "正在下载更新（勿断电）");
    ota_app_begin_update();
}

static void build_ota(lv_obj_t *root)
{
    lv_obj_t *body = page_scaffold(root, "版本更新", NULL);

    lv_obj_t *c = make_label(body, "当前版本", FONT_SMALL);
    lv_obj_set_pos(c, 8, 8);
    s_ota_cur = make_label(body, "--", FONT_SMALL);
    lv_obj_set_pos(s_ota_cur, 96, 8);

    lv_obj_t *n = make_label(body, "最新版本", FONT_SMALL);
    lv_obj_set_pos(n, 8, 34);
    s_ota_new = make_label(body, "--", FONT_SMALL);
    lv_obj_set_pos(s_ota_new, 96, 34);

    s_ota_status = make_label(body, "点下面按钮开始检查 ", FONT_SMALL);
    lv_obj_set_pos(s_ota_status, 8, 62);

    /*  两个按钮（黑白 ）*/
    lv_obj_t *b1 = lv_button_create(body);
    lv_obj_set_size(b1, 84, 28);
    lv_obj_set_pos(b1, 8, 100);
    lv_obj_set_style_bg_color(b1, lv_color_black(), 0);
    lv_obj_set_style_border_width(b1, 1, 0);
    lv_obj_set_style_border_color(b1, lv_color_white(), 0);
    lv_obj_set_style_radius(b1, 6, 0);
    lv_obj_center(make_label(b1, "检查更新", FONT_SMALL));
    lv_obj_add_event_cb(b1, on_ota_check, LV_EVENT_CLICKED, NULL);

    lv_obj_t *b2 = lv_button_create(body);
    lv_obj_set_size(b2, 84, 28);
    lv_obj_set_pos(b2, 100, 100);
    lv_obj_set_style_bg_color(b2, lv_color_black(), 0);
    lv_obj_set_style_border_width(b2, 1, 0);
    lv_obj_set_style_border_color(b2, lv_color_white(), 0);
    lv_obj_set_style_radius(b2, 6, 0);
    lv_obj_center(make_label(b2, "开始更新", FONT_SMALL));
    lv_obj_add_event_cb(b2, on_ota_update, LV_EVENT_CLICKED, NULL);
}

void ui_ota_set_versions(const char *cur, const char *newv, const char *status)
{
    if (s_ota_cur && cur)    lv_label_set_text(s_ota_cur, cur);
    if (s_ota_new && newv)   lv_label_set_text(s_ota_new, newv);
    if (s_ota_status && status) lv_label_set_text(s_ota_status, status);
}

/* ---------- 日历 ---------- */
static void build_cal(lv_obj_t *root)
{
    lv_obj_t *body = page_scaffold(root, "日历", "calendar");
    lv_obj_t *c = lv_calendar_create(body);
    /* 标出今天 + 显示当月 */
    {
        time_t now = time(NULL);
        struct tm tm_n;
        localtime_r(&now, &tm_n);
        lv_calendar_set_today_date(c, tm_n.tm_year + 1900, tm_n.tm_mon + 1, tm_n.tm_mday);
        lv_calendar_set_showed_date(c, tm_n.tm_year + 1900, tm_n.tm_mon + 1);
    }
    lv_obj_set_size(c, LEFT_W - 16, SCR_H - 34 - 8);
    lv_obj_set_pos(c, 8, 4);
    lv_obj_set_style_bg_color(c, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_text_color(c, lv_color_white(), 0);
}

/* ---------- 日程 ---------- */
/* ===== 日程页：真列表（数据来自 task_schedule ）===== */
static lv_obj_t *s_sched_list = NULL;

/* ===== 日程编辑面板（点日程行弹出  改时间/规则/保存 ）===== */
static lv_obj_t *s_edit = NULL;
static int       s_edit_idx = 0;
static int       s_edit_h = 0, s_edit_m = 0, s_edit_rule = 0;
static lv_obj_t *s_edit_timelb = NULL;
static lv_obj_t *s_edit_rulelb = NULL;

static void edit_refresh(void)
{
    if (s_edit_timelb) lv_label_set_text_fmt(s_edit_timelb, "%02d:%02d", s_edit_h, s_edit_m);
    if (s_edit_rulelb) {
        const char *r = (s_edit_rule == 0) ? "每天" : (s_edit_rule == 1) ? "工作日" : "指定日";
        lv_label_set_text(s_edit_rulelb, r);
    }
}

static void edit_min_cb(lv_event_t *e)
{
    int d = (int)(intptr_t)lv_event_get_user_data(e);
    s_edit_m += d;
    while (s_edit_m >= 60) { s_edit_m -= 60; s_edit_h = (s_edit_h + 1) % 24; }
    while (s_edit_m < 0)   { s_edit_m += 60; s_edit_h = (s_edit_h + 23) % 24; }
    edit_refresh();
}

static void edit_rule_cb(lv_event_t *e) { (void)e; s_edit_rule = (s_edit_rule + 1) % 3; edit_refresh(); }

static void edit_close(void)
{
    if (s_edit) { lv_obj_del(s_edit); s_edit = NULL; }
    s_edit_timelb = NULL; s_edit_rulelb = NULL;
}

static void edit_cancel_cb(lv_event_t *e) { (void)e; edit_close(); }

static void edit_save_cb(lv_event_t *e)
{
    (void)e;

    if (s_edit_idx < 0) {
        /*  新建 （规则用当前选中的 ）*/
        Create_schedule_task_message("提醒", 0, s_edit_h, s_edit_m, s_edit_rule);
    } else {
        /*  修改  */
        Update_schedule_task_message(s_edit_idx, get_schedule_task_message_text(s_edit_idx),
                                     get_schedule_task_message_wday(s_edit_idx),
                                     s_edit_h, s_edit_m, s_edit_rule);
    }
    edit_close();
    sched_rebuild();
}

static lv_obj_t *edit_btn(lv_obj_t *parent, const char *txt, int x, int y, int w,
                          lv_event_cb_t cb, void *ud)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, w, 28);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_bg_color(b, lv_color_black(), 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, lv_color_white(), 0);
    lv_obj_set_style_radius(b, 6, 0);
    lv_obj_center(make_label(b, txt, FONT_SMALL));
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    return b;
}

static void sched_edit_open(int idx);   /* 前置声明  */

static void on_sched_new(lv_event_t *e)
{
    (void)e;
    sched_edit_open(-1);        /* -1 = 新建模式  */
}

static void on_sched_edit(lv_event_t *e)
{
    sched_edit_open((int)(intptr_t)lv_event_get_user_data(e));
}

static void sched_edit_open(int idx)
{
    s_edit_idx = idx;

    if (idx < 0) {
        /*  新建：默认"下一个整点" 规则每天  */
        time_t now = time(NULL);
        struct tm tm_n;
        localtime_r(&now, &tm_n);
        s_edit_h    = tm_n.tm_hour;
        s_edit_m    = 0;
        s_edit_rule = 0;
    } else {
        s_edit_h    = get_schedule_task_message_hour(idx);
        s_edit_m    = get_schedule_task_message_minute(idx);
        s_edit_rule = get_schedule_task_message_rule(idx);
    }

    edit_close();

    s_edit = lv_obj_create(lv_layer_top());
    style_black(s_edit, false);
    lv_obj_set_size(s_edit, SCR_W * 2 / 3, 138);
    lv_obj_center(s_edit);
    lv_obj_set_style_border_width(s_edit, 1, 0);
    lv_obj_set_style_border_color(s_edit, lv_color_white(), 0);

    lv_obj_t *tt = make_label(s_edit, (s_edit_idx < 0) ? "新日程" : "改日程", FONT_SMALL);
    lv_obj_set_pos(tt, 8, 4);

    s_edit_timelb = make_label(s_edit, "00:00", FONT_BIG);
    lv_obj_set_pos(s_edit_timelb, 6, 22);

    edit_btn(s_edit, "+30", 8,  80, 46, edit_min_cb,  (void *)(intptr_t)30);
    edit_btn(s_edit, "-30", 58, 80, 46, edit_min_cb,  (void *)(intptr_t)-30);

    edit_btn(s_edit, "规则", 112, 80, 46, edit_rule_cb, NULL);
    s_edit_rulelb = make_label(s_edit, "每天", FONT_SMALL);
    lv_obj_set_pos(s_edit_rulelb, 112, 112);

    edit_btn(s_edit, "保存", 8,  104, 46, edit_save_cb,   NULL);
    edit_btn(s_edit, "取消", 58, 104, 46, edit_cancel_cb, NULL);

    edit_refresh();
}
static void sched_rebuild(void)
{
    if (s_sched_list == NULL) return;
    lv_obj_clean(s_sched_list);

    int n = get_schedule_task_message_sum();
    if (n <= 0) {
        lv_obj_t *e = make_label(s_sched_list, "暂无日程", FONT_SMALL);
        lv_obj_set_style_opa(e, LV_OPA_40, 0);
        lv_obj_align(e, LV_ALIGN_TOP_MID, 0, 30);
        return;
    }

    for (int i = 0; i < n; i++) {
        lv_obj_t *row = lv_obj_create(s_sched_list);
        style_black(row, true);
        lv_obj_set_size(row, LEFT_W - 12, 46);   /*  3246：不挤了  */
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(row, lv_color_white(), 0);
        lv_obj_set_style_border_opa(row, LV_OPA_20, 0);

        lv_obj_t *tx = make_label(row, get_schedule_task_message_text(i), FONT_SMALL);
        lv_obj_set_pos(tx, 6, 4);                 /*  留出内边距  */
        lv_obj_set_width(tx, LEFT_W - 52);
        lv_label_set_long_mode(tx, LV_LABEL_LONG_DOT);

        lv_obj_t *mt = make_label(row, "", FONT_SMALL);
        lv_label_set_text_fmt(mt, "%02d:%02d  %s", get_schedule_task_message_hour(i),
                              get_schedule_task_message_minute(i),
                              get_schedule_task_message_state(i) ? "开" : "关");
        lv_obj_set_style_opa(mt, LV_OPA_50, 0);
        lv_obj_set_pos(mt, 6, 26);                /*  第二行下移  两行不叠  */

        /* 删除按钮（右上 ）*/
        lv_obj_t *del = lv_button_create(row);
        lv_obj_set_size(del, 30, 30);   /*  按钮加大好点  */
        lv_obj_align(del, LV_ALIGN_RIGHT_MID, -2, 0);
        lv_obj_set_style_bg_color(del, lv_color_black(), 0);
        lv_obj_set_style_border_width(del, 1, 0);
        lv_obj_set_style_border_color(del, lv_color_white(), 0);
        lv_obj_set_style_radius(del, 5, 0);
        lv_obj_center(make_label(del, "删", FONT_SMALL));
        lv_obj_add_event_cb(del, on_sched_del, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        /*  整行可点  打开编辑面板 （改时间 / 改规则 ）*/
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, on_sched_edit, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

static void on_sched_del(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    Delete_schedule_task_message(idx);     /*  业务 API  */
    sched_rebuild();
}

static void on_sched_add(lv_event_t *e)
{
    (void)e;
    /* 新增一条：1 分钟后每天重复 （拿它当"测试按钮"用 ）*/
    time_t now = time(NULL);
    struct tm tm_n;
    localtime_r(&now, &tm_n);
    int h = tm_n.tm_hour, m = tm_n.tm_min + 1;
    if (m >= 60) { m -= 60; h = (h + 1) % 24; }

    create_schedule_task_message("提醒", tm_n.tm_wday, h, m, 0);
    sched_rebuild();
}

static void build_sched(lv_obj_t *root)
{
    /*  右上「新增」按钮 （弹出编辑面板确认后才真正创建 ）
       点已有一条  修改 ；点右上「新增」 新建  */
    lv_obj_t *addb = lv_button_create(root);
    lv_obj_set_size(addb, 52, 24);
    lv_obj_align(addb, LV_ALIGN_TOP_RIGHT, -4, 0);
    lv_obj_set_style_bg_color(addb, lv_color_black(), 0);
    lv_obj_set_style_border_width(addb, 1, 0);
    lv_obj_set_style_border_color(addb, lv_color_white(), 0);
    lv_obj_set_style_radius(addb, 6, 0);
    lv_obj_center(make_label(addb, "新增", FONT_SMALL));
    lv_obj_add_event_cb(addb, nw_sched_add, LV_EVENT_CLICKED, NULL);   /* 新面板 */


    s_sched_list = lv_obj_create(root);
    style_black(s_sched_list, true);
    lv_obj_set_pos(s_sched_list, 0, 28);
    lv_obj_set_size(s_sched_list, LEFT_W, SCR_H - 28);
    lv_obj_set_flex_flow(s_sched_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(s_sched_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_sched_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_sched_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_row(s_sched_list, 0, 0);

    sched_rebuild();
}

/* ---------- 天气（只搭框架  业务复用已有代码 ）---------- */
/* ===== 天气页：真数据（weather 模块从 template001 移植 ）=====
   网络请求不能放 LVGL 任务里   起普通任务  结果写字符串  tick 刷控件  */
static lv_obj_t *s_w_city_lb = NULL;
static lv_obj_t *s_w_now_lb  = NULL;
static lv_obj_t *s_w_date[3] = {0};
static lv_obj_t *s_w_desc[3] = {0};
static lv_obj_t *s_w_temp[3] = {0};

static char s_w_city_buf[64] = "--";
static char s_w_now_buf[96]  = "正在获取天气";
static weather_forecast_t s_w_fc;
static volatile bool s_w_ready = false;
static volatile bool s_w_ok    = false;

static void weather_fetch_task(void *arg)
{
    (void)arg;
    char city[WEATHER_CITY_MAX_LEN] = {0};

    if (weather_get_ip_city(city, sizeof(city)) == ESP_OK) {
        snprintf(s_w_city_buf, sizeof(s_w_city_buf), "%s", city);
        weather_data_t d;
        if (weather_get_by_city(city, &d) == ESP_OK) {
            snprintf(s_w_now_buf, sizeof(s_w_now_buf), "%s℃  %s", d.temperature, d.text);
            s_w_ok = true;
        }
        if (weather_get_forecast_by_city(city, &s_w_fc) == ESP_OK) s_w_ok = true;
    }
    if (!s_w_ok) snprintf(s_w_now_buf, sizeof(s_w_now_buf), "获取失败（检查网络）");
    s_w_ready = true;
    vTaskDelete(NULL);
}

static void weather_refresh_ui(void)
{
    if (s_w_city_lb) lv_label_set_text(s_w_city_lb, s_w_city_buf);
    if (s_w_now_lb)  lv_label_set_text(s_w_now_lb, s_w_now_buf);

    for (int i = 0; i < 3; i++) {
        if (s_w_date[i]) {
            const char *dt = s_w_fc.days[i].date;
            lv_label_set_text(s_w_date[i], (dt && dt[0]) ? dt : "---");
        }
        if (s_w_desc[i]) {
            const char *tx = s_w_fc.days[i].text_day;
            lv_label_set_text(s_w_desc[i], (tx && tx[0]) ? tx : "--");
        }
        if (s_w_temp[i]) {
            lv_label_set_text_fmt(s_w_temp[i], "%d~%d℃",
                                  s_w_fc.days[i].low, s_w_fc.days[i].high);
        }
    }
}

static void on_weather_refresh(lv_event_t *e)
{
    (void)e;
    s_w_ready = false;
    s_w_ok = false;
    snprintf(s_w_now_buf, sizeof(s_w_now_buf), "正在获取天气");
    if (s_w_now_lb) lv_label_set_text(s_w_now_lb, s_w_now_buf);
    xTaskCreate(weather_fetch_task, "weather", 10240, NULL, 3, NULL)   /*  6K10K  防止栈溢出崩溃  */;
}

static void build_weather(lv_obj_t *root)
{
    lv_obj_t *body = page_scaffold(root, "天气", "未来3天");

    /*  手动刷新按钮 （失败了可以再点 ）*/
    lv_obj_t *rb = lv_button_create(body);
    lv_obj_set_size(rb, 48, 22);
    lv_obj_align(rb, LV_ALIGN_TOP_RIGHT, -2, 8);   /* 往下挪一点 */
    lv_obj_set_style_bg_color(rb, lv_color_black(), 0);
    lv_obj_set_style_border_width(rb, 1, 0);
    lv_obj_set_style_border_color(rb, lv_color_white(), 0);
    lv_obj_set_style_radius(rb, 6, 0);
    lv_obj_center(make_label(rb, "刷新", FONT_SMALL));
    lv_obj_add_event_cb(rb, on_weather_refresh, LV_EVENT_CLICKED, NULL);

    s_w_city_lb = make_label(body, "--", FONT_SMALL);
    lv_obj_set_pos(s_w_city_lb, 6, 0);
    lv_obj_set_style_opa(s_w_city_lb, LV_OPA_60, 0);

    s_w_now_lb = make_label(body, "正在获取天气", FONT_SMALL);
    lv_obj_set_pos(s_w_now_lb, 6, 20);

    for (int i = 0; i < 3; i++) {
        lv_obj_t *row = lv_obj_create(body);
        style_black(row, true);
        lv_obj_set_size(row, LEFT_W - 12, 40);
        lv_obj_set_pos(row, 6, 46 + i * 44);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(row, lv_color_white(), 0);
        lv_obj_set_style_border_opa(row, LV_OPA_20, 0);

        s_w_date[i] = make_label(row, "---", FONT_SMALL);
        lv_obj_set_pos(s_w_date[i], 2, 2);

        s_w_desc[i] = make_label(row, "--", FONT_SMALL);
        lv_obj_set_style_opa(s_w_desc[i], LV_OPA_60, 0);
        lv_obj_set_pos(s_w_desc[i], 2, 20);

        s_w_temp[i] = make_label(row, "--~--℃", FONT_SMALL);
        lv_obj_align(s_w_temp[i], LV_ALIGN_RIGHT_MID, -2, 0);
    }

    if (!s_w_ready) {
        xTaskCreate(weather_fetch_task, "weather", 10240, NULL, 3, NULL)   /*  6K10K  防止栈溢出崩溃  */;
    } else {
        weather_refresh_ui();
    }
}

/* ---------- 小智AI（只搭框架  业务复用已有代码 ）---------- */
/* ===== 小智AI：DeepSeek 一问一答（异步  不卡界面 ）===== */



static void ai_refresh_ui(void)
{
    int st = ai_chat_state();
    if (st == s_ai_last) return;      /*  状态没变就不动控件  省 CPU  */
    s_ai_last = st;

    if (st == 1) {
        if (s_ai_state) lv_label_set_text(s_ai_state, "请求中");
        if (s_ai_ans)   lv_label_set_text(s_ai_ans, "思考中");
    } else if (st == 2) {
        if (s_ai_state) lv_label_set_text(s_ai_state, "完成");
        if (s_ai_ans)   lv_label_set_text(s_ai_ans, ai_chat_answer());
    } else if (st == 3) {
        if (s_ai_state) lv_label_set_text(s_ai_state, "失败");
        if (s_ai_ans)   lv_label_set_text(s_ai_ans, ai_chat_error());
    }
}

/* ===== 小智AI（deepseek）：对话式  ===== */
static lv_obj_t *s_ai_ta = NULL;

static void nw_ai_send_cb(lv_event_t *e)
{
    (void)e;
    const char *q = s_ai_ta ? lv_textarea_get_text(s_ai_ta) : "";
    if (q == NULL || q[0] == 0) return;
    if (s_ai_ans)   lv_label_set_text(s_ai_ans, "...思考中...");
    if (s_ai_state) lv_label_set_text(s_ai_state, "请求中...");
    ai_chat_ask(q);
    if (s_ai_ta) lv_textarea_set_text(s_ai_ta, "");
    s_ai_last = -1;
}

static void build_ai(lv_obj_t *root)
{
    lv_obj_t *body = page_scaffold(root, "deepseek", NULL);

    s_ai_state = make_label(body, "空闲", FONT_SMALL);
    lv_obj_align(s_ai_state, LV_ALIGN_TOP_RIGHT, -8, 0);
    lv_obj_set_style_opa(s_ai_state, LV_OPA_50, 0);

    s_ai_ta = lv_textarea_create(body);
    lv_obj_set_size(s_ai_ta, LEFT_W - 78, 30);
    lv_obj_set_pos(s_ai_ta, 6, 26);
    lv_textarea_set_one_line(s_ai_ta, true);
    lv_textarea_set_max_length(s_ai_ta, 60);
    lv_obj_set_style_bg_color(s_ai_ta, lv_color_black(), 0);
    lv_obj_set_style_text_color(s_ai_ta, lv_color_white(), 0);
    lv_obj_set_style_border_width(s_ai_ta, 1, 0);
    lv_obj_set_style_border_color(s_ai_ta, lv_color_white(), 0);
    lv_obj_set_style_radius(s_ai_ta, 6, 0);

    lv_obj_t *b = lv_button_create(body);
    lv_obj_set_size(b, 60, 30);
    lv_obj_set_pos(b, LEFT_W - 66, 26);
    lv_obj_set_style_bg_color(b, lv_color_black(), 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, lv_color_white(), 0);
    lv_obj_set_style_radius(b, 6, 0);
    lv_obj_center(make_label(b, "发送", FONT_SMALL));
    lv_obj_add_event_cb(b, nw_ai_send_cb, LV_EVENT_CLICKED, NULL);

    nw_make_kb(body, s_ai_ta);

    lv_obj_t *box = lv_obj_create(body);
    style_black(box, true);
    lv_obj_set_size(box, LEFT_W - 12, SCR_H - 66);
    lv_obj_set_pos(box, 6, 62);
    lv_obj_add_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(box, LV_DIR_VER);

    s_ai_ans = make_label(box, "输入问题后点发送", FONT_SMALL);
    lv_obj_set_pos(s_ai_ans, 2, 2);
    lv_obj_set_width(s_ai_ans, LEFT_W - 24);
    lv_label_set_long_mode(s_ai_ans, LV_LABEL_LONG_WRAP);

    s_ai_last = -1;
    ai_refresh_ui();
}

/* ---------- 游戏（只搭容器  业务复用已有代码 ）---------- */
static void build_game(lv_obj_t *root)
{
    /*  2048：直接在左内容区里铺  滑动操作 
       ⚠️ 棋盘自己监听 PRESSED/RELEASED 判方向  不抢菜单的手势  */
    lv_obj_t *body = page_scaffold(root, "游戏", "滑动");
    game2048_create(body);
}

/* ---------- WiFi ---------- */
/* ===== WiFi 页：扫描附近热点（用 esp_wifi 的同步扫描 ）===== */
static lv_obj_t *s_wifi_list  = NULL;
static lv_obj_t *s_wifi_state = NULL;

/*  手机信号格样式（4 格  白色  黑白不引入彩色 ）*/
static void wifi_bars(lv_obj_t *parent, int rssi)
{
    int level = (rssi > -50) ? 4 : (rssi > -62) ? 3 : (rssi > -75) ? 2 : 1;
    for (int b = 0; b < 4; b++) {
        lv_obj_t *bar = lv_obj_create(parent);
        style_black(bar, true);
        int h = 4 + b * 3;
        lv_obj_set_size(bar, 3, h);
        lv_obj_set_pos(bar, b * 5, 14 - h);
        lv_obj_set_style_bg_color(bar, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(bar, (b < level) ? LV_OPA_COVER : LV_OPA_20, 0);
        lv_obj_set_style_radius(bar, 1, 0);
    }
}

/*  点热点  连接（密码用 NVS 里已保存的那个  新热点请用配网页输密码 ）*/
/* ==========================================================================
 *  WiFi 点热点 → 弹密码面板（复用 nw_make_kb）→ 连接 → 显示"连接成功"
 * ========================================================================== */
static lv_obj_t *nw_wf     = NULL;
static lv_obj_t *nw_wf_ta  = NULL;
static char      nw_wf_ssid[33];
static bool      nw_wf_wait = false;

static void nw_wf_close(void)
{
    if (nw_wf) { lv_obj_del(nw_wf); nw_wf = NULL; }
    nw_wf_ta = NULL;
}

static void nw_wf_do_connect(lv_event_t *e)
{
    (void)e;
    const char *pwd = nw_wf_ta ? lv_textarea_get_text(nw_wf_ta) : "";
    if (pwd == NULL) pwd = "";

    wifi_config_t cfg = { 0 };
    size_t n = strlen(nw_wf_ssid);
    if (n >= sizeof(cfg.sta.ssid)) n = sizeof(cfg.sta.ssid) - 1;
    memcpy(cfg.sta.ssid, nw_wf_ssid, n);
    cfg.sta.ssid[n] = 0;

    n = strlen(pwd);
    if (n >= sizeof(cfg.sta.password)) n = sizeof(cfg.sta.password) - 1;
    memcpy(cfg.sta.password, pwd, n);
    cfg.sta.password[n] = 0;

    ESP_LOGI(TAG_UI, "连接 WiFi: %s (pwd len %d)", nw_wf_ssid, (int)strlen(pwd));

    esp_wifi_disconnect();
    if (esp_wifi_set_config(WIFI_IF_STA, &cfg) == ESP_OK) {
        esp_wifi_connect();
        nw_wf_wait = true;
        if (s_wifi_state) lv_label_set_text(s_wifi_state, "连接中...");
    }
    nw_wf_close();
}

static void nw_wf_cancel(lv_event_t *e) { (void)e; nw_wf_close(); }

static void on_wifi_pick(lv_event_t *e)
{
    const char *ssid = (const char *)lv_event_get_user_data(e);
    if (ssid == NULL || ssid[0] == 0) return;

    snprintf(nw_wf_ssid, sizeof(nw_wf_ssid), "%s", ssid);
    nw_wf_close();

    nw_wf = lv_obj_create(lv_layer_top());
    style_black(nw_wf, false);
    lv_obj_set_size(nw_wf, SCR_W, SCR_H);
    lv_obj_center(nw_wf);

    lv_obj_t *tt = make_label(nw_wf, ssid, FONT_SMALL);
    lv_obj_set_pos(tt, 8, 4);

    nw_wf_ta = lv_textarea_create(nw_wf);
    lv_obj_set_size(nw_wf_ta, SCR_W - 16, 30);
    lv_obj_set_pos(nw_wf_ta, 8, 26);
    lv_textarea_set_one_line(nw_wf_ta, true);
    lv_textarea_set_password_mode(nw_wf_ta, true);
    lv_textarea_set_max_length(nw_wf_ta, 63);
    lv_obj_set_style_bg_color(nw_wf_ta, lv_color_black(), 0);
    lv_obj_set_style_text_color(nw_wf_ta, lv_color_white(), 0);
    lv_obj_set_style_border_width(nw_wf_ta, 1, 0);
    lv_obj_set_style_border_color(nw_wf_ta, lv_color_white(), 0);

    lv_obj_t *ok = lv_button_create(nw_wf);
    lv_obj_set_size(ok, 64, 26);
    lv_obj_set_pos(ok, 8, 62);
    lv_obj_set_style_bg_color(ok, lv_color_black(), 0);
    lv_obj_set_style_border_width(ok, 1, 0);
    lv_obj_set_style_border_color(ok, lv_color_white(), 0);
    lv_obj_set_style_radius(ok, 6, 0);
    lv_obj_center(make_label(ok, "连接", FONT_SMALL));
    lv_obj_add_event_cb(ok, nw_wf_do_connect, LV_EVENT_CLICKED, NULL);

    lv_obj_t *cc = lv_button_create(nw_wf);
    lv_obj_set_size(cc, 64, 26);
    lv_obj_set_pos(cc, 80, 62);
    lv_obj_set_style_bg_color(cc, lv_color_black(), 0);
    lv_obj_set_style_border_width(cc, 1, 0);
    lv_obj_set_style_border_color(cc, lv_color_white(), 0);
    lv_obj_set_style_radius(cc, 6, 0);
    lv_obj_center(make_label(cc, "取消", FONT_SMALL));
    lv_obj_add_event_cb(cc, nw_wf_cancel, LV_EVENT_CLICKED, NULL);

    nw_make_kb(nw_wf, nw_wf_ta);
}


#define NW_MAX_AP 12
static char s_ap_ssid[NW_MAX_AP][33];
static int  s_ap_rssi[NW_MAX_AP];
static int  s_ap_n = 0;

/* ---- 在 LVGL 任务里画列表 ---- */
static void nw_wifi_render(void *arg)
{
    (void)arg;
    if (s_wifi_list == NULL) return;
    lv_obj_clean(s_wifi_list);

    if (s_wifi_state)
        lv_label_set_text_fmt(s_wifi_state, "扫描结束 %d个", s_ap_n);

    for (int i = 0; i < s_ap_n; i++) {
        lv_obj_t *row = lv_obj_create(s_wifi_list);
        style_black(row, true);
        lv_obj_set_size(row, LEFT_W - 18, 52);   /* 两行显示 */
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(row, lv_color_white(), 0);
        lv_obj_set_style_border_opa(row, LV_OPA_20, 0);

        lv_obj_t *tx = make_label(row, s_ap_ssid[i], FONT_SMALL);
        lv_obj_set_pos(tx, 4, 4);
        lv_obj_set_width(tx, LEFT_W - 80);
        lv_obj_set_height(tx, 36);
        lv_label_set_long_mode(tx, LV_LABEL_LONG_DOT);   /* 最多两行 */

        lv_obj_t *bars = lv_obj_create(row);
        style_black(bars, true);
        lv_obj_set_size(bars, 24, 18);
        lv_obj_align(bars, LV_ALIGN_RIGHT_MID, -4, 0);
        wifi_bars(bars, s_ap_rssi[i]);

        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, on_wifi_pick, LV_EVENT_CLICKED, (void *)s_ap_ssid[i]);
    }
}

/* ---- 后台任务：阻塞扫描（不卡 LVGL）---- */
static void wifi_scan_task(void *arg)
{
    (void)arg;
    wifi_scan_config_t cfg = { 0 };
    esp_err_t ret = esp_wifi_scan_start(&cfg, true);

    if (ret != ESP_OK) {
        ESP_LOGW(TAG_UI, "扫描失败 0x%x", ret);
        s_ap_n = 0;
    } else {
        uint16_t num = 0;
        esp_wifi_scan_get_ap_num(&num);
        if (num > NW_MAX_AP) num = NW_MAX_AP;
        wifi_ap_record_t *recs = calloc(num ? num : 1, sizeof(wifi_ap_record_t));
        if (recs) {
            esp_wifi_scan_get_ap_records(&num, recs);
            s_ap_n = (int)num;
            for (int i = 0; i < s_ap_n; i++) {
                snprintf(s_ap_ssid[i], sizeof(s_ap_ssid[i]), "%s", (const char *)recs[i].ssid);
                s_ap_rssi[i] = (int)recs[i].rssi;
            }
            free(recs);
        }
        ESP_LOGI(TAG_UI, "扫描完成 %d 个", s_ap_n);
    }
    lv_async_call(nw_wifi_render, NULL);
    vTaskDelete(NULL);
}

static void on_wifi_scan(lv_event_t *e)
{
    (void)e;
    if (s_wifi_state) lv_label_set_text(s_wifi_state, "扫描中...");
    if (xTaskCreate(wifi_scan_task, "wifi_scan", 10240, NULL, 3, NULL) != pdPASS) {
        if (s_wifi_state) lv_label_set_text(s_wifi_state, "任务创建失败");
    }
}

static void build_wifi(lv_obj_t *root)
{
    lv_obj_t *btn = lv_button_create(root);
    lv_obj_set_size(btn, 66, 22);
    lv_obj_set_pos(btn, 4, 2);
    lv_obj_set_style_bg_color(btn, lv_color_black(), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_white(), 0);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_center(make_label(btn, "扫描", FONT_SMALL));
    lv_obj_add_event_cb(btn, on_wifi_scan, LV_EVENT_CLICKED, NULL);

    s_wifi_state = make_label(root, "", FONT_SMALL);
    lv_obj_align(s_wifi_state, LV_ALIGN_TOP_RIGHT, -6, 4);
    lv_obj_set_style_opa(s_wifi_state, LV_OPA_60, 0);

    s_wifi_list = lv_obj_create(root);
    style_black(s_wifi_list, true);
    lv_obj_set_pos(s_wifi_list, 0, 28);
    lv_obj_set_size(s_wifi_list, LEFT_W, SCR_H - 28);
    lv_obj_set_flex_flow(s_wifi_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(s_wifi_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_wifi_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_wifi_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_row(s_wifi_list, 0, 0);
}

/* ---------- 息屏页的【页面内版本】（可选一般用全屏覆盖那个 ）---------- */
static void build_off(lv_obj_t *root)
{
    lv_obj_t *body = page_scaffold(root, "息屏", "idle");
    lv_obj_t *l = make_label(body, "（用 ui_screenoff_show() 走全屏息屏 ）", FONT_SMALL);
    lv_obj_set_style_opa(l, LV_OPA_40, 0);
    lv_obj_center(l);
}

/* ==========================================================================
 *  10. 弧形菜单创建
 * ========================================================================== */
static const struct {
    const char              *label;
    ui_page_t                page;
    const lv_image_dsc_t    *icon;      /*  图标（旧 UI 那批  没有就写字 ）*/
    int                      iscale;    /*  每个图标的缩放  源图尺寸不同要分别调  */
} MENU_DEF[] = {
    /*  菜单图标：统一用我生成的 40x40 单色图标 （缩放 256 = 原尺寸  最清晰 ）
       主页仍然用你的 bilibili 卡通 （你指定过 ）*/
    { "主页",  UI_PAGE_HOME,    &home_pet,     110 },
    { "消息",  UI_PAGE_MSG,     &ic_msg,       256 },
    { "设置",  UI_PAGE_SET,     &ic_set,       256 },
    { "更新",  UI_PAGE_OTA,     &ic_ota,       256 },
    { "日历",  UI_PAGE_CAL,     &ic_cal,       256 },
    { "日程",  UI_PAGE_SCHED,   &ic_sched,     256 },
    { "天气",  UI_PAGE_WEATHER, &ic_weather,   256 },
    { "AI",    UI_PAGE_AI,      &ic_ai,        256 },
    { "游戏",  UI_PAGE_GAME,    &ic_game,      256 },
    { "云端",  UI_PAGE_CLOUD,   &ic_cloud,     256 },
    { "传感",  UI_PAGE_SENSOR,  &ic_temp,      256 },
    { "WiFi",  UI_PAGE_WIFI,    &ic_wifi,      256 },
};

/*  菜单卡片风格（3 种挑一个 ）
   0 = 描边卡片（当前）
   1 = 无边框纯图标+文字（最干净）
   2 = 粗边大圆角（最醒目）*/
static void menu_apply_style(void)
{
    for (int i = 0; i < s_menu_cnt; i++) {
        lv_obj_t *it = s_items[i];
        if (it == NULL) continue;

        lv_obj_set_style_border_color(it, lv_color_white(), 0);

        if (s_card_style == 0) {
            lv_obj_set_style_border_width(it, 1, 0);
            lv_obj_set_style_border_opa(it, LV_OPA_50, 0);
            lv_obj_set_style_radius(it, 6, 0);
            lv_obj_set_style_bg_opa(it, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(it, lv_color_black(), 0);
            lv_obj_set_style_bg_image_src(it, NULL, 0);
        } else if (s_card_style == 1) {
            lv_obj_set_style_border_width(it, 0, 0);
            lv_obj_set_style_radius(it, 0, 0);
            lv_obj_set_style_bg_opa(it, LV_OPA_TRANSP, 0);
            lv_obj_set_style_bg_image_src(it, NULL, 0);
        } else if (s_card_style == 2) {
            lv_obj_set_style_border_width(it, 3, 0);
            lv_obj_set_style_border_opa(it, LV_OPA_80, 0);
            lv_obj_set_style_radius(it, 14, 0);
            lv_obj_set_style_bg_opa(it, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(it, lv_color_black(), 0);
            lv_obj_set_style_bg_image_src(it, NULL, 0);
        } else {
            /*  风格 3/4/5：用【图片】当边框 （我生成的 3 套 ）*/
            const void *img = (s_card_style == 3) ? (const void *)&menu_frame_round
                            : (s_card_style == 4) ? (const void *)&menu_frame_corner
                                                  : (const void *)&menu_frame_bar;
            lv_obj_set_style_border_width(it, 0, 0);
            lv_obj_set_style_radius(it, 0, 0);
            lv_obj_set_style_bg_opa(it, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(it, lv_color_black(), 0);
            lv_obj_set_style_bg_image_src(it, img, 0);   /*  图片铺底  */
        }
    }
}

/* ==========================================================================
 *   点按切菜单（替代滑动  用户要求）
 *    点屏幕【上半】 上一个    点【下半】 下一个 
 *    "机械感"：切完让选中卡片向左顶 7px 再弹回 
 *    （整数平移  几乎零开销  不用缩放 ）
 * ========================================================================== */
static void menu_bump(lv_obj_t *it)
{
    if (it == NULL) return;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, it);
    lv_anim_set_exec_cb(&a, anim_set_tx);
    lv_anim_set_values(&a, -7, 0);
    lv_anim_set_duration(&a, 120);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void menu_tap_cb(lv_event_t *e)
{
    (void)e;
    if (s_menu_cnt <= 0) return;

    lv_indev_t *indev = lv_indev_active();
    if (indev == NULL) return;

    lv_point_t p;
    lv_indev_get_point(indev, &p);

    int dir = (p.y < SCR_H / 2) ? -1 : 1;        /* 上半 上一个 / 下半 下一个  */

    int ns = s_sel + dir;
    if (ns < 0) ns = s_menu_cnt - 1;             /* 循环  */
    if (ns >= s_menu_cnt) ns = 0;

    s_sel         = ns;
    s_page_raw    = ns;
    s_scroll      = (float)ns;
    s_vel         = 0.0f;
    s_layout_done = false;

    menu_layout();
    menu_bump(s_items[ns]);
    on_selection_changed(ns);
}

static void menu_build(void)
{
    s_menu = lv_obj_create(s_scr);
    style_black(s_menu, true);
    lv_obj_set_pos(s_menu, MENU_X0, 0);          /*  菜单容器左上角 = 分割线右侧  */
    lv_obj_set_size(s_menu, MENU_W, SCR_H);      /*  触摸感应区（条目另挂在屏幕上 ）*/
    lv_obj_add_flag(s_menu, LV_OBJ_FLAG_CLICKABLE);
    /*  按你要求：不再滑动  改成【点按】
         点上半  上一个 
         点下半  下一个 
         机械感：切完立刻顿一下（选中的卡片向左顶一下再回来 ）*/
    lv_obj_add_event_cb(s_menu, menu_tap_cb, LV_EVENT_CLICKED, NULL);

    s_menu_cnt = (int)(sizeof(MENU_DEF) / sizeof(MENU_DEF[0]));

    for (int i = 0; i < s_menu_cnt; i++) {
        /*  挂在【屏幕】上而不是 s_menu 上 （s_menu 只当触摸感应区 ）
           这样 lv_obj_set_pos 的绝对坐标才可靠  不会被容器重置/裁剪  */
        lv_obj_t *it = lv_obj_create(s_scr);
        style_black(it, false);                          /* 黑底  */
        lv_obj_set_size(it, ITEM_W, ITEM_H);
        lv_obj_set_style_border_width(it, 1, 0);         /* 白色细边（单色  不是彩色 ）*/
        lv_obj_set_style_border_color(it, lv_color_white(), 0);
        lv_obj_set_style_border_opa(it, LV_OPA_50, 0);
        lv_obj_set_style_radius(it, 6, 0);
        lv_obj_set_style_transform_pivot_x(it, lv_pct(50), 0);   /* 以中心缩放  */
        lv_obj_set_style_transform_pivot_y(it, lv_pct(50), 0);
        lv_obj_remove_flag(it, LV_OBJ_FLAG_CLICKABLE);          /* 点击交给父容器  否则拖动会被吃掉  */

        /*  布局按你要求：**图标在上、文字在下**  且图标比文字大  */
        if (MENU_DEF[i].icon != NULL) {
            lv_obj_t *img = lv_image_create(it);
            lv_image_set_src(img, MENU_DEF[i].icon);
            lv_image_set_scale(img, MENU_DEF[i].iscale ? MENU_DEF[i].iscale : 150);   /*  每个图标自己的缩放  */
            lv_obj_align(img, LV_ALIGN_TOP_MID, 0, 6); /* 顶部居中  */

            lv_obj_t *lb = make_label(it, MENU_DEF[i].label, FONT_SMALL);
            lv_obj_align(lb, LV_ALIGN_BOTTOM_MID, 0, -6);  /* 底部居中  */
        } else {
            lv_obj_t *lb = make_label(it, MENU_DEF[i].label, FONT_SMALL);
            lv_obj_center(lb);
        }

        s_items[i]      = it;
        s_menu_pages[i] = MENU_DEF[i].page;
    }
}

/* ==========================================================================
 *  11. 息屏页（息屏入口  全屏独立  会盖住右侧菜单 ）
 * ========================================================================== */
static void off_update_clock(void)
{
    if (s_off == NULL) return;

    time_t t = time(NULL);
    struct tm tm_n;
    localtime_r(&t, &tm_n);                  /*  一定用 _r  线程安全  */

    char d[64], c[32];   /*  放大：中文 3 字节多个  小了会报 format-truncation  */
    snprintf(d, sizeof(d), "%04d年%02d月%02d日",
             tm_n.tm_year + 1900, tm_n.tm_mon + 1, tm_n.tm_mday);
    snprintf(c, sizeof(c), "%02d:%02d:%02d", tm_n.tm_hour, tm_n.tm_min, tm_n.tm_sec);

    if (s_off_date) lv_label_set_text(s_off_date, d);
    if (s_off_time) lv_label_set_text(s_off_time, c);
}

static void off_wake_cb(lv_event_t *e)
{
    (void)e;
    ui_screenoff_hide();
}

void ui_screenoff_show(const lv_image_dsc_t *bg)
{
    if (s_off != NULL) return;

    /*  不传图  默认用用户提供的背景图 
       想换成纯黑：ui_screenoff_show 传一个特殊值或直接把下面这行删掉  */
    if (bg == NULL) bg = &screen_off_bg;

    /*  建在 lv_layer_top() 上  所以它【盖住一切】包含右侧弧形菜单 
       满足"息屏时菜单被隐藏"的要求  */
    s_off = lv_obj_create(lv_layer_top());
    style_black(s_off, false);                       /* 纯黑底  */
    lv_obj_set_size(s_off, SCR_W, SCR_H);
    lv_obj_set_pos(s_off, 0, 0);
    lv_obj_add_flag(s_off, LV_OBJ_FLAG_CLICKABLE);    /*  必须可点  否则点击会穿透到菜单  */
    lv_obj_add_event_cb(s_off, off_wake_cb, LV_EVENT_PRESSED, NULL);

    /* ---- 背景图（可选）---- */
    if (bg != NULL) {
        lv_obj_t *img = lv_image_create(s_off);
        lv_image_set_src(img, bg);
        lv_obj_center(img);
        /* 压暗一点保证白字可读 （单色处理不引入彩色 ）*/
        /*  现在是【原色】图  不压暗  全不透明  按你要求好看优先  */
    }

    /* ---- 日期（上）---- */
    s_off_date = make_label(s_off, "----年--月--日", FONT_SMALL);
    lv_obj_set_style_text_color(s_off_date, lv_color_black(), 0);
    /*  尺寸缩放已删除  原因：缩放是【非整数倍】 LVGL 每次刷新都要重采样 
       而时钟每秒刷新  整屏重绘  CPU1 吃满  task_wdt  重启 （实测 31 秒重启 ）
       改用 40px 字体直接显示  不缩放  性能安全  */
    lv_obj_align(s_off_date, LV_ALIGN_CENTER, 0, -36);

    /* ---- 大时钟（中）---- */
    s_off_time = make_label(s_off, "--:--:--", FONT_BIG);
    lv_obj_set_style_text_color(s_off_time, lv_color_black(), 0);
    /*  尺寸缩放已删除  原因：缩放是【非整数倍】 LVGL 每次刷新都要重采样 
       而时钟每秒刷新  整屏重绘  CPU1 吃满  task_wdt  重启 （实测 31 秒重启 ）
       改用 40px 字体直接显示  不缩放  性能安全  */
    lv_obj_align(s_off_time, LV_ALIGN_CENTER, 0, 10);

    off_update_clock();
    ESP_LOGI(TAG_UI, "息屏页已显示（菜单被覆盖隐藏 ）触摸唤醒 ");
}

void ui_screenoff_hide(void)
{
    if (s_off == NULL) return;
    lv_obj_del(s_off);
    s_off = NULL;
    s_off_date = NULL;
    s_off_time = NULL;
    ESP_LOGI(TAG_UI, "息屏页已关闭回到主布局 ");
}

/* ==========================================================================
 *  12. 周期任务：刷时钟 + 业务 tick
 * ========================================================================== */
static void ui_tick_cb(lv_timer_t *t)
{
    (void)t;

    /* 首页时钟  */
    time_t now = time(NULL);
    struct tm tm_n;
    localtime_r(&now, &tm_n);

    char d[64], c[32];   /*  放大：中文 3 字节多个  小了会报 format-truncation  */
    snprintf(d, sizeof(d), "%04d年%02d月%02d日",
             tm_n.tm_year + 1900, tm_n.tm_mon + 1, tm_n.tm_mday);
    snprintf(c, sizeof(c), "%02d:%02d:%02d", tm_n.tm_hour, tm_n.tm_min, tm_n.tm_sec);
    ui_home_set_clock(d, c);

    /*  首页显示【真实】温度/光照 （之前一直显示 "--"  因为没人填空 ）*/
    {
        char info[64];
        snprintf(info, sizeof(info), "温度 %.1f℃   光照 %d%%", adc_temp_c(), adc_light_pct());
        ui_home_set_info(info);
    }

    if (s_off) off_update_clock();          /* 息屏页也要走  */

    /* ============================================================
     * 息屏入口（自动触发）
     *   LVGL 自带"多久没输入"的统计  不用自己记时间戳 
     *   lv_display_get_inactive_time() 返回距今毫秒数 
     *   超过 IDLE_OFF_MS  弹全屏息屏页（会盖住右侧菜单 ）
     *   ⚠️ 息屏页自己就注册了 PRESSED  摸一下自动唤醒 
     * ============================================================ */
    if (s_off == NULL) {
        uint32_t idle = lv_display_get_inactive_time(lv_display_get_default());
        if (idle >= IDLE_OFF_MS) {
            ESP_LOGI(TAG_UI, "闲置 %u ms  息屏 ", (unsigned)idle);
            ui_screenoff_show(NULL);        /* 传 NULL = 纯黑底 
                                                想放背景图：改成 ui_screenoff_show(&你的图片)  */
        }
    }

    ui_on_tick(500);                        /*  业务钩子  */

    /*  WiFi 连接/断开  屏幕弹消息 （按你要求）
       用状态【变化】判断  不是每 500ms 都发  */
    {
        static bool last_wifi = false;
        bool now = boot_wifi_connected();
        if (s_cur != UI_PAGE_OFF && now != last_wifi) {
            last_wifi = now;
            msg_post(msg_src_system, now ? "WIFI已连接" : "WIFI未连接");
        }
    }

    /*  消息/日程列表自动刷新（数量变了才重建  不能每帧重建 ）*/
    if (s_cur == UI_PAGE_SET) set_refresh();
    if (s_cur == UI_PAGE_WEATHER && s_w_ready) weather_refresh_ui();
    if (s_cur == UI_PAGE_AI) ai_refresh_ui();
    /* ★ WiFi 连接结果提示 */
    if (nw_wf_wait) {
        if (boot_wifi_connected()) {
            nw_wf_wait = false;
            if (s_wifi_state) lv_label_set_text(s_wifi_state, "连接成功");
            msg_post(msg_src_system, "wifi连接成功");
            ESP_LOGI(TAG_UI, "WiFi 连接成功");
        }
    }
    nw_dot_update();
    nw_cloud_tick();                 /* 云端：收下行（全局）*/
    if (s_cur == UI_PAGE_SENSOR) nw_sensor_tick();
    if (s_cur == UI_PAGE_MSG) {
        int n = msg_count();
        if (n != s_msg_shown) { s_msg_shown = n; msg_list_rebuild(); }
    }
}

/* ==========================================================================
 *  13. 入口
 * ========================================================================== */
/* ==========================================================================
 *   开机自检页（4 项全部通过才进主界面 ）
 *
 *  为什么要它：
 *    上电后 WiFi/时间/云端/版本上报都是【异步】的 
 *    如果直接进主界面  用户会看到"没网络/时间 1970"这种半成品状态 
 *     先用一页把它们逐个等出来  全绿才放行 
 *
 *  4 项分别对应：
 *     WiFi 连上      boot_wifi_connected()
 *     时间校准(SNTP)  init_sntp_is_synced()
 *     OneNET 连上    get_onenet_state()
 *     版本上报成功    get_my_onenet_ota_upload_version()
 *
 *  ⚠️ 必须有【超时兜底】 否则没网时永远进不去 
 *      超时 12 秒就直接进（并在最后一行显示"跳过"）
 * ========================================================================== */
#define BOOT_TIMEOUT_MS   30000   /*  2030s：实测 4 项要 17纷  留足余量  */
#define BOOT_TICK_MS      200
#define BOOT_HOLD_MS      800      /* 全绿后再停一下让用户看到  */

static lv_obj_t *s_boot     = NULL;
static lv_obj_t *s_boot_mark[4] = {0};
static lv_obj_t *s_boot_all_lb = NULL;   /*  全部成功的结论行  */
static lv_timer_t *s_boot_t = NULL;
static uint32_t  s_boot_ms  = 0;
static bool      s_boot_all = false;
static uint32_t  s_boot_hold = 0;   /* 全绿时刻 */

static const char *BOOT_NAMES[4] = { "WiFi", "SNTP", "ONENET", "VERSION" };

static bool boot_item_ok(int i)
{
    switch (i) {
    case 0: return boot_wifi_connected();
    case 1: return init_sntp_is_synced();
    case 2: return get_onenet_state();
    default:return get_my_onenet_ota_upload_version();
    }
}

static void boot_close(void)
{
    if (s_boot_t) { lv_timer_del(s_boot_t); s_boot_t = NULL; }
    if (s_boot)   { lv_obj_del(s_boot);     s_boot = NULL; }
    ESP_LOGI(TAG_UI, "自检页结束  进入主界面 ");
}

static void boot_tick_cb(lv_timer_t *t)
{
    (void)t;
    s_boot_ms += BOOT_TICK_MS;

    bool all = true;
    for (int i = 0; i < 4; i++) {
        bool ok = boot_item_ok(i);
        if (!ok) all = false;
        if (s_boot_mark[i]) {
            /*  纯黑白：只靠字符区分  不用颜色  */
            /*  按你要求：连接中【红】 成功【蓝】（本页例外允许彩色 ）*/
            lv_label_set_text(s_boot_mark[i], ok ? "success!!!" : "connecting...");
            lv_obj_set_style_text_color(s_boot_mark[i],
                                        ok ? lv_color_make(60, 220, 100)
                                           : lv_color_make(255, 70, 70), 0);
        }
    }

    if (all && !s_boot_all) {
        s_boot_all = true;
        ESP_LOGI(TAG_UI, "自检 4 项全部通过 ");
    }
    if (s_boot_all_lb) {
        lv_label_set_text(s_boot_all_lb, all ? "success" : "");
    }

    if (s_boot_all && s_boot_hold == 0) s_boot_hold = s_boot_ms;   /* 记下全绿时刻  */
    bool done = (s_boot_hold != 0) ? ((s_boot_ms - s_boot_hold) >= 2000)
                                   : (s_boot_ms >= BOOT_TIMEOUT_MS);
    if (done) {
        if (s_boot_hold == 0) {
            ESP_LOGW(TAG_UI, "自检超时逐项状态");
            for (int i = 0; i < 4; i++)
                ESP_LOGW(TAG_UI, "   [%d] %s = %s", i, BOOT_NAMES[i], boot_item_ok(i) ? "OK" : "未通过");
        }
        boot_close();
    }
}

/* 建自检页（在 lv_layer_top 上  盖住一切 ）*/
static void boot_create(void)
{
    s_boot = lv_obj_create(lv_layer_top());
    style_black(s_boot, false);                 /* 纯黑底  */
    lv_obj_set_size(s_boot, SCR_W, SCR_H);
    lv_obj_set_pos(s_boot, 0, 0);

    /*  按【上一版 UI 的开机页】重做（不是你嫌弃的临时版 ）
       上一版的样子：
         顶部：大标题 "ESP32-S3 智能终端" 
         中间：一个【转圈动画】的圆环 （旧版是 arc 转圈  这里用旋转的弧线重现）
         下面：4 行检查项  每行 名字 +  ----------- + 
         底部：卡通图 
       ⚠️ 纯黑白  不用颜色  */

    lv_obj_t *title = make_label(s_boot, "ESP32-S3 智能终端", FONT_SMALL);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    /* ---- 中间的转圈弧线（旧版就是这个感觉 ）---- */
    lv_obj_t *ring = lv_arc_create(s_boot);
    lv_obj_set_size(ring, 54, 54);
    lv_obj_align(ring, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_remove_style(ring, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_width(ring, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ring, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(ring, LV_OPA_20, LV_PART_MAIN);
    lv_obj_set_style_arc_width(ring, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(ring, lv_color_white(), LV_PART_INDICATOR);
    lv_arc_set_bg_angles(ring, 0, 360);
    lv_arc_set_angles(ring, 0, 70);
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE);

    /* 转圈动画：只改角度  只重画这一小块  开销很小  */
    {
        lv_anim_t ra;
        lv_anim_init(&ra);
        lv_anim_set_var(&ra, ring);
        lv_anim_set_exec_cb(&ra, (lv_anim_exec_xcb_t)lv_arc_set_rotation);
        lv_anim_set_values(&ra, 0, 360);
        lv_anim_set_duration(&ra, 1400);
        lv_anim_set_repeat_count(&ra, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&ra);
    }

    /* ---- 4 行检查项（旧版是 名字 + 一串点 + 勾 ）---- */
    for (int i = 0; i < 4; i++) {
        lv_obj_t *nm = make_label(s_boot, BOOT_NAMES[i], FONT_SMALL);
        lv_obj_set_pos(nm, 34, 96 + i * 26);

        lv_obj_t *dots = make_label(s_boot, "-----------", FONT_SMALL);
        lv_obj_set_style_opa(dots, LV_OPA_30, 0);
        lv_obj_set_pos(dots, 90, 96 + i * 26);

        s_boot_mark[i] = make_label(s_boot, "", FONT_SMALL);
        lv_obj_align(s_boot_mark[i], LV_ALIGN_TOP_RIGHT, -34, 96 + i * 26);
    }

    /*  按你要求：开机页的卡通删掉  */

    /* 全部成功后的结论行（蓝色 ）*/
    s_boot_all_lb = make_label(s_boot, "", FONT_SMALL);
    lv_obj_set_style_text_color(s_boot_all_lb, lv_color_make(60, 220, 100), 0);
    lv_obj_align(s_boot_all_lb, LV_ALIGN_BOTTOM_MID, 0, -18);

    s_boot_ms = 0;
    s_boot_all = false;
    s_boot_t = lv_timer_create(boot_tick_cb, BOOT_TICK_MS, NULL);
    ESP_LOGI(TAG_UI, "自检页已显示  等 4 项通过（最多 %d 秒 ）", BOOT_TIMEOUT_MS / 1000);
}


/*  删掉主页项的殇文字（只留图标  不改其他结构 ）*/
static void nw_hide_home_label(void)
{
    if (s_items[0] == NULL) return;
    uint32_t n = lv_obj_get_child_count(s_items[0]);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *c = lv_obj_get_child(s_items[0], i);
        if (c && lv_obj_check_type(c, &lv_label_class)) {
            lv_obj_del(c);
            return;
        }
    }
}


/* 消息红点：红色圆圈 + 未读数 */
static lv_obj_t *nw_dot     = NULL;
static lv_obj_t *nw_dot_num = NULL;

static void nw_dot_create(lv_obj_t *parent)
{
    nw_dot = lv_obj_create(parent);
    style_black(nw_dot, false);
    lv_obj_set_size(nw_dot, 20, 20);
    lv_obj_align(nw_dot, LV_ALIGN_TOP_RIGHT, -2, 2);
    lv_obj_set_style_radius(nw_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(nw_dot, lv_color_make(230, 40, 40), 0);
    lv_obj_set_style_bg_opa(nw_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(nw_dot, 0, 0);
    lv_obj_remove_flag(nw_dot, LV_OBJ_FLAG_CLICKABLE);

    nw_dot_num = make_label(nw_dot, "0", FONT_SMALL);
    lv_obj_set_style_text_color(nw_dot_num, lv_color_white(), 0);
    lv_obj_center(nw_dot_num);
    lv_obj_add_flag(nw_dot, LV_OBJ_FLAG_HIDDEN);
}

static void nw_dot_update(void)
{
    if (nw_dot == NULL || nw_dot_num == NULL) return;
    int n = msg_unread();
    if (n <= 0) { lv_obj_add_flag(nw_dot, LV_OBJ_FLAG_HIDDEN); return; }
    lv_obj_remove_flag(nw_dot, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text_fmt(nw_dot_num, (n > 9) ? "9+" : "%d", n);
}

void ui_init(void)
{
    ESP_LOGI(TAG_UI, "初始化全新 UI（%dx%d分割线 x=%d）", SCR_W, SCR_H, DIVIDER_X);

    /* ----  整个屏幕：纯黑底  ---- */
    s_scr = lv_obj_create(NULL);
    style_black(s_scr, false);
    lv_obj_set_style_bg_color(s_scr, lv_color_black(), 0);

    /* ----  左内容区（子控件超出会被裁掉  保证不越过分割线 ）---- */
    s_content = lv_obj_create(s_scr);
    style_black(s_content, true);
    lv_obj_set_pos(s_content, 0, 0);
    lv_obj_set_size(s_content, LEFT_W, SCR_H);
    /*  默认就是裁剪子对象的  千万不要加 LV_OBJ_FLAG_OVERFLOW_VISIBLE  */

    /* ----  右侧弧形菜单（永不删除 ）---- */
    menu_build();
    nw_hide_home_label();
    if (s_items[1]) nw_dot_create(s_items[1]);   /* 消息红点 */   /*  删掉主页两个字  */

    /* ----  首页（上电默认 ）---- */
    s_sel    = 0;
    s_scroll = 0.0f;
    lv_obj_t *home = ui_page_root(UI_PAGE_HOME);
    lv_obj_remove_flag(home, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(home, LV_OPA_COVER, 0);
    s_cur = UI_PAGE_HOME;
    ui_on_page_enter(UI_PAGE_HOME);
    menu_layout();

    /* ----  物理定时器 + 业务定时器 ---- */
    lv_timer_create(menu_physics_cb, PHYS_PERIOD_MS, NULL);
    lv_timer_create(ui_tick_cb, 500, NULL);

    /*  菜单自检（一次性日志  用来确认菜单真的建出来并对 ）
       排查"右边看不见菜单" 时非常有用  可以留着以便以后回归  */
    /*  关键：get_x/get_y 读的是【已刷新】的坐标 
       不先 update_layout 就永远是 0  会误判成"没摆好" */
    lv_obj_update_layout(s_scr);
    ESP_LOGI(TAG_UI, "菜单自检: 屏 %dx%d, 内容区 %d, 菜单容器 x=%d y=%d w=%d h=%d, 条目数=%d",
             SCR_W, SCR_H, LEFT_W,
             (int)lv_obj_get_x(s_menu), (int)lv_obj_get_y(s_menu),
             (int)lv_obj_get_width(s_menu), (int)lv_obj_get_height(s_menu), s_menu_cnt);
    for (int i = 0; i < s_menu_cnt && i < 3; i++) {
        lv_obj_t *it = s_items[i];
        ESP_LOGI(TAG_UI, "  条目%d: x=%d y=%d w=%d h=%d 隐藏=%d 透明度=%d",
                 i, (int)lv_obj_get_x(it), (int)lv_obj_get_y(it),
                 (int)lv_obj_get_width(it), (int)lv_obj_get_height(it),
                 (int)lv_obj_has_flag(it, LV_OBJ_FLAG_HIDDEN),
                 (int)lv_obj_get_style_opa(it, 0));
    }

    lv_screen_load(s_scr);

    ESP_LOGI(TAG_UI, "UI 就绪  右侧弧形菜单常驻  左内容区可切页 ");

    /*  最后弹【自检页】：4 项全过（或超时）才自动关闭 
       所以上电先看到自检页  通过后才看到主页  */
    boot_create();
}
