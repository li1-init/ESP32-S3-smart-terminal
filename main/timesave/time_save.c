/* ==========================================================================
 *  time_save.c  时间断电保存（NVS）
 *
 *   思路（就是你说的"两个选择"）
 *      开机第一件事：从 NVS 读上次时间  立刻 set 进系统 
 *        （这样即使没网时钟也是上次关机时的时间  不是 1970 ）
 *      联网后 SNTP 校准   校准成功后把【准确时间】存回 NVS 
 *      之后每 10 分钟存一次  关机也不会丢太多 
 *
 *   为什么要判断"当前时间是否可信"
 *    刚上电时系统时间是 1970 或很小的值 
 *    如果无条件用 NVS 覆盖它  会把 SNTP 已经对好的时间冲掉 
 *     只在【当前时间明显不对】时才用 NVS 
 * ========================================================================== */
#include <stdio.h>
#include <time.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "time_save.h"
#include "init.h"          /* init_sntp_is_synced()  */

#define TAG_TS      "timesave"
#define TS_NS       "tsave"
#define TS_KEY      "epoch"
#define TS_MIN_VALID 1700000000ULL      /* 2023-11-14 之前都算"不可信"  */
#define TS_SAVE_PERIOD_MS (10 * 60 * 1000)

static nvs_handle_t s_nvs;
static bool         s_opened = false;

static bool open_nvs(void)
{
    if (s_opened) return true;

    /*  上电时 NVS 可能还没 init （0x1101 = NOT_INITIALIZED ）
        这里自己保证一次 （重复调安全 ）*/
    esp_err_t ie = nvs_flash_init();
    if (ie == ESP_ERR_NVS_NO_FREE_PAGES || ie == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ie = nvs_flash_init();
    }
    if (ie != ESP_OK) {
        ESP_LOGE(TAG_TS, "nvs_flash_init 失败 0x%x ", ie);
    }

    esp_err_t e = nvs_open(TS_NS, NVS_READWRITE, &s_nvs);
    if (e != ESP_OK) {
        ESP_LOGE(TAG_TS, "nvs_open 失败 0x%x ", e);
        return false;
    }
    s_opened = true;
    return true;
}

/* ----------------  开机读取 ---------------- */
void time_save_init(void)
{
    time_t now = time(NULL);
    if ((uint64_t)now >= TS_MIN_VALID) {
        ESP_LOGI(TAG_TS, "当前时间可信  不需要用 NVS 覆盖 ");
        return;
    }

    if (!open_nvs()) return;

    uint64_t saved = 0;
    esp_err_t e = nvs_get_u64(s_nvs, TS_KEY, &saved);
    if (e != ESP_OK || saved < TS_MIN_VALID) {
        ESP_LOGW(TAG_TS, "NVS 里还没有可用的时间（第一次开机 ）");
        return;
    }

    struct timeval tv = { .tv_sec = (time_t)saved, .tv_usec = 0 };
    settimeofday(&tv, NULL);

    struct tm tm_n;
    localtime_r((time_t *)&saved, &tm_n);
    ESP_LOGI(TAG_TS, "已从 NVS 恢复时间  %04d-%02d-%02d %02d:%02d:%02d",
             tm_n.tm_year + 1900, tm_n.tm_mon + 1, tm_n.tm_mday,
             tm_n.tm_hour, tm_n.tm_min, tm_n.tm_sec);
}

/* ----------------  保存 ---------------- */
void time_save_now(void)
{
    time_t now = time(NULL);
    if ((uint64_t)now < TS_MIN_VALID) {
        ESP_LOGW(TAG_TS, "时间还不可信  先不存 ");
        return;
    }
    if (!open_nvs()) return;

    esp_err_t e = nvs_set_u64(s_nvs, TS_KEY, (uint64_t)now);
    if (e == ESP_OK) e = nvs_commit(s_nvs);
    if (e != ESP_OK) {
        ESP_LOGE(TAG_TS, "保存失败 0x%x ", e);
    }
}

/* ----------------  后台自动保存 ---------------- */
static void time_save_task(void *arg)
{
    (void)arg;
    bool first_saved = false;

    while (1) {
        if (init_sntp_is_synced()) {
            if (!first_saved) {
                time_save_now();
                ESP_LOGI(TAG_TS, " SNTP 已同步  时间已存入 NVS ");
                first_saved = true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(TS_SAVE_PERIOD_MS));
        if (first_saved) time_save_now();
    }
}

void time_save_start_auto(void)
{
    if (xTaskCreate(time_save_task, "timesave", 3072, NULL, 2, NULL) != pdPASS) {
        ESP_LOGE(TAG_TS, "任务创建失败 ");
    }
}
