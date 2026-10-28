#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_
#include "driver/gpio.h"
#include "xl9555.h"

/* =====================================================================
 *  本文件集中存放「你这块开发板」的所有引脚定义。
 *  以后换板/改线只改这一个文件即可。
 * ===================================================================== */

/* ---------------- XL9555 (I2C 扩展 IO) ---------------- */
#define BSP_XL9555_SDA      GPIO_NUM_10
#define BSP_XL9555_SCL      GPIO_NUM_11

/* XL9555 上挂的外设控制脚 */
#define BSP_LCD_RST_IO      IO1_3   /* LCD 复位 */
#define BSP_LCD_BL_IO       IO1_2   /* LCD 背光 */
#define BSP_TP_RST_IO       IO1_0   /* 触摸复位 */
#define BSP_SPK_EN_IO       IO0_0   /* 功放使能 */

/* ---------------- ST7789 LCD (I80 8bit 并口) ---------------- */
#define BSP_LCD_WIDTH       320
#define BSP_LCD_HEIGHT      240
#define BSP_LCD_CS          GPIO_NUM_2
#define BSP_LCD_RS          GPIO_NUM_1      /* 命令/数据 */
#define BSP_LCD_WR          GPIO_NUM_41     /* 写使能 */
#define BSP_LCD_DATA_PINS   { GPIO_NUM_40, GPIO_NUM_38, GPIO_NUM_39, GPIO_NUM_48, \
                              GPIO_NUM_45, GPIO_NUM_21, GPIO_NUM_47, GPIO_NUM_14 }
#define BSP_LCD_BUS_WIDTH   8

/* ---------------- FT6336U 触摸 (I2C) ---------------- */
#define BSP_TP_SCL          GPIO_NUM_12
#define BSP_TP_SDA          GPIO_NUM_13
#define BSP_TP_FREQ_HZ      (400 * 1000)

/* ---------------- WS2812 RGB 灯 ---------------- */
#define BSP_WS2812_GPIO     GPIO_NUM_18
#define BSP_WS2812_NUM      3

/* ---------------- 板载普通 LED (PWM) ---------------- */
#define BSP_LED_GPIO        GPIO_NUM_15

#endif