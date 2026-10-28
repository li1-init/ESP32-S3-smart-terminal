/* ==========================================================================
 *  light.c  板载灯统一实现
 *
 *  器件与驱动对应关系：
 *    LED2   IO15    LEDC(LEDC_TIMER_0/CH0, 10bit, 5kHz)  可以调亮度 
 *    WS2812 IO18 3  RMT(由 led_ws2812.c 负责  那是从参考工程搬来的 )
 *
 *   为什么两套驱动：它们物理原理不同 
 *      LED2 是普通 LED  用 PWM 调占空比调亮度 
 *      WS2812 是"单线串行协议" 靠精确时序的高低电平编码 0/1 
 *           必须用 RMT 外设（硬件负责发时序  不受任务调度影响 ）
 *
 *   为什么这里再包一层：上层只关心"设颜色/设亮度"
 *      不该知道 RMT/LEDC 这些细节 （以后换灯珠也不用改上层 ）
 * ========================================================================== */
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/ledc.h"

#include "board_config.h"     /* BSP_LED_GPIO / BSP_WS2812_GPIO / BSP_WS2812_NUM */
#include "led_ws2812.h"       /* 参考工程搬来的：ws2812_init / ws2812_write  */
#include "light.h"

#define TAG_LIGHT "light"

/* ---------------- LED2 的 LEDC 参数 ---------------- */
#define LED2_MODE       LEDC_LOW_SPEED_MODE
#define LED2_TIMER      LEDC_TIMER_0
#define LED2_CHANNEL    LEDC_CHANNEL_0
#define LED2_RES        LEDC_TIMER_10_BIT     /* 0 ~ 1023  */
#define LED2_MAX_DUTY   1023
#define LED2_PWM_HZ     5000                  /* 5kHz：够快人眼看不到闪  */

/* ---------------- 运行状态 ---------------- */
typedef enum {
    LIGHT_MODE_IDLE = 0,
    LIGHT_MODE_BREATH,        /* 呼吸灯  */
    LIGHT_MODE_ALARM,         /* 红色慢闪（报警） */
} light_mode_t;

static ws2812_strip_handle_t s_strip  = NULL;
static bool                  s_inited = false;
static volatile light_mode_t s_mode   = LIGHT_MODE_IDLE;
/*  云端开关优先标志：为 true 时【阻止报警逻辑把灯关掉】
   原因：adc.c 每 600ms 会调 light_effect_alarm(false) 灭灯 
         你在云端点亮的灯会被立刻灭掉 （就是"没反应"的真正原因）*/
static volatile bool s_cloud_on = false;
static TaskHandle_t          s_task   = NULL;

/* ==========================================================================
 *  初始化
 * ========================================================================== */
static void light_anim_task(void *param);
static void ensure_task(void);        /*  前置声明：效果函数要先能用它  */

esp_err_t light_init(void)
{
    if (s_inited) return ESP_OK;

    /* ---------------- LED2：LEDC ---------------- */
    ledc_timer_config_t tcfg = {
        .speed_mode      = LED2_MODE,
        .duty_resolution = LED2_RES,
        .timer_num       = LED2_TIMER,
        .freq_hz         = LED2_PWM_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    esp_err_t ret = ledc_timer_config(&tcfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_LIGHT, "LEDC 定时器配置失败 0x%x ", ret);
        return ret;
    }

    ledc_channel_config_t ccfg = {
        .gpio_num   = BSP_LED_GPIO,
        .speed_mode = LED2_MODE,
        .channel    = LED2_CHANNEL,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = LED2_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ret = ledc_channel_config(&ccfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_LIGHT, "LEDC 通道配置失败 0x%x ", ret);
        return ret;
    }

    /* ---------------- WS2812：RMT ---------------- */
    ret = ws2812_init(BSP_WS2812_GPIO, BSP_WS2812_NUM, &s_strip);
    if (ret != ESP_OK) {
        /*  不 return 失败  LED2 还是能用的  灯珠坏了不该拖垮整机  */
        ESP_LOGW(TAG_LIGHT, "WS2812 初始化失败 0x%x （LED2 仍可用 ）", ret);
        s_strip = NULL;
    } else {
        ws2812_off();
    }

    s_inited = true;
    ESP_LOGI(TAG_LIGHT, "灯模块就绪  LED2=IO%d(PWM)  WS2812=IO%d%d",
             (int)BSP_LED_GPIO, (int)BSP_WS2812_GPIO, BSP_WS2812_NUM);
    return ESP_OK;
}

/* ==========================================================================
 *  LED2
 * ========================================================================== */
esp_err_t led2_set(uint8_t percent)
{
    if (percent > 100) percent = 100;
    uint32_t duty = (uint32_t)percent * LED2_MAX_DUTY / 100;

    esp_err_t ret = ledc_set_duty(LED2_MODE, LED2_CHANNEL, duty);
    if (ret == ESP_OK) ret = ledc_update_duty(LED2_MODE, LED2_CHANNEL);
    return ret;
}

esp_err_t led2_off(void)
{
    return led2_set(0);
}

/* ==========================================================================
 *  WS2812
 * ========================================================================== */
esp_err_t ws2812_set_all(uint8_t r, uint8_t g, uint8_t b)
{
    if (s_strip == NULL) return ESP_ERR_INVALID_STATE;
    for (int i = 0; i < BSP_WS2812_NUM; i++) {
        esp_err_t ret = ws2812_write(s_strip, i, r, g, b);
        if (ret != ESP_OK) return ret;
    }
    return ESP_OK;
}

esp_err_t ws2812_set_one(int idx, uint8_t r, uint8_t g, uint8_t b)
{
    if (s_strip == NULL) return ESP_ERR_INVALID_STATE;
    if (idx < 0 || idx >= BSP_WS2812_NUM) return ESP_ERR_INVALID_ARG;
    return ws2812_write(s_strip, idx, r, g, b);
}

esp_err_t ws2812_off(void)
{
    return ws2812_set_all(0, 0, 0);
}

/* ==========================================================================
 *  现成效果（规则引擎 / 语音直接用 ）
 * ========================================================================== */
void light_effect_breath(void)
{
    ensure_task();                    /*  第一次用效果就自动起任务  */
    s_mode = LIGHT_MODE_BREATH;
}

/*  云端开关灯：常亮（不走闪烁任务） 优先级最高  */
esp_err_t light_cloud_set(bool on)
{
    if (!s_inited) return ESP_ERR_INVALID_STATE;

    s_cloud_on = on;
    s_mode     = LIGHT_MODE_IDLE;      /* 停掉动画任务  */

    if (on) {
        ws2812_set_all(255, 0, 0);     /*  常亮红 （想换颜色改这三个数 ）*/
        ESP_LOGI(TAG_LIGHT, "云端：开灯（常亮红）");
    } else {
        ws2812_off();
        ESP_LOGI(TAG_LIGHT, "云端：关灯");
    }
    return ESP_OK;
}

/*  云端亮度（0~100） 保存后重画当前颜色  */
static int s_cloud_br   = 100;
static int s_cloud_r    = 255;
static int s_cloud_g    = 0;
static int s_cloud_b    = 0;
static bool s_cloud_lit = false;

static void cloud_apply(void)
{
    if (!s_inited) return;
    if (!s_cloud_lit) { ws2812_off(); return; }
    int k = s_cloud_br;
    ws2812_set_all((uint8_t)(s_cloud_r * k / 100),
                   (uint8_t)(s_cloud_g * k / 100),
                   (uint8_t)(s_cloud_b * k / 100));
}

esp_err_t light_cloud_set_brightness(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    s_cloud_br = pct;
    cloud_apply();
    return ESP_OK;
}

esp_err_t light_cloud_set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    s_cloud_r = r; s_cloud_g = g; s_cloud_b = b;
    s_cloud_lit = true;
    s_cloud_on  = true;
    s_mode      = LIGHT_MODE_IDLE;
    cloud_apply();
    return ESP_OK;
}

void light_effect_alarm(bool on)
{
    if (on) ensure_task();            /*  只有要亮才起任务  */
    s_mode = on ? LIGHT_MODE_ALARM : LIGHT_MODE_IDLE;
    if (!on) {
        /*  云端已开灯  不允许报警逻辑把它关掉  */
    if (!on && s_cloud_on) return;

    /*  只关 WS2812 【不要】碰 LED2 
           LED2 归"光照太低"那条规则管 
           这里要是也关 LED2两条规则会互相打架  */
        ws2812_off();
    }
}

/* ==========================================================================
 *  动画任务（50ms 一帧  优先级较低不抢 LVGL ）
 * ========================================================================== */
#define ANIM_PERIOD_MS 50

static void light_anim_task(void *param)
{
    (void)param;
    uint32_t frame = 0;

    while (1) {
        switch (s_mode) {
        case LIGHT_MODE_BREATH: {
            /* 三角波 01000周期约 2 秒（40 帧） */
            uint32_t p = frame % 40;
            uint8_t  v = (p < 20) ? (uint8_t)(p * 5) : (uint8_t)((40 - p) * 5);
            led2_set(v);
            ws2812_set_all(0, 0, v);
            frame++;
            break;
        }
        case LIGHT_MODE_ALARM: {
            /* 每 12 帧（600ms）翻转一次  慢闪  */
            bool on = ((frame / 12) % 2) == 0;
            ws2812_set_all(on ? 60 : 0, 0, 0);   /* 红色  亮度压到 60 不刺眼  */
            frame++;
            break;
        }
        case LIGHT_MODE_IDLE:
        default:
            frame = 0;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(ANIM_PERIOD_MS));
    }
}

/* 内部用：确保动画任务在跑（第一次调效果时自动起 ）*/
static void ensure_task(void)
{
    if (s_task != NULL) return;
    if (xTaskCreate(light_anim_task, "light_anim", 3072, NULL, 3, &s_task) != pdPASS) {
        ESP_LOGE(TAG_LIGHT, "动画任务创建失败 ");
        s_task = NULL;
    }
}
