#include "light.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "led_ws2812.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <math.h>

static ws2812_strip_handle_t s_ws2812_handle = NULL;
static EventGroupHandle_t   light_event = NULL;
#define ATMOSPHERE_BIT  BIT0

#define MAX_RGB_NUM     3

static void light_task(void* param);

void light_init(void)
{
    //初始化一个定时器
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,      //低速模式
        .timer_num        = LEDC_TIMER_0,     //定时器ID
        .duty_resolution  = LEDC_TIMER_12_BIT,  //占空比分辨率，这里最大值是2^12-1
        .freq_hz          = 5000,  // PWM频率,这里是5KHZ
        .clk_cfg          = LEDC_AUTO_CLK    // 时钟
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    //ledc通道初始化
    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_LOW_SPEED_MODE,        //低速模式
        .channel        = LEDC_CHANNEL_0,     //PWM 通道0-7
        .timer_sel      = LEDC_TIMER_0,       //关联定时器，也就是上面初始化好的那个定时器
        .gpio_num       = GPIO_NUM_15,   //设置输出PWM方波的GPIO管脚
        .duty           = 0, // 设置默认占空比为0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    //开启硬件PWM
    ledc_fade_func_install(0);

    //初始化WS2812
    ws2812_init(GPIO_NUM_18,MAX_RGB_NUM,&s_ws2812_handle);
    light_event = xEventGroupCreate();
    xTaskCreatePinnedToCore(light_task,"light",4096,NULL,3,NULL,1);

}

//开某个ws2812
void light_ws2812_on(int index,uint8_t r,uint8_t g,uint8_t b)
{
    ws2812_write(s_ws2812_handle,index,r,g,b);
}

//控制ws2812亮度
void light_ws2812_set_brightness(int index,uint8_t brightness)
{
    ws2812_set_brightness(s_ws2812_handle,index,brightness);
}

//控制ws2812亮度
uint8_t light_ws2812_get_brightness(int index)
{
    return ws2812_get_brightness(s_ws2812_handle,index)&0xff;
}

//控制led亮度
void light_led_on(uint8_t brightness)
{
    uint32_t duty = 4095ul*brightness/100;
    ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0,duty,0);
}

//启动氛围模式
void light_start_atmosphere(void)
{
    xEventGroupSetBits(light_event,ATMOSPHERE_BIT);
}

//停止氛围模式
void light_stop_atmosphere(void)
{
    xEventGroupClearBits(light_event,ATMOSPHERE_BIT);
}

// 将 HSV 颜色转换为 RGB，并映射到 0~100 范围
void hsv_to_rgb_100(float h, float s, float v, int *r, int *g, int *b) {
    // h: 0-360, s: 0-1, v: 0-1
    float c = v * s;
    float x = c * (1 - fabsf(fmodf(h / 60.0f, 2) - 1));
    float m = v - c;
    float rp, gp, bp;

    if (h < 60) {
        rp = c; gp = x; bp = 0;
    } else if (h < 120) {
        rp = x; gp = c; bp = 0;
    } else if (h < 180) {
        rp = 0; gp = c; bp = x;
    } else if (h < 240) {
        rp = 0; gp = x; bp = c;
    } else if (h < 300) {
        rp = x; gp = 0; bp = c;
    } else {
        rp = c; gp = 0; bp = x;
    }

    *r = (int)((rp + m) * 100);
    *g = (int)((gp + m) * 100);
    *b = (int)((bp + m) * 100);

    // 限制在 0~100
    *r = (*r < 0) ? 0 : ((*r > 100) ? 100 : *r);
    *g = (*g < 0) ? 0 : ((*g > 100) ? 100 : *g);
    *b = (*b < 0) ? 0 : ((*b > 100) ? 100 : *b);
}


static void light_task(void* param)
{
    EventBits_t ev;
    float hue = 0.0f;         // 色相，0~360
    float brightness_base = 0.7f; // 基础亮度
    float pulse_amplitude = 0.3f; // 脉动幅度
    float time = 0.0f;
    while(1)
    {
        ev = xEventGroupWaitBits(light_event,ATMOSPHERE_BIT,pdFALSE,pdFALSE,portMAX_DELAY);
        if(ev&ATMOSPHERE_BIT)
        {
             // 每帧更新时间
            time += 0.02f; // 控制速度

            // 色相缓慢变化，形成彩虹效果
            hue = fmodf(time * 30.0f, 360.0f); // 每秒约30度变化

            // 亮度脉动：使用正弦波模拟呼吸/闪耀效果
            float brightness = brightness_base + pulse_amplitude * sinf(time * 2.0f); // 脉动频率

            // 转换 HSV 到 RGB (0~100)
            int r, g, b;
            hsv_to_rgb_100(hue, 1.0f, brightness, &r, &g, &b);
            // 写入 LED
            for(int i = 0;i < MAX_RGB_NUM;i++)
                ws2812_write(s_ws2812_handle,i,r, g, b);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
