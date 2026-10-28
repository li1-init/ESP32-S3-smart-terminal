/* ==========================================================================
 *  game2048.c  黑白 2048
 *
 *  规则（和手机上的 2048 一样 ）：
 *     4x4 棋盘  每次滑动  所有块往那个方向挤 
 *     相邻且数字相同的两块【合并成一块 2】 一次滑动里每块只合并一次 
 *     滑动后如果有变化  随机在空格里生成 2 或 4 
 *     填满且无法合并  游戏结束 
 *
 *  实现要点（为什么这么写）：
 *     全部用 lv_obj + lv_label 画（16 个格子 ）不用 canvas  省内存 
 *     每次状态变了【整体重画】 16 个对象  很便宜  不用增量更新  复杂易错 
 *     触摸用 PRESSED/RELEASED 两点求方向  不需要手势库 
 * ========================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"

#include "game2048.h"

/*  字体声明必须在 lvgl.h 之后 （game2048.h 里已包含 lvgl.h ）*/
LV_FONT_DECLARE(lv_font_ZiTiQuanWeiJunHeiW22_18);

#define TAG_G "2048"

#define N        4
#define CELL_GAP 3

static lv_obj_t *s_board   = NULL;
static lv_obj_t *s_cells[N][N];
static lv_obj_t *s_score   = NULL;
static lv_obj_t *s_btn_new = NULL;

static uint16_t  s_g[N][N];
static uint32_t  s_pts = 0;
static bool      s_over = false;

static lv_point_t s_press_pt;

/* ---------------- 工具 ---------------- */
static void grid_clear(void)
{
    for (int r = 0; r < N; r++)
        for (int c = 0; c < N; c++)
            s_g[r][c] = 0;
}

static int empty_count(void)
{
    int n = 0;
    for (int r = 0; r < N; r++)
        for (int c = 0; c < N; c++)
            if (s_g[r][c] == 0) n++;
    return n;
}

/* 随机放一个 2 或 4  */
static bool spawn_tile(void)
{
    int total = empty_count();
    if (total <= 0) return false;

    int pick = (int)(esp_random() % (uint32_t)total);
    uint16_t v = (esp_random() % 10u == 0u) ? 4 : 2;   /* 10% 出 4  像原版  */

    for (int r = 0; r < N; r++) {
        for (int c = 0; c < N; c++) {
            if (s_g[r][c] == 0) {
                if (pick-- == 0) { s_g[r][c] = v; return true; }
            }
        }
    }
    return false;
}

/* 数字  亮度（越大越亮  单色 ）*/
static lv_opa_t value_opa(uint16_t v)
{
    switch (v) {
    case 0:    return LV_OPA_TRANSP;
    case 2:    return LV_OPA_30;
    case 4:    return LV_OPA_40;
    case 8:    return LV_OPA_50;
    case 16:   return LV_OPA_60;
    case 32:   return LV_OPA_70;
    case 64:   return LV_OPA_80;
    case 128:  return LV_OPA_90;
    default:   return LV_OPA_COVER;
    }
}

/* 重画整盘 （状态变了就全画  简单可靠 ）*/
static void board_render(void)
{
    if (s_board == NULL) return;

    lv_obj_update_layout(s_board);
    int bw = lv_obj_get_content_width(s_board);
    int bh = lv_obj_get_content_height(s_board);
    int cw = (bw - CELL_GAP * (N + 1)) / N;
    int ch = (bh - CELL_GAP * (N + 1)) / N;

    for (int r = 0; r < N; r++) {
        for (int c = 0; c < N; c++) {
            lv_obj_t *cell = s_cells[r][c];
            if (cell == NULL) continue;

            lv_obj_set_pos(cell, CELL_GAP + c * (cw + CELL_GAP),
                                 CELL_GAP + r * (ch + CELL_GAP));
            lv_obj_set_size(cell, cw, ch);

            uint16_t v = s_g[r][c];

            /* 底色：空格透明；有数字  白色按数值调透明度  */
            lv_obj_set_style_bg_color(cell, lv_color_white(), 0);
            lv_obj_set_style_bg_opa(cell, value_opa(v), 0);
            lv_obj_set_style_border_width(cell, 1, 0);
            lv_obj_set_style_border_color(cell, lv_color_white(), 0);
            lv_obj_set_style_border_opa(cell, LV_OPA_40, 0);

            lv_obj_t *lab = lv_obj_get_child(cell, 0);
            if (lab == NULL) continue;

            if (v == 0) {
                lv_label_set_text(lab, "");
            } else {
                lv_label_set_text_fmt(lab, "%u", (unsigned)v);
                /*  高数值格子是"白底" 字要变黑  否则看不见  */
                lv_obj_set_style_text_color(lab,
                    (v >= 64) ? lv_color_black() : lv_color_white(), 0);
            }
        }
    }

    if (s_score) {
        if (s_over) lv_label_set_text_fmt(s_score, "结束 得分 %u", (unsigned)s_pts);
        else        lv_label_set_text_fmt(s_score, "得分 %u", (unsigned)s_pts);
    }
    if (s_btn_new) {
        if (s_over) lv_obj_remove_flag(s_btn_new, LV_OBJ_FLAG_HIDDEN);
        else        lv_obj_add_flag(s_btn_new, LV_OBJ_FLAG_HIDDEN);
    }
}

