/* ==========================================================================
 *  light.h  板载灯统一接口（LED2 + WS2812）
 *
 *   为什么要封装：
 *      WS2812 是 RMT 外设  LED2 是 LEDC(PWM)  两套完全不同的驱动 
 *      上层（规则引擎/语音/UI）不该关心这些细节 
 *       只暴露 "设个颜色 / 设个亮度" 这种语义 
 * ========================================================================== */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化：LED2(LEDC) + WS2812(RMT)  在 init_all() 里调一次  */
esp_err_t light_init(void);

/* ---- LED2：板载蓝色小灯（IO15PWM 可调光 ）---- */
esp_err_t led2_set(uint8_t percent);        /* 0~100  0=灭  */
esp_err_t led2_off(void);

/* ---- WS2812：3 颗 RGB（IO18）---- */
esp_err_t ws2812_set_all(uint8_t r, uint8_t g, uint8_t b);
esp_err_t ws2812_set_one(int idx, uint8_t r, uint8_t g, uint8_t b);
esp_err_t ws2812_off(void);

/* ---- 现成的效果（规则引擎/语音直接用 ）---- */
void light_effect_breath(void);             /* 呼吸灯：在 light_task 里调  */
void light_effect_alarm(bool on);
esp_err_t light_cloud_set(bool on);                       /* 开/关  */
esp_err_t light_cloud_set_brightness(int pct);            /* 亮度 0~100  */
esp_err_t light_cloud_set_rgb(uint8_t r, uint8_t g, uint8_t b);  /* RGB  */
esp_err_t ws2812_set_all(uint8_t r, uint8_t g, uint8_t b);
esp_err_t ws2812_off(void);   /*  云端开关灯（常亮不被报警覆盖 ）*/           /* 报警：红色慢闪 / 关  */
void light_task(void *param);               /* 可选：需要动画就起这个任务  */

#ifdef __cplusplus
}
#endif