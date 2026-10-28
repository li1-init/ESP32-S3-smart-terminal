#ifndef _LIGHT_H_
#define _LIGHT_H_
#include <stdint.h>

//灯光初始化
void light_init(void);

//开某个ws2812
void light_ws2812_on(int index,uint8_t r,uint8_t g,uint8_t b);

//控制ws2812亮度
void light_ws2812_set_brightness(int index,uint8_t brightness);

//控制ws2812亮度
uint8_t light_ws2812_get_brightness(int index);

//控制led亮度
void light_led_on(uint8_t brightness);

//启动氛围模式
void light_start_atmosphere(void);

//停止氛围模式
void light_stop_atmosphere(void);

#endif