/* ---------------- 移动 / 合并核心 ---------------- */
/* 把一维数组往左压（返回是否有变化 ）*/
static bool slide_line(uint16_t *line, uint32_t *gained)
{
    uint16_t out[N] = {0};
    int w = 0;

    /*  去掉空的挤到左边 */
    for (int i = 0; i < N; i++) {
        if (line[i] != 0) out[w++] = line[i];
    }
    /*  相邻相同就合并（一次滑动每块只合一次 ）*/
    for (int i = 0; i + 1 < w; i++) {
        if (out[i] != 0 && out[i] == out[i + 1]) {
            out[i] = (uint16_t)(out[i] * 2);
            *gained += out[i];
            for (int j = i + 1; j < w - 1; j++) out[j] = out[j + 1];
            out[w - 1] = 0;
            w--;
        }
    }
    /*  写回 */
    bool changed = false;
    for (int i = 0; i < N; i++) {
        if (line[i] != out[i]) changed = true;
        line[i] = out[i];
    }
    return changed;
}

/* dir: 0=左 1=右 2=上 3=下  */
static bool do_move(int dir)
{
    bool changed = false;
    uint32_t gained = 0;
    uint16_t line[N];

    for (int i = 0; i < N; i++) {
        /*  取出第 i 条线（按方向摆成"往左压"的顺序 ）*/
        for (int j = 0; j < N; j++) {
            int r = 0, c = 0;
            switch (dir) {
            case 0: r = i; c = j;         break;   /* 左：行正序 */
            case 1: r = i; c = N - 1 - j; break;   /* 右：行反序 */
            case 2: r = j; c = i;         break;   /* 上：列正序 */
            default:r = N - 1 - j; c = i; break;   /* 下：列反序 */
            }
            line[j] = s_g[r][c];
        }

        if (slide_line(line, &gained)) changed = true;

        /*  按同样的顺序写回 */
        for (int j = 0; j < N; j++) {
            int r = 0, c = 0;
            switch (dir) {
            case 0: r = i; c = j;         break;
            case 1: r = i; c = N - 1 - j; break;
            case 2: r = j; c = i;         break;
            default:r = N - 1 - j; c = i; break;
            }
            s_g[r][c] = line[j];
        }
    }

    s_pts += gained;
    return changed;
}

static bool has_moves(void)
{
    for (int r = 0; r < N; r++)
        for (int c = 0; c < N; c++)
            if (s_g[r][c] == 0) return true;

    for (int r = 0; r < N; r++)
        for (int c = 0; c + 1 < N; c++)
            if (s_g[r][c] == s_g[r][c + 1]) return true;

    for (int c = 0; c < N; c++)
        for (int r = 0; r + 1 < N; r++)
            if (s_g[r][c] == s_g[r + 1][c]) return true;

    return false;
}

static void try_step(int dir)
{
    if (s_over) return;

    if (do_move(dir)) {
        spawn_tile();
        if (!has_moves()) {
            s_over = true;
            ESP_LOGI(TAG_G, "游戏结束得分 %u", (unsigned)s_pts);
        }
    } else {
        /* 没动就不生成新块  也不会判死  */
    }
    board_render();
}

