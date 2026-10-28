#ifndef _OTA_APP_H_
#define _OTA_APP_H_

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 *  设置页「检查更新 / 开始更新」两个按钮的应用层实现
 *
 *  为什么单独一个文件：
 *     custom.c 里那两个按钮的回调是【弱符号】钩子  它只负责改文字 
 *     真正的网络/OTA 动作丢给这里 （UI 与业务分离 ）
 *
 *  custom.c 里声明的是：
 *      __attribute__((weak)) void ota_app_check(void)        { }
 *      __attribute__((weak)) void ota_app_begin_update(void) { }
 *  这里给【强实现】 链接时自动覆盖弱符号 
 * ========================================================================== */

void ota_app_check(void);          /* 检查有没有新版本  */
void ota_app_begin_update(void);   /* 真的开始下载+烧写+重启  */

#ifdef __cplusplus
}
#endif

#endif /* _OTA_APP_H_ */
