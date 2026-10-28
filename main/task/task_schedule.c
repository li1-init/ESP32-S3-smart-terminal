#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "task_schedule.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "task_message.h"
#include "wdt.h"
#include <time.h>
#include <nvs.h>
#include "esp_err.h"
#include <task_begin.h>
#include "freertos/timers.h"

#define TAG_schedule "schedule_task"

static char time_str[128] = {0};

static int schedule_count_sum = 0;

static schedule_task_t schedule_task_message_list[schedule_count_sum_max] = {0};

/* ==================== NVS 存取（F9 的骨架  这里先用上 ）====================
   ⚠️ 坑1：nvs_flash_init 不用管  WiFi 已经初始化过了 
   ⚠️ 坑2：nvs_set_blob 之后【必须 nvs_commit】 少了它断电就丢 
   ⚠️ 坑3：第一次开机键不存在  当成"0 条" 不是错误 
   ⚠️ 坑4：读回来要【夹住】条数 （防御 ）
   ⚠️ 坑5：struct tm 里有个 const char *tm_zone 指针  存进 NVS 就是垃圾 
            但你只读 tm_wday/tm_hour/tm_min  那个指针永远不会被解引用  所以没事 
   ========================================================================== */
#define NVS_NS_SCHED "sched"
#define NVS_KEY_SCHED "items"

static void sched_save(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_SCHED, NVS_READWRITE, &h) != ESP_OK)
    {
        ESP_LOGE(TAG_schedule, "NVS 打开失败 ");
        return;
    }
    esp_err_t err = nvs_set_blob(h, NVS_KEY_SCHED, schedule_task_message_list,
                                 (size_t)schedule_count_sum * sizeof(schedule_task_t));
    if (err == ESP_OK)
        err = nvs_commit(h); /*  缺这句  断电就丢  */
    nvs_close(h);

    if (err != ESP_OK)
        ESP_LOGE(TAG_schedule, "NVS 保存失败：%s", esp_err_to_name(err));
    else
        ESP_LOGI(TAG_schedule, "已保存 %d 条日程到 NVS ", schedule_count_sum);
}

static void sched_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS_SCHED, NVS_READONLY, &h) != ESP_OK)
        return; /* 还没存过  就当 0 条  */

    size_t len = sizeof(schedule_task_message_list);
    if (nvs_get_blob(h, NVS_KEY_SCHED, schedule_task_message_list, &len) == ESP_OK) //数据复制进去schedule_task_message_list[schedule_count_sum] 这个数组
    {
        int n = (int)(len / sizeof(schedule_task_t)); /*  用长度反推条数  */
        if (n < 0)
            n = 0;
        if (n > schedule_count_sum_max)
            n = schedule_count_sum_max; /*  夹住  */
        schedule_count_sum = n;
        ESP_LOGI(TAG_schedule, "从 NVS 读回 %d 条日程 ", n);
    }                                                                               // nvs_get_blob（） 把这块数据赋值出来到ram里面  len刚开始是你写的你想要多少后面经过这个函数变成了实际输出了多少
    nvs_close(h);
}
static bool enable_schedule_task_message(int schedule_count)
{
    if (schedule_count >= schedule_count_sum)
    {
        return false;
    }
    schedule_task_message_list[schedule_count].schedule_enable = true;
    return true;
}

static bool disable_schedule_task_message(int schedule_count)
{
    if (schedule_count >= schedule_count_sum)
    {
        return false;
    }
    schedule_task_message_list[schedule_count].schedule_enable = false;
    return true;
}

void Enable_schedule_task_message(int schedule_count)
{
    /*  写法：先拿结果  再统一反馈 （比写两遍 msg_post 干净 ）*/
    bool ok = enable_schedule_task_message(schedule_count);
    if (ok) sched_save();

    char err_msg[128] = {0};
    snprintf(err_msg, sizeof(err_msg),
             ok ? "日程 %d 启用成功" : "启用失败：没有第 %d 条日程",   /*  三元只用来【选文案】 */
             schedule_count);
    msg_post(msg_src_system, err_msg);
}

void Disable_schedule_task_message(int schedule_count)
{
    bool ok = disable_schedule_task_message(schedule_count);
    if (ok) sched_save();

    char err_msg[128] = {0};
    snprintf(err_msg, sizeof(err_msg),
             ok ? "日程 %d 停用成功" : "停用失败：没有第 %d 条日程",
             schedule_count);
    msg_post(msg_src_system, err_msg);
}

