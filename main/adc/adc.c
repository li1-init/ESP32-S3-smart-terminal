/* ==========================================================================
 *  ADC 驱动实现
 *   三个关键点（NOTES 里的学习点 ）
 *      必须【校准】adc_cali  不校准数值严重非线性 
 *      必须【平均滤波】 连采 N 次取平均去毛刺 
 *      输入最高 3.3V  模块 VCC 只能接 3.3V 
 * ========================================================================== */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/semphr.h"
#include "adc.h"
#include "freertos/timers.h"
#include "task_message.h"
#include "light.h"
#include "wdt.h"

static TimerHandle_t adc_timer = NULL;

/* ============ 标定常数（想校准就改这 6 个数字 ）============
   热敏实测：1339 mV ≈ 室温 25℃   1527 mV ≈ 手心 34℃
        -> 两点线性插值 （25~34℃ 这段 NTC 近似线性 ）
         想更准：拿真温度计测 3 个点（冰水/室温/温水）发我  我拟合 Beta 曲线 
   光敏实测：全暗 raw≈4095（电压高 ）  手电筒直照 raw≈39（电压低 ）
         【越亮电压越低】 所以算百分比要反过来  */
#define TEMP_MV_COLD 1339
/*  2026-10-27 修正：它显示 30.4℃ 而你这里真实只有 17~20℃ 
   反推：30.4 = 20 + (mV-1339)13/188    mV ≈ 1489
          室温 18.5℃ 时它的电压是 1489 mV 
    说明【整条曲线偏高了约 12℃】
   临时先整体下移 12℃ （让读数合理 ）
   ⚠️ 想真正准  手心捏 2 分钟给我 mV  我按两点重算  */
#define TEMP_C_COLD 8.0f
#define TEMP_MV_HOT 1527
#define TEMP_C_HOT 21.0f
#define LIGHT_RAW_DARK 4095
#define LIGHT_RAW_BRIGHT 39

#define TAG_ADC "adc"

#define ADC_RAW_TEMP ADC_CHANNEL_7  /* GPIO8  热敏 */
#define ADC_RAW_LIGHT ADC_CHANNEL_8 /* GPIO9  光敏 */
#define ADC_ATTEN ADC_ATTEN_DB_12   /* 量程 0 ~ 3.1V  */
#define ADC_AVG_N 16                /* 平均滤波次数  */

static adc_oneshot_unit_handle_t s_adc = NULL;
static adc_cali_handle_t s_cali = NULL;
/*  ADC 单元【不是可重入的】
   现在有两个任务在读它： adc_task（每2秒打印） onenetpost_task（上报）
    不加锁可能读到错值/报错   加个互斥锁 （FreeRTOS 的 Mutex 自带优先级继承 ）*/
static SemaphoreHandle_t s_adc_mux = NULL;

static adc_channel_t to_raw_ch(int ch)
{
    return (ch == ADC_CH_LIGHT) ? ADC_RAW_LIGHT : ADC_RAW_TEMP;
}

int adc_read_raw(int ch)
{
    int raw = 0;
    if (s_adc == NULL)
        return -1;
    if (s_adc_mux && xSemaphoreTake(s_adc_mux, pdMS_TO_TICKS(100)) != pdTRUE)
        return -1;
    esp_err_t e = adc_oneshot_read(s_adc, to_raw_ch(ch), &raw);
    if (s_adc_mux)
        xSemaphoreGive(s_adc_mux);
    return (e == ESP_OK) ? raw : -1;
}

int adc_read_mv(int ch)
{
    int raw = 0, mv = 0;
    if (s_adc == NULL)
        return -1;
    if (s_adc_mux && xSemaphoreTake(s_adc_mux, pdMS_TO_TICKS(100)) != pdTRUE)
        return -1;
    esp_err_t e = adc_oneshot_read(s_adc, to_raw_ch(ch), &raw);
    if (e != ESP_OK)
    {
        if (s_adc_mux)
            xSemaphoreGive(s_adc_mux);
        return -1;
    }
    int out = -1;
    if (s_cali != NULL && adc_cali_raw_to_voltage(s_cali, raw, &mv) == ESP_OK)
        out = mv;
    else
        out = (int)((long)raw * 3100 / 4095);
    if (s_adc_mux)
        xSemaphoreGive(s_adc_mux);
    return out; /* 没校准只能粗算  不推荐  */
}

