#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "task_message.h"
#include "wdt.h"
#include "freertos/timers.h"
#include "task_schedule.h"
#include "task_begin.h"
#define TAG_TASK_MESSAGE "task_message_task"
#define RING_MAX 10 /* 环形缓冲格数 */

/* ==================== 内部状态（全部 static  外面看不见 ）====================
   队列句柄 = 钥匙  只能自己拿 ；别人要功能 -> 调 msg_post() （门铃 ）        */
static QueueHandle_t s_q = NULL;

/* ---- 环形缓冲：3 个变量各回答一个问题  ----
   s_write : 下一条【写哪一格】
   s_count : 现在【一共几条】   <- 光看 s_write 分不出"空"和"满"  必须靠它 
   （未读数不再单独存  -> 改成 msg_unread() 里【实时扫描】算  保证不会和数组不一致 ）*/
static task_message_t s_msgs[RING_MAX];
static int s_write = 0;
static int s_count = 0;

static TimerHandle_t timer = NULL;

/*  语音已删除（2026-10-25） 喇叭让给两个 ADC 脚了 
   voice_state* 三个函数 + 定时器里的播放一起去掉了 
   ⚠️ 去抖定时器本身【保留】 那是 P6 的学习成果 
      知识在 NOTES.md 第 15 节（FreeRTOS 软件定时器） */

/* ==================== 来源数字 -> 显示文字 ==================== */
const char *msg_src_name(uint8_t src)
{
    switch (src)
    {
    case msg_src_system:
        return "系统";
    case msg_src_cloud:
        return "云端";
    case msg_src_alarm:
        return "报警";
    case msg_src_schedule:
        return "日程";
    default:
        return "未知";
    }
}

/* ==================== 时间戳 -> "2026-10-23" ====================
   注意：返回的 buf 是函数内的 static  不可重入 （多任务同时调会互相覆盖  本工程只有消息相关用 ）*/
const char *msg_ts_to_time(uint32_t ts)
{
    static char buf[40]; /* 别太小：%04d 理论最大能到 28 字节  GCC 会报 format-truncation */

    if (ts < 1700000000) /* 2023-11 之前 = SNTP 还没同步  */
        return "--";

    time_t t = (time_t)ts;
    struct tm *lt = localtime(&t);
    if (lt == NULL)
        return "未知";

    /* 只要年月日 ；想带时分就改成 "%04d-%02d-%02d %02d:%02d"  */
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
             lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday);
    return buf;
}

/* ==================== 初始化：建队列 ==================== */
void task_message_createqueue(void)
{
    s_q = xQueueCreate(10, sizeof(task_message_t));
}

/* ==================== 投递：任何模块都能调  ====================
   投递方不需要知道谁在收、队列多深、收到后干嘛   这就是解耦  */
void msg_post(uint8_t src, const char *text)
{
    task_message_t msg =
        {
            .src = src,
            .ts = (uint32_t)time(NULL),
            .state_read = false, /*  显式写出来  别靠零初始化（C 虽然保证  但意图不清 ）*/
    };
    snprintf(msg.text, sizeof(msg.text), "%s", text);

    if (xQueueSend(s_q, &msg, 0) != pdTRUE) /* 超时传 0  回调里绝不阻塞  */
    {
        task_message_t dump;
        xQueueReceive(s_q, &dump, 0); /*  队列满：丢掉最旧的一条 */
        xQueueSend(s_q, &msg, 0);     /* 再试一次 */
        ESP_LOGW(TAG_TASK_MESSAGE, "队列满丢最旧重新发送");
    }
}

/* ==================== 对外查询 ==================== */
int msg_count(void) { return s_count; }
/*  未读数：实时扫描数组算出来  不单独存变量 
   为什么不存变量？因为环形缓冲会【丢最旧】
   被丢掉的那条如果本来"未读"  它的未读身份也跟着消失 
   -> 单独存的计数器就会 > 实际存的消息数 （角标显示 15 但列表只有 10 条 ）
   -> 扫描算 = 永远和数组一致 （这就是"单一口径" ）*/
