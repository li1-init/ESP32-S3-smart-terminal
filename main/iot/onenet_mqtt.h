#ifndef _ONENET_MQTT_H_
#define _ONENET_MQTT_H_
#include "esp_err.h"

/* 产品 ID / 产品密钥 / 设备名称
 * 真实值放在 main/app_secrets.h（本机私密文件，已被 .gitignore 忽略），
 * 仓库里只有 app_secrets.h.example 模板。 */
#include "app_secrets.h"

/**
 * 上报数据
 * @param data 数据
 * @return 错误
 */
esp_err_t onenet_post_property_data(const char *data);

/**
 * 启动onenet连接
 * @return 错误码
 */
esp_err_t onenet_start(void);

/**
 * 停止onenet连接
 * @param 无
 * @return 错误码
 */
esp_err_t onenet_stop(void);

#endif