/*  两级滤波（实测有效 ）
   第 1 级：连采 N 次取平均    压掉【随机噪声】
   第 2 级：一阶低通 EMA        压掉【漂移/跳动】
       y = y + (x - y)  K
       新值只占 K旧值占 (1-K)  输出变化平缓 
       K 越小越平滑但越迟钝 ：0.2 = 大约 5 次采样跟上一个台阶 

    两个通道各有一份状态  所以【所有调用者看到的都是同一条平滑曲线】
     （打印任务和上报任务不会再各算各的  不会再出现"同一 raw 两个温度"）*/
#define ADC_EMA_K 0.2f
static float s_mv_ema[2] = {-1.0f, -1.0f}; /* [0]=温度 [1]=光照 */

/* 第 1 级：平均 */
static int adc_avg_raw(int ch)
{
    long sum = 0;
    int n = 0;
    for (int i = 0; i < ADC_AVG_N; i++)
    {
        int mv = adc_read_mv(ch);
        if (mv >= 0)
        {
            sum += mv;
            n++;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    return n ? (int)(sum / n) : -1;
}

/* 第 2 级：EMA（ 只对温度做  光照要保持反应快  遮一下手就要立刻变 ）*/
static int adc_avg_mv(int ch)
{
    int avg = adc_avg_raw(ch);
    if (avg < 0)
        return -1;

    if (ch != ADC_CH_TEMP)
        return avg; /* 光照：不做 EMA  保持灵敏  */

    if (s_mv_ema[0] < 0)
        s_mv_ema[0] = (float)avg; /* 第一次：直接采信  */
    else
        s_mv_ema[0] += ((float)avg - s_mv_ema[0]) * ADC_EMA_K;
    return (int)(s_mv_ema[0] + 0.5f);
}

/*  温度：两点线性插值  */
/*  对外只暴露【一个只读函数】 不暴露内部变量 
   为什么这么做（封装）：
      adc_temp_abnormal_state_last 是 static   别的文件本来就够不着 
      如果把它改成全局变量让谁都改   谁都能乱改状态  逻辑就烂了 
      包一个函数：名字说清是"读" 实现只有一行 return 
        内部怎么变外面都不用改 （这就是"接口稳定"）*/
bool adc_temp_alarm(void)
{
    return adc_temp_abnormal_state_last;
}

float adc_temp_c(void)
{
    int mv = adc_avg_mv(ADC_CH_TEMP);
    if (mv < 0)
        return -100.0f;
    return TEMP_C_COLD + (float)(mv - TEMP_MV_COLD) * (TEMP_C_HOT - TEMP_C_COLD) / (float)(TEMP_MV_HOT - TEMP_MV_COLD);
}

/*  光照 0~100%：注意越亮电压越低  要反过来算  */
int adc_light_pct(void)
{
    int raw = adc_read_raw(ADC_CH_LIGHT);
    if (raw < 0)
        return -1;
    int span = LIGHT_RAW_DARK - LIGHT_RAW_BRIGHT;
    if (span <= 0)
        return 0;
    int pct = (LIGHT_RAW_DARK - raw) * 100 / span;
    if (pct < 0)
        pct = 0;
    if (pct > 100)
        pct = 100;
    return pct;
}

volatile bool temp_abnormal_state = false;
volatile bool light_abnormal_state = false;

bool get_temp_abnormal_state(void)
{
    return temp_abnormal_state;
}

bool get_light_abnormal_state(void)
{
    return light_abnormal_state;
}

bool set_temp_abnormal_state(bool state)
{
    temp_abnormal_state = state;
    return temp_abnormal_state;
}

bool set_light_abnormal_state(bool state)
{
    light_abnormal_state = state;
    return light_abnormal_state;
}
#define TEMP_C_MIN 28.0f
#define TEMP_C_MAX 30.0f
#define LIGHT_MIN 10
#define LIGHT_MAX 70

void adc_timer_callback(TimerHandle_t xTimer)
{
    (void)xTimer;
    if (!(get_temp_abnormal_state() && get_light_abnormal_state()))
        return;
    if (get_temp_abnormal_state())
    {
        msg_post(msg_src_system, "温度异常");
    }
    if (get_light_abnormal_state())
    {
        msg_post(msg_src_system, "光照异常");
    }
}

bool adc_temp_abnormal_state_last = false;
bool adc_light_abnormal_state_last = false;

bool get_adc_temp_abnormal_state(void)
{
    return adc_temp_abnormal_state_last;
}
bool get_adc_light_abnormal_state(void)
{
    return adc_light_abnormal_state_last;
}
void adc_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(1500)); /* 等系统起来  */
    ESP_LOGI(TAG_ADC, "================ ADC 打印任务开始 ================");
    ESP_LOGI(TAG_ADC, "要看到的效果：遮住光敏  光敏数字变；捏热敏  热敏数字变");
    ESP_LOGI(TAG_ADC, "如果数字纹丝不动  多半是没接好 或 VCC 没接 3.3V");
    while (1)
    {
        keep_living(HB_ADC);   /*  心跳：我活着 （周期 2 秒  < 窗口 15 秒 ）*/
        int rt = adc_read_raw(ADC_CH_TEMP);
        int rl = adc_read_raw(ADC_CH_LIGHT);
        int mt = adc_avg_mv(ADC_CH_TEMP);
        int ml = adc_avg_mv(ADC_CH_LIGHT);

        float temp_c = adc_temp_c();
        int light_c = adc_light_pct();

        bool adc_temp_abnormal_state_now = adc_temp_abnormal_state_last;   /*  从上次状态出发  */
        if (temp_c >= TEMP_C_MAX)
        {
            adc_temp_abnormal_state_now = true;
        }
        else if (temp_c <= TEMP_C_MIN)
        {
            adc_temp_abnormal_state_now = false;
        }
        if (adc_temp_abnormal_state_now != adc_temp_abnormal_state_last)
        {
            adc_temp_abnormal_state_last = adc_temp_abnormal_state_now;
            if (adc_temp_abnormal_state_now)
            {
                msg_post(msg_src_alarm, "温度过高！");
                /*  你要的：三颗一起【亮↔灭】闪烁 （不是常亮 ）
                   light_effect_alarm(true) 会让动画任务每 600ms 翻转一次  */
                light_effect_alarm(true);
            }
            else
            {
                msg_post(msg_src_alarm, "温度已恢复正常");
                light_effect_alarm(false);     /* 灭灯  */
            }
        }
        /*  光照告警 = 【太暗】才报 （光照高不是坏事  不报 ）
              太暗  提醒开灯 / 护眼 
           ⚠️ 迟滞写法同上：先继承上次状态  再从上次状态出发  */
        bool adc_light_abnormal_state_now = adc_light_abnormal_state_last;
        if (light_c <= LIGHT_MIN)              /*  暗到阈值  报警  */
        {
            adc_light_abnormal_state_now = true;
        }
        else if (light_c >= LIGHT_MIN + 15)    /*  亮回来 15% 才恢复 （迟滞带 ）*/
        {
            adc_light_abnormal_state_now = false;
        }
        if (adc_light_abnormal_state_now != adc_light_abnormal_state_last)
        {
            adc_light_abnormal_state_last = adc_light_abnormal_state_now;
            if (adc_light_abnormal_state_now)
            {
                msg_post(msg_src_alarm, "光线太暗了 注意用眼 ");
                led2_set(100);                 /*  光照过低  板载 LED2 亮  */
            }
            else
            {
                msg_post(msg_src_alarm, "光线已恢复");
                led2_off();                    /*  光照恢复（够亮） LED2 灭  */
            }
        }
        ESP_LOGI(TAG_ADC, "温度=%.1f C   光照=%3d%%    (热敏 raw=%4d %4dmV | 光敏 raw=%4d %4dmV)",
                 temp_c, light_c, rt, mt, rl, ml);
        vTaskDelay(pdMS_TO_TICKS(2000)); /* 每 2 秒一行  */
    }
}

