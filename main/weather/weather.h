#ifndef __WEATHER_H__
#define __WEATHER_H__

#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WEATHER_CITY_MAX_LEN      64
#define WEATHER_TEXT_MAX_LEN      64
#define WEATHER_TEMP_MAX_LEN      16
#define WEATHER_CODE_MAX_LEN      8
#define WEATHER_DATE_MAX_LEN      16
#define WEATHER_FORECAST_DAYS     3

typedef struct {
    char city[WEATHER_CITY_MAX_LEN];
    char temperature[WEATHER_TEMP_MAX_LEN];
    char text[WEATHER_TEXT_MAX_LEN];
} weather_data_t;

typedef struct {
    char date[WEATHER_DATE_MAX_LEN];
    char code_day[WEATHER_CODE_MAX_LEN];
    char text_day[WEATHER_TEXT_MAX_LEN];
    char code_night[WEATHER_CODE_MAX_LEN];
    char text_night[WEATHER_TEXT_MAX_LEN];
    int high;
    int low;
} weather_day_t;

typedef struct {
    char city[WEATHER_CITY_MAX_LEN];
    weather_day_t days[WEATHER_FORECAST_DAYS];
} weather_forecast_t;

/**
 * @brief 通过公网 IP 获取当前城市名称。
 */
esp_err_t weather_get_ip_city(char *city, size_t city_len);

/**
 * @brief 获取指定城市的实时天气。
 */
esp_err_t weather_get_by_city(const char *city, weather_data_t *data);

/**
 * @brief 获取固定城市（武汉）的实时天气。
 */
esp_err_t weather_get(weather_data_t *data);

/**
 * @brief 获取指定城市未来 3 天预报days[0] 今天days[1] 明天days[2] 后天。
 */
esp_err_t weather_get_forecast_by_city(const char *city, weather_forecast_t *forecast);

/**
 * @brief 获取固定城市（武汉）未来 3 天预报。
 */
esp_err_t weather_get_forecast(weather_forecast_t *forecast);

/**
 * @brief 根据心知天气返回的 code_day/text_day 生成 SPIFFS 图片路径。
 *
 * 图片放在 image 分区挂载点为 /image文件名为 0@1x.png ~ 38@1x.png未知天气使用 99@1x.png。
 * LVGL 使用 A: 作为 stdio 文件系统盘符所以返回路径形如：
 *   A:/image/14@1x.png
 *
 * @param day      某一天的心知天气数据
 * @param path     输出路径缓冲区
 * @param path_len 输出缓冲区长度
 * @return ESP_OK 成功其他值失败
 */
esp_err_t weather_day_get_icon_path(const weather_day_t *day,
                                    char *path,
                                    size_t path_len);

#ifdef __cplusplus
}
#endif

#endif /* __WEATHER_H__ */