int msg_unread(void)
{
    int n = 0;
    for (int i = 0; i < s_count; i++)
    {
        int idx = (s_write - s_count + i + RING_MAX) % RING_MAX;
        if (!s_msgs[idx].state_read)
            n++;
    }
    return n;
}
int msg_read(void) { return s_count - msg_unread(); }

/*  取第 i 条（i=0 = 最旧的一条） 环形缓冲的灵魂公式 
   (s_write - s_count + i + RING_MAX) % RING_MAX
     |__ 从写指针往回数 count 格 = 最旧 __|  +RING_MAX 防负数   % 绕回 0~9  */
void msg_get(int i, task_message_t *out)
{
    if (out == NULL || i < 0 || i >= s_count)
        return; /* 越界保护  */

    int idx = (s_write - s_count + i + RING_MAX) % RING_MAX;
    *out = s_msgs[idx]; /*  拷贝出去 （不返回内部指针  防止边读边被覆盖）*/
}

void msg_mark_all_read(void)
{
    /* !! 注意：不能写 s_msgs[i] 
       环形里的有效数据【不一定从 0 开始】 必须用同一个公式定位  */
    for (int i = 0; i < s_count; i++)
    {
        int idx = (s_write - s_count + i + RING_MAX) % RING_MAX;
        s_msgs[idx].state_read = true;
    }
    /* 不用清"未读数"  因为它现在是扫描算出来的 -> 全标已读后自然是 0  */
}

/*  去抖定时器回调：语音删掉后这里【暂时不做事】
   保留它 = 保留 P6「软件定时器做去抖」的结构 
   以后拿来干别的（比如闪灯提示 ）直接往里填  */
static void timer_cb(TimerHandle_t t)
{
    (void)t;
}

/* ==================== 消费者任务：只干"等 + 存 + 报" ==================== */
void task_message_task(void *arg)
{
    (void)arg;
    task_message_t msg_recv;

    if (timer == NULL)
        timer = xTimerCreate("task_message_timer", pdMS_TO_TICKS(500), pdFALSE, NULL, timer_cb);

    while (1)
    {
    
    keep_living(HB_MESSAGE);   /*  心跳：我活着  */
        /*  队列空就在这里睡着  不占 CPU  有数据立刻被唤醒 （对比轮询 ）*/
        /*  原来是 portMAX_DELAY（永不到期）
       没消息时它一直睡着  这是好事  但【看门狗会以为它卡死】
        改成 1 秒超时  醒了没数据就 continue  顺手喂一次心跳 
       开销：每秒醒一次  可忽略  */
        if (xQueueReceive(s_q, &msg_recv, pdMS_TO_TICKS(1000)) != pdTRUE)
            continue;

        /* ---------- 1 入环 ---------- */
        s_msgs[s_write] = msg_recv;         /* 整体拷贝结构体  */
        s_msgs[s_write].state_read = false; /* 新来的当然是未读  */
        s_write = (s_write + 1) % RING_MAX; // 6.1.06   /* 写指针绕着转  这就是"环" */
        if (s_count < RING_MAX)
            s_count++; /* 没满就 +1  满了不动（因为覆盖掉最旧的了 ）*/

        /* ---------- 2 打印（保留  用局部 buf 格式化不额外占存储 ）---------- */
        char buf[224];
        snprintf(buf, sizeof(buf), "来源:%s 时间:%s 内容:%s",
                 msg_src_name(msg_recv.src),
                 msg_ts_to_time(msg_recv.ts),
                 msg_recv.text);
        ESP_LOGI(TAG_TASK_MESSAGE, "[%d/%d 未读%d] %s", s_count, RING_MAX, msg_unread(), buf);

        /* ---------- 3 语音播报 ---------- */
        if (timer != NULL)
        {
            xTimerReset(timer, 0);
        }
    }
}