void Delete_schedule_task_message(int schedule_count)
{
    /*   边界检查：别人传来的下标必须拦 （负数也要拦 ）*/
    if (schedule_count < 0 || schedule_count >= schedule_count_sum)
    {
        char err_msg[128] = {0};
        snprintf(err_msg, sizeof(err_msg), "删除失败：没有第 %d 条日程", schedule_count);
        msg_post(msg_src_system, err_msg);
        return;
    }

    /*   把【后面所有元素】整体前移一格（补上这个洞 ）
       目标首地址   = &list[schedule_count]        （洞的位置 ）
       源首地址     = &list[schedule_count + 1]    （它的下一格 ）
       要搬的字节数 = 剩下的条数  每条大小
       ⚠️ 必须用 memmove  不能用 memcpy   两块内存【重叠】时 memcpy 是未定义行为  */
    int remain = schedule_count_sum - schedule_count - 1; /* 洞后面还有几条  */
    if (remain > 0)
    {
        memmove(&schedule_task_message_list[schedule_count],
                &schedule_task_message_list[schedule_count + 1],
                (size_t)remain * sizeof(schedule_task_t));
    }

    /*   条数减一并把【最后那一格】清掉（不然留着残影 而且下次新建会看到旧内容 ）*/
    schedule_count_sum--;
    schedule_task_message_list[schedule_count_sum] = (schedule_task_t){0};

    char err_msg[128] = {0};
    sched_save(); /*  删完就存  */
    snprintf(err_msg, sizeof(err_msg), "日程 %d 删除成功", schedule_count);
    msg_post(msg_src_system, err_msg);
}

int get_schedule_task_message_sum(void)
{
    return schedule_count_sum;
}

/*  给 UI 用的 3 个 getter：UI 要【数据】 不要"格式化好的字符串" 
   （"数据 vs 显示分离"   "设置时间22:35" 是给显示用的 
     UI 真正需要的是 22 和 35 这两个数字  才好排版 ）*/
int get_schedule_task_message_hour(int schedule_count)
{
    if (schedule_count < 0 || schedule_count >= schedule_count_sum) return -1;
    return schedule_task_message_list[schedule_count].schedule_time.tm_hour;
}
int get_schedule_task_message_minute(int schedule_count)
{
    if (schedule_count < 0 || schedule_count >= schedule_count_sum) return -1;
    return schedule_task_message_list[schedule_count].schedule_time.tm_min;
}
int get_schedule_task_message_rule(int schedule_count)
{
    if (schedule_count < 0 || schedule_count >= schedule_count_sum) return -1;
    return schedule_task_message_list[schedule_count].schedule_rule;
}
int get_schedule_task_message_wday(int schedule_count)
{
    if (schedule_count < 0 || schedule_count >= schedule_count_sum) return -1;
    return schedule_task_message_list[schedule_count].schedule_time.tm_wday;   /* 0=周日  */
}
char *get_schedule_task_message_text(int schedule_count)
{
    if (schedule_count < 0 || schedule_count >= schedule_count_sum)
        return NULL; /*  边界  */
    return schedule_task_message_list[schedule_count].schedule_text;
}

bool get_schedule_task_message_state(int schedule_count)
{
    if (schedule_count < 0 || schedule_count >= schedule_count_sum)
        return false; /*  边界  */
    return schedule_task_message_list[schedule_count].schedule_enable;
}

char *get_schedule_task_message_time(int schedule_count)
{
    if (schedule_count < 0 || schedule_count >= schedule_count_sum) /*  边界  */
    {
        time_str[0] = '\0';
        return time_str;
    }
    memset(time_str, 0, sizeof(time_str));
    snprintf(time_str, sizeof(time_str), "设置时间%02d:%02d",
             schedule_task_message_list[schedule_count].schedule_time.tm_hour,
             schedule_task_message_list[schedule_count].schedule_time.tm_min);
    return time_str;
}

bool create_schedule_task_message(char *schedule_text, int tm_wday, int hour, int minute, int schedule_rule)
{
    if (schedule_count_sum >= schedule_count_sum_max) /*  8 格数组>= 才对  用 > 会越界写第 9 条  */
    {
        return false;
    }
    schedule_task_message_list[schedule_count_sum].schedule_count = schedule_count_sum;
    schedule_task_message_list[schedule_count_sum].schedule_rule = schedule_rule;
    snprintf(schedule_task_message_list[schedule_count_sum].schedule_text, sizeof(schedule_task_message_list[schedule_count_sum].schedule_text), "%s", schedule_text);
    schedule_task_message_list[schedule_count_sum].schedule_time.tm_wday = tm_wday;
    schedule_task_message_list[schedule_count_sum].schedule_time.tm_hour = hour;
    schedule_task_message_list[schedule_count_sum].schedule_time.tm_min = minute;
    schedule_task_message_list[schedule_count_sum].schedule_enable = true;    /*  新建默认启用  UI 不用再补一次 enable  */
    schedule_count_sum++;
    sched_save(); /*  加完就存  */
    return true;
}

/*  修改第 idx 条（界面上的"改"按钮用 ）
    只改这一条  不动别的  位置也不变 （比"删了再建"体验好 ）
    改完顺手存 NVS  + 给反馈  */
