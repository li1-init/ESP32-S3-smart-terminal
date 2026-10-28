/* ==========================================================================
 *  wdt.c  心跳计数看门狗（软件级）
 *
 *  一句话原理：
 *      每个长驻任务每轮 keep_living(x)  g_hb[x]++
 *      另一个 esp_timer 每 5 秒看一眼：谁的计数没变就给谁记一次"没动"
 *      连续 3 次（= 15 秒）都没动  认定卡死  打日志  重启 
 *
 *   为什么计数器要 volatile
 *      编译器看不到"别的任务也在读它"  可能把值缓存在寄存器里 
 *       检查者永远读到旧值   加 volatile 禁止这种优化 
 *
 *   黑匣子（RTC 内存）
 *      普通全局变量重启就清零 
 *      RTC_NOINIT_ATTR 修饰的变量【软件重启/panic 都保留】（只有断电才丢 ）
 *       重启后能告诉你"上次跑到哪一步" 
 * ========================================================================== */
#include <stdio.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h" /* esp_restart / esp_reset_reason  */
#include "esp_attr.h"   /* RTC_NOINIT_ATTR  */

#include "wdt.h"

static const char *TAG = "wdt";

/* ---------------- 心跳计数表（每个任务一格 ）---------------- */
volatile uint32_t g_hb[HB_MAX] = {0};

/* ---------------- 检查者的内部状态 ---------------- */
static uint32_t s_last[HB_MAX] = {0}; /* 上一轮读到的心跳值  */
static uint8_t s_miss[HB_MAX] = {0};  /* 连续没动的次数  */

static const char *s_name[HB_MAX] = {
    "lvgl", "message", "onenetpost", "schedule", "adc", "living"};   /*  6 个名字对应 HB_MAX=6  少一个就会打出 (null)/?  */

static esp_timer_handle_t s_timer = NULL;

/* ---------------- 黑匣子：RTC 内存 ---------------- */
#define WDT_RTC_MAGIC 0x57445431u /* "WDT1"  用来识别冷启动  魔术字标记用来校验 RTC 内存数据是否有效；断电上电后 RTC 内存是随机乱值靠魔术字区分。*/

RTC_NOINIT_ATTR static uint32_t s_rtc_magic; // RTC 内存存魔术字；软件重启不清除断电丢失。   把这个变量放到 **RTC 慢速内存**
                                             // ✅ **启动的时候bootloader / 内核不会自动清零、初始化这个变量 **
RTC_NOINIT_ATTR static uint32_t s_rtc_stage; ///* 上次走到哪一步  */ // 卡死时写入重启后读取。

RTC_NOINIT_ATTR static uint32_t s_rtc_boots; /* 累计重启次数  */

uint32_t wdt_get_last_stage(void)
{
    return (s_rtc_magic == WDT_RTC_MAGIC) ? s_rtc_stage : 0;
}

void wdt_mark_stage(uint32_t stage) // 用户业务代码调用标记当前运行阶段方便定位卡死前执行位置。
{
    s_rtc_stage = stage;
}

/* ==========================================================================
 *  重启前留证据 
 * ========================================================================== */
void save_reset_reason_and_restart(uint32_t stage)
{
    ESP_LOGE(TAG, " 准备重启：stage=%u", (unsigned)stage);

    s_rtc_magic = WDT_RTC_MAGIC;
    s_rtc_stage = stage;

    /*  给串口一点时间把上面这行吐完 
       否则重启太快你串口里【看不到原因】 */
    vTaskDelay(pdMS_TO_TICKS(200));

    esp_restart();
}

/* ==========================================================================
 *  检查回调：跑在 esp_timer 任务里  必须快  不能阻塞 
 * ========================================================================== */
