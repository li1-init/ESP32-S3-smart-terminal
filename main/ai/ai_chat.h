/* ==========================================================================
 *  ai_chat.h  接 DeepSeek 大模型（HTTPS 一问一答）
 *
 *   设计：UI 只负责"发问题 / 显示答案"
 *       ai_chat_ask() 可以在【任何任务】里调  它自己起任务  不阻塞 
 *       结果放静态缓冲  UI 用 ai_chat_answer() 轮询取 
 *    （LVGL 任务里绝不能阻塞等网络  会卡界面 + 触发看门狗 ）
 * ========================================================================== */
#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

/* 发一个问题（非阻塞  异步  同一时间只跑一个 ）*/
void ai_chat_ask(const char *question);

/* 状态：0=空闲 1=请求中 2=完成 3=失败 */
int          ai_chat_state(void);
/* 取答案（完成后有效 ）*/
const char  *ai_chat_answer(void);
/* 上次错误信息（失败时看 ）*/
const char  *ai_chat_error(void);

#ifdef __cplusplus
}
#endif
