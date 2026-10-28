#ifndef _ONENET_OTA_H_
#define _ONENET_OTA_H_
#include "esp_err.h"

/**
 * 启动onenet ota升级流程
 * @param 无
 * @return 无
 */
void onenet_ota_start(void);

/*  只检查、不下载 （设置页「检查更新」用）
   返回 ESP_OK            = 有新版本 （版本号写进 out_ver ）
        ESP_ERR_NOT_FOUND  = 已是最新 
        ESP_FAIL           = 网络/鉴权失败  */
esp_err_t onenet_ota_check_only(char *out_ver, size_t out_len);

/*  读上一次检查到的目标版本 / 任务 id（调试用  可为 NULL）*/
const char *onenet_ota_target_version(void);
int         onenet_ota_task_id(void);

/**
 * 获取应用程序版本号
 * @param 无
 * @return 版本号
 */
const char* get_app_verion(void);

/**
 * 设置合法启动分区
 * @param vaild 是否合法
 * @return 无
 */
void set_app_vaild(int vaild);

/**
 * 上报版本号
 * @param 无
 * @return 错误码
 */
esp_err_t  onenet_ota_upload_version(void);

extern bool my_onenet_ota_upload_version;
extern bool get_my_onenet_ota_upload_version(void);

#endif