static void hb_check_cb(void *arg)
{
    (void)arg;

    for (int i = 0; i < HB_MAX; i++)
    {
        if (g_hb[i] == s_last[i])
        {
            /* ---- 这一轮没动  ---- */
            s_miss[i]++;
            ESP_LOGW(TAG, "%s 没心跳 (%d/%d)", (i < (int)(sizeof(s_name)/sizeof(s_name[0])) && s_name[i]) ? s_name[i] : "?", s_miss[i], WDT_MISS_MAX);

            if (s_miss[i] >= WDT_MISS_MAX)
            {
                ESP_LOGE(TAG, "%s 判为卡死  重启 ", (i < (int)(sizeof(s_name)/sizeof(s_name[0])) && s_name[i]) ? s_name[i] : "?");
                save_reset_reason_and_restart(100 + (uint32_t)i);
            }
        }
        else
        {
            /* ---- 动过了  清零犯错计数  ---- */
            s_last[i] = g_hb[i];
            s_miss[i] = 0;
        }
    }
}

/* ==========================================================================
 *  初始化
 * ========================================================================== */
void wdt_init(void)
{
    /* ----------------  先报上次怎么死的  ---------------- */
    esp_reset_reason_t why = esp_reset_reason();

    if (s_rtc_magic != WDT_RTC_MAGIC)
    {
        /* 冷启动（断电/复位键） RTC 内容无效  */
        s_rtc_magic = WDT_RTC_MAGIC;
        s_rtc_stage = 0;
        s_rtc_boots = 0;
        ESP_LOGI(TAG, "冷启动  看门狗开始工作（窗口 %d 秒 ）",
                 WDT_CHECK_PERIOD_S * WDT_MISS_MAX);
    }
    else
    {
        s_rtc_boots++;
        ESP_LOGW(TAG, " 上次非冷启动：第 %u 次重启  停在 stage=%u  原因=%d",
                 (unsigned)s_rtc_boots, (unsigned)s_rtc_stage, (int)why);

        /*  reset reason 对照表（串口看到数字就知道是什么原因 ）*/
        const char *rs = "未知";
        switch (why)
        {
        case ESP_RST_POWERON:
            rs = "正常上电 ";
            break;
        case ESP_RST_SW:
            rs = "软件主动重启（OTA/我们的心跳）";
            break;
        case ESP_RST_PANIC:
            rs = "程序崩溃/异常 ";
            break;
        case ESP_RST_INT_WDT:
            rs = "中断看门狗 ";
            break;
        case ESP_RST_TASK_WDT:
            rs = "任务看门狗 ";
            break;
        case ESP_RST_WDT:
            rs = "看门狗 ";
            break;
        case ESP_RST_BROWNOUT:
            rs = " 电源电压不足 （电池版必查）";
            break;
        case ESP_RST_DEEPSLEEP:
            rs = "深睡唤醒 ";
            break;
        default:
            break;
        }
        ESP_LOGW(TAG, "     %s", rs);
    }

    /* ----------------  建检查定时器 ---------------- */
    if (s_timer != NULL)
        return;

    esp_timer_create_args_t a = {
        .callback = hb_check_cb,
        .name = "wdt_hb",
    };
    esp_err_t ret = esp_timer_create(&a, &s_timer);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "定时器创建失败 0x%x ", ret);
        s_timer = NULL;
        return;
    }
    esp_timer_start_periodic(s_timer, (uint64_t)WDT_CHECK_PERIOD_S * 1000ULL * 1000ULL);
    ESP_LOGI(TAG, "看门狗已启动  检查周期 %d 秒容错 %d 次（窗口 %d 秒）",
             WDT_CHECK_PERIOD_S, WDT_MISS_MAX, WDT_CHECK_PERIOD_S * WDT_MISS_MAX);
}

/* ==========================================================================
 *  任务里调用：喂一次心跳 （就一行 ++  开销可忽略 ）
 * ========================================================================== */
void keep_living(wdt_task_t task)
{
    if (task < HB_MAX)
    {
        g_hb[task]++;
    }
}
