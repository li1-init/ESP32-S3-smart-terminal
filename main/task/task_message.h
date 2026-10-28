#ifndef _TASK_MESSAGE_H_
#define _TASK_MESSAGE_H_

#include <stdint.h>
#include <stdbool.h>

/* 一条消息（存的就是它本身  不是格式化好的字符串 ）*/
typedef struct
{
    uint8_t src;     /* 来源用 msg_src_t 的值  */
    uint32_t ts;     /* unix 时间戳  */
    bool state_read; /* 已读标志（方案A：每条自带 ）*/
    char text[128];  /* 正文 */
} task_message_t;

/* 消息来源（数字是给机器判断用的  文字靠 msg_src_name 转 ）*/
typedef enum
{
    msg_src_system = 0, /* 系统：WiFi / 时间 */
    msg_src_cloud,      /* 云端下发 */
    msg_src_alarm,      /* 温度报警 */
    msg_src_schedule,   /* 日程闹钟 */
} msg_src_t;

/* ==================== 对外接口（只露这些 ）====================
   内部状态（队列句柄 / 环形数组 / 计数器）一律 static 在 .c 里  不露出来  */

void task_message_createqueue(void);          /* 建队列（在 app_main 里调一次）*/
void task_message_task(void *arg);            /* 消费者任务 */
void msg_post(uint8_t src, const char *text); /*  投递：任何模块都能调  */

/* 显示用的转换（UI 也要用  所以是公开的 ）*/
const char *msg_src_name(uint8_t src);   /* 来源数字 -> "系统"/"云端"...  */
const char *msg_ts_to_time(uint32_t ts); /* 时间戳 -> "2026-10-23"  */

/* 查询（消息页 / 角标要用 ）*/
int msg_count(void);                      /* 一共存了几条  */
int msg_unread(void);                     /* 几条未读  */
int msg_read(void);                       /* 几条已读  */
void msg_get(int i, task_message_t *out); /* 取第 i 条（i=0 最旧）拷贝出来  */
void msg_mark_all_read(void);             /* 全部标记已读 + 未读清零  */

#endif