void adc_init(void)
{
    /*  ADC1 单元 */
    adc_oneshot_unit_init_cfg_t unit_cfg = {.unit_id = ADC_UNIT_1};
    if (adc_oneshot_new_unit(&unit_cfg, &s_adc) != ESP_OK)
    {
        ESP_LOGE(TAG_ADC, "ADC 建单元失败 ");
        s_adc = NULL;
        return;
    }

    s_adc_mux = xSemaphoreCreateMutex(); /*  先建锁  再起任务  */

    adc_oneshot_chan_cfg_t chan_cfg = {.atten = ADC_ATTEN, .bitwidth = ADC_BITWIDTH_DEFAULT};
    adc_oneshot_config_channel(s_adc, ADC_RAW_TEMP, &chan_cfg);
    adc_oneshot_config_channel(s_adc, ADC_RAW_LIGHT, &chan_cfg);

    /*  校准（ 不做这步数值会严重偏 ）*/
    adc_cali_curve_fitting_config_t cali_cfg =
        {
            .unit_id = ADC_UNIT_1,
            .atten = ADC_ATTEN,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali) != ESP_OK)
    {
        ESP_LOGW(TAG_ADC, "校准不可用  只能粗算（不准 ）");
        s_cali = NULL;
    }

    ESP_LOGI(TAG_ADC, "ADC 就绪：热敏=IO8(CH7)  光敏=IO9(CH8)  平均%d次 校准=%s",
             ADC_AVG_N, s_cali ? "有 " : "无 ");
}