/* ---------------- 触摸：判断滑动方向 ---------------- */
static void board_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED) {
        lv_indev_t *indev = lv_indev_active();
        if (indev) lv_indev_get_point(indev, &s_press_pt);

    } else if (code == LV_EVENT_RELEASED) {
        lv_indev_t *indev = lv_indev_active();
        if (indev == NULL) return;

        lv_point_t p;
        lv_indev_get_point(indev, &p);

        int dx = p.x - s_press_pt.x;
        int dy = p.y - s_press_pt.y;
        int adx = dx < 0 ? -dx : dx;
        int ady = dy < 0 ? -dy : dy;

        if (adx < 20 && ady < 20) return;          /* 太小当点击  不动  */

        if (adx > ady) try_step(dx > 0 ? 1 : 0);   /* 横向  */
        else           try_step(dy > 0 ? 3 : 2);   /* 纵向  */
    }
}

static void on_new_game(lv_event_t *e)
{
    (void)e;
    game2048_restart();
}

/* ---------------- 对外 ---------------- */
void game2048_restart(void)
{
    grid_clear();
    s_pts = 0;
    s_over = false;
    spawn_tile();
    spawn_tile();          /* 开局两块  */
    board_render();
}

void game2048_create(lv_obj_t *parent)
{
    if (parent == NULL) return;

    /* 顶栏：得分 + 新局按钮  */
    s_score = lv_label_create(parent);
    lv_obj_set_style_text_font(s_score, &lv_font_ZiTiQuanWeiJunHeiW22_18, 0);
    lv_obj_set_style_text_color(s_score, lv_color_white(), 0);
    lv_label_set_text(s_score, "得分 0");
    lv_obj_set_pos(s_score, 4, 2);

    s_btn_new = lv_button_create(parent);
    lv_obj_set_size(s_btn_new, 56, 24);
    lv_obj_align(s_btn_new, LV_ALIGN_TOP_RIGHT, -4, 0);
    lv_obj_set_style_bg_color(s_btn_new, lv_color_black(), 0);
    lv_obj_set_style_border_width(s_btn_new, 1, 0);
    lv_obj_set_style_border_color(s_btn_new, lv_color_white(), 0);
    lv_obj_set_style_radius(s_btn_new, 6, 0);
    {
        lv_obj_t *l = lv_label_create(s_btn_new);
        lv_obj_set_style_text_font(l, &lv_font_ZiTiQuanWeiJunHeiW22_18, 0);
        lv_obj_set_style_text_color(l, lv_color_white(), 0);
        lv_label_set_text(l, "新局");
        lv_obj_center(l);
    }
    lv_obj_add_event_cb(s_btn_new, on_new_game, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(s_btn_new, LV_OBJ_FLAG_HIDDEN);

    /* 棋盘  */
    s_board = lv_obj_create(parent);
    lv_obj_remove_style_all(s_board);
    lv_obj_set_size(s_board, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_top(s_board, 28, 0);      /* 给顶栏留位置  */
    lv_obj_set_style_bg_opa(s_board, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(s_board, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_board, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_board, board_event_cb, LV_EVENT_PRESSED,  NULL);
    lv_obj_add_event_cb(s_board, board_event_cb, LV_EVENT_RELEASED, NULL);

    /* 16 个格子  每个里面放一个 label  */
    for (int r = 0; r < N; r++) {
        for (int c = 0; c < N; c++) {
            lv_obj_t *cell = lv_obj_create(s_board);
            lv_obj_remove_style_all(cell);
            lv_obj_set_style_radius(cell, 4, 0);
            lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_remove_flag(cell, LV_OBJ_FLAG_CLICKABLE);   /* 点击交给棋盘  */

            lv_obj_t *lab = lv_label_create(cell);
            lv_obj_set_style_text_font(lab, &lv_font_ZiTiQuanWeiJunHeiW22_18, 0);
            lv_obj_set_style_text_color(lab, lv_color_white(), 0);
            lv_label_set_text(lab, "");
            lv_obj_center(lab);

            s_cells[r][c] = cell;
        }
    }

    game2048_restart();
    ESP_LOGI(TAG_G, "2048 已就绪  滑动操作 ");
}
