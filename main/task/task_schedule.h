#ifndef _SCHEDULE_H_
#define _SCHEDULE_H_
#define schedule_count_sum_max 8

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

typedef struct
{
   int schedule_count;         //日程的编号
   char schedule_text[128];    //日程的内容
   struct tm schedule_time;    //日程到期的时间
   int schedule_rule;          //日程的规则：0-每天1-工作日2-指定日期
   bool schedule_enable;       //是否启用
} schedule_task_t;

typedef enum
{
    schedule_rule_everyday = 0,
    schedule_rule_workday = 1,
    schedule_rule_specify = 2,
} schedule_rule_t;

void schedule_task(void *arg);

/* ===== 下面这些是你写在 .c 里、但没有在 .h 里声明的对外函数 （按你写的原样补上 ）===== */
void Enable_schedule_task_message(int schedule_count);
void Disable_schedule_task_message(int schedule_count);
void Delete_schedule_task_message(int schedule_count);
bool create_schedule_task_message(char *schedule_text, int tm_wday, int hour, int minute, int schedule_rule);
void Create_schedule_task_message(char *schedule_text, int tm_wday, int hour, int minute, int schedule_rule);
bool Update_schedule_task_message(int idx, char *schedule_text, int tm_wday, int hour, int minute, int schedule_rule);   /*  修改第 idx 条  */
int  get_schedule_task_message_sum(void);
char *get_schedule_task_message_text(int schedule_count);
int  get_schedule_task_message_hour(int schedule_count);      /*  给 UI 用：数据  不是字符串  */
int  get_schedule_task_message_minute(int schedule_count);
int  get_schedule_task_message_rule(int schedule_count);
int  get_schedule_task_message_wday(int schedule_count);
bool get_schedule_task_message_state(int schedule_count);
char *get_schedule_task_message_time(int schedule_count);

#endif
