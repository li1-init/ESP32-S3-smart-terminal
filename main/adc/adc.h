#ifndef _ADC_H_
#define _ADC_H_

/* ==========================================================================
 *  ADC 驱动（F1 传感器采集的骨架  先出电压  标定后再换算温度/光照 ）
 *
 *  接线（就 3 根  2 ）：
 *     热敏模块 AO ── 丝印 IO8   （ADC1_CH7 ）
 *     光敏模块 AO ── 丝印 IO9   （ADC1_CH8 ）
 *     两个 VCC    ── 3.3V  ⚠️⚠️ 千万别接 5V（ADC 最高只能 3.3V  会打坏 ）
 *     两个 GND    ── GND
 *     两个 DO     ── 不接
 *
 *   为什么用 IO8/IO9：
 *      ESP32-S3  ADC1 = GPIO1~10   能用
 *                ADC2 = GPIO11~20  WiFi 一开着就完全不能用 
 *      你板子上能引出的空闲脚里只有 IO8/IO9 属于 ADC1 
 *      （代价：喇叭模块已整体删除  见 NOTES.md 第 17 节）
 * ========================================================================== */

#define ADC_CH_TEMP     0       /* 逻辑通道 0 = 热敏 = IO8   */
#define ADC_CH_LIGHT    1       /* 逻辑通道 1 = 光敏 = IO9   */

void adc_init(void);                    /* 建 ADC1 + 校准 + 起打印任务  */

/* 读一次电压（mV） 已做 N 次平均滤波  失败返回 -1  */
int  adc_read_mv(int ch);
/* 读一次原始值（0~4095） 标定时一起看更清楚  */
int  adc_read_raw(int ch);
/*  换算后的物理量（标定常数在 adc.c 顶部 ）*/
float adc_temp_c(void);                 /* 温度 ℃  */
int   adc_light_pct(void);              /* 光照 0~100%  */
/*  只读接口：当前是否处于温度报警 （内部变量是 static  用函数读 ）*/
bool  adc_temp_alarm(void);

void adc_task(void *arg);

extern volatile bool temp_abnormal_state;
extern volatile bool light_abnormal_state;

bool get_temp_abnormal_state(void);

bool get_light_abnormal_state(void);

bool set_temp_abnormal_state(bool state);

bool set_light_abnormal_state(bool state);

extern bool adc_temp_abnormal_state_last;
extern bool adc_light_abnormal_state_last;

bool get_adc_temp_abnormal_state(void);

bool get_adc_light_abnormal_state(void);

#endif