bool Update_schedule_task_message(int idx, char *schedule_text, int tm_wday, int hour, int minute, int schedule_rule)
{
    if (idx < 0 || idx >= schedule_count_sum)                 /*  边界  */
    {
        msg_post(msg_src_system, "修改失败：没有这条日程");
        return false;
    }
    schedule_task_t *m = &schedule_task_message_list[idx];
    snprintf(m->schedule_text, sizeof(m->schedule_text), "%s", schedule_text);
    m->schedule_time.tm_wday = tm_wday;
    m->schedule_time.tm_hour = hour;
    m->schedule_time.tm_min  = minute;
    m->schedule_rule         = schedule_rule;
    sched_save();

    char err_msg[128] = {0};
    snprintf(err_msg, sizeof(err_msg), "日程已改为：%02d:%02d  %s", hour, minute, schedule_text);
    msg_post(msg_src_system, err_msg);
    return true;
}

void Create_schedule_task_message(char *schedule_text, int tm_wday, int hour, int minute, int schedule_rule)
{
    bool ok = create_schedule_task_message(schedule_text, tm_wday, hour, minute, schedule_rule);

    char err_msg[128] = {0};
    if (ok)
        snprintf(err_msg, sizeof(err_msg), "日程已创建：%02d:%02d  %s", hour, minute, schedule_text);
    else
        snprintf(err_msg, sizeof(err_msg), "创建失败：最多只能有 %d 条日程", schedule_count_sum_max);
    msg_post(msg_src_system, err_msg);
}

/*  测试程序：加一条"现在 + N 分钟"的日程并【直接启用】（验收就靠它 ）
   以后 UI 里那个"1 分钟后测试"按钮也是这么实现的  */
static void sched_add_test(int minutes_later, const char *text)
{
    time_t t = time(NULL) + (time_t)minutes_later * 60;
    struct tm tm_target;
    localtime_r(&t, &tm_target); /*  可重入  跨小时/跨天都自动对  */

    char tmp[128];
    snprintf(tmp, sizeof(tmp), "%s", text); /* 避免把字符串常量传给 char*  */

    if (!create_schedule_task_message(tmp, tm_target.tm_wday, tm_target.tm_hour, tm_target.tm_min, schedule_rule_everyday))
        return;

    enable_schedule_task_message(schedule_count_sum - 1); /* 新建默认是关的  测试要开起来  */
    sched_save();
    ESP_LOGI(TAG_schedule, " 测试日程已加：%02d:%02d  %s（等它到点 ）",
             tm_target.tm_hour, tm_target.tm_min, text);
}

bool schdule_task_message_time_judeg(schedule_task_t *schedule_task_message)
{
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now); /*  可重入：结果写进【自己的】变量  不用锁  */
    return (tm_now.tm_hour == schedule_task_message->schedule_time.tm_hour &&
            tm_now.tm_min == schedule_task_message->schedule_time.tm_min);
}

bool schdule_task_message_rule_judeg(schedule_task_t *schedule_task_message)
{
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now); /*  可重入  */

    if (schedule_task_message->schedule_rule == schedule_rule_everyday)
        return true;
    if (schedule_task_message->schedule_rule == schedule_rule_workday)
        return tm_now.tm_wday != 0 && tm_now.tm_wday != 6;
    if (schedule_task_message->schedule_rule == schedule_rule_specify)
        return tm_now.tm_wday == schedule_task_message->schedule_time.tm_wday;
    return false;
}

void schedule_task(void *arg)
{
    /*  开机先读回上次保存的日程  */
    sched_load();

    

    while (1)
    {
    
    keep_living(HB_SCHEDULE);   /*  心跳：我活着  */
        if (!get_sntp_bool())
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        for (int i = 0; i < schedule_count_sum; i++)
        {

            if (!schedule_task_message_list[i].schedule_enable)
                continue;
            if (!schdule_task_message_time_judeg(&schedule_task_message_list[i]))
                continue;
            if (!schdule_task_message_rule_judeg(&schedule_task_message_list[i]))
                continue;

            msg_post(msg_src_schedule, schedule_task_message_list[i].schedule_text);   /*  来源=日程  消息页会显示"日程"  */
        }
        /*  对齐到下一个【整分】（秒=00）再醒 
           算法：看现在第几秒  还差 (60 - 秒) 秒到整分 
           ⚠️ 正好 0 秒时要等【满 60 秒】 否则会 1 秒内醒 60 次 
            每次都重新算  自动纠正累积误差  不会漂移  */
        time_t now = time(NULL);
        struct tm tm_now;
        localtime_r(&now, &tm_now);
        int wait_s = 60 - tm_now.tm_sec;
        if (wait_s <= 0)
            wait_s = 60;
        /*  原来是 vTaskDelay(整整 wait_s 秒) 一次性睡 
               等下一个闹钟可能要几分钟  期间不喂心跳 
                看门狗 15 秒就判死   【会自动重启】
           现在：拆成 5 秒一段  每段喂一次心跳  总时长不变  */
        for (uint32_t left = wait_s; left > 0; left -= 5)
        {
            keep_living(HB_SCHEDULE);
            vTaskDelay(pdMS_TO_TICKS((left >= 5 ? 5 : left) * 1000));
        }
    }
}
