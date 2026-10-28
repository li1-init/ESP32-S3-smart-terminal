/* ==========================================================================
 *  OTA 应用层：设置页「检查更新 / 开始更新」
 *
 *   整体流程（OneNET 的 OTA 是"HTTPS 为主MQTT 只做通知"）
 *     上报当前版本      onenet_ota_upload_version()   走 HTTPS
 *     询问有没有新版本  onenet_ota_check_only()       走 HTTPS
 *         返回 ESP_OK            = 有新版本  拿到版本号 
 *         返回 ESP_ERR_NOT_FOUND = 已是最新 
 *         返回 ESP_FAIL          = 网络/鉴权出错 
 *     有新版本  用户点「开始更新」
 *         onenet_ota_start()   下载  校验  写 ota 分区  esp_restart()
 *
 *   线程模型（这是最容易踩的地方）
 *     按钮回调跑在【LVGL 任务】里 
 *     HTTPS 请求要几秒钟  绝不能在 LVGL 任务里阻塞 （界面会卡死）
 *     所以：这里起一个普通任务去做网络 
 *      做完了要改控件  不能直接改 
 *       用 lv_async_call() 把"改控件"这件事【托回 LVGL 任务】
 * ========================================================================== */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>   /* strdup / free  */
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lvgl.h"

#include "onenet_ota.h"
#include "ui.h"             /*  新 UI：ui_ota_set_versions()  */
#include "ota_app.h"

#define TAG_OTA_APP "ota_app"

/* 检查结果通过 lv_async_call 的 user_data 传给 LVGL 任务  */
typedef enum {
    OTA_RES_AVAILABLE = 0,   /* 有新版本  */
    OTA_RES_NONE,            /* 已是最新  */
    OTA_RES_ERROR,           /* 出错  */
} ota_res_t;

/* ==========================================================================
 *   下面三个回调都跑在【LVGL 任务】里  可以安全操作控件 
 *    它们由 lv_async_call 触发 
 * ========================================================================== */

static void ui_show_available(void *arg)
{
    char *ver = (char *)arg;
    if (ver) {
        ui_ota_set_versions(get_app_verion(), ver, "发现新版本可点「开始更新」");
        free(ver);                               /*  谁分配的谁释放  */
    }
}

static void ui_show_none(void *arg)
{
    (void)arg;
    ui_ota_set_versions(get_app_verion(), "已是最新", "已是最新版本");
}

static void ui_show_error(void *arg)
{
    (void)arg;
    ui_ota_set_versions(get_app_verion(), "--", "检查失败：网络/鉴权出错");
}

/* ==========================================================================
 *  检查任务（跑在普通任务里  不碰控件 ）
 * ========================================================================== */
static void ota_check_task(void *param)
{
    (void)param;
    char ver[16] = {0};

    /*  先把当前版本报给云端 （云端要靠它算"有没有更新"）*/
    if (onenet_ota_upload_version() != ESP_OK) {
        ESP_LOGE(TAG_OTA_APP, "上报版本失败 （设备没连上？）");
        lv_async_call(ui_show_error, NULL);
        goto done;
    }

    /*  问云端 */
    esp_err_t ret = onenet_ota_check_only(ver, sizeof(ver));
    if (ret == ESP_OK) {
        ESP_LOGI(TAG_OTA_APP, " 发现新版本：%s", ver);
        char *heap = strdup(ver);                 /*  拷到堆上  因为 lv_async_call 是异步的  */
        if (heap) lv_async_call(ui_show_available, heap);
        else      lv_async_call(ui_show_error, NULL);
    } else if (ret == ESP_ERR_NOT_FOUND) {
        ESP_LOGI(TAG_OTA_APP, "已是最新版本");
        lv_async_call(ui_show_none, NULL);
    } else {
        ESP_LOGE(TAG_OTA_APP, "检查失败 ");
        lv_async_call(ui_show_error, NULL);
    }

done:
    vTaskDelete(NULL);
}

/* ==========================================================================
 *  对外接口
 * ========================================================================== */
void ota_app_check(void)
{
    /* ⚠️ 按钮可能被连点  但这里不用特殊处理：
       连点最多起两个任务  都只是查询  无害 （真正下载由 ota_is_running 保护 ）*/
    ESP_LOGI(TAG_OTA_APP, "开始检查更新");
    if (xTaskCreate(ota_check_task, "ota_check", 6144, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG_OTA_APP, "任务创建失败 ");
        lv_async_call(ui_show_error, NULL);
    }
}

void ota_app_begin_update(void)
{
    /*  真正的下载/烧写/重启由 iot 层完成 
         它内部有 ota_is_running 保护  不会重入  */
    ESP_LOGI(TAG_OTA_APP, "开始下载并升级");
    onenet_ota_start();
}
