/* ==========================================================================
 *  voice.c  PDM 麦克风自检（语音识别第 0 步：必过闸门）
 *
 *   为什么"减法"是这一步的核心
 *      PDM 麦克风输出是【单比特流】转成 16 位采样后有一个【很大的直流偏置】
 *      例如读到 956974225 那种"死值"其实就是偏置占满了 
 *       直接算平方和：偏置的平方会淹没一切  说话也看不出变化 
 *       必须先减【均值】(去直流)  剩下的才是"声音" 
 *
 *   三个指标
 *      RMS    ：整体能量（说话时明显变大 ）
 *      PP     ：峰峰值（更直观"安静 20 / 说话 3000"  推荐看这个 ）
 *      MEAN   ：直流偏置（诊断用  应该是个很大的常数  变了说明硬件有问题 ）
 * ========================================================================== */
#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"

#include "driver/i2s_pdm.h"
#include "driver/gpio.h"

#include "voice.h"

#define TAG_VOICE "voice"

/* ---------------- 硬件引脚（NOTES 已核对 ）---------------- */
#define VOICE_MIC_CLK      GPIO_NUM_3
#define VOICE_MIC_DIN      GPIO_NUM_42
#define VOICE_MIC_I2S      I2S_NUM_0

/* ---------------- 采样参数 ---------------- */
#define VOICE_SR_HZ        16000           /* 16kHz：语音识别标准采样率  */
#define VOICE_DMA_DESC     4
#define VOICE_DMA_FRAMES   256
#define VOICE_READ_N       512             /* 一次读 512 个采样 = 32ms  */

static i2s_chan_handle_t s_rx = NULL;
static bool              s_inited = false;
static TaskHandle_t      s_test_task = NULL;

static volatile uint32_t s_last_rms = 0;
static volatile uint32_t s_last_pp  = 0;

uint32_t voice_mic_last_rms(void) { return s_last_rms; }
uint32_t voice_mic_last_pp(void)  { return s_last_pp;  }

/* ==========================================================================
 *  初始化 PDM RX
 * ========================================================================== */
esp_err_t voice_mic_init(void)
{
    if (s_inited) return ESP_OK;

    /* ----  创建一个 RX 通道 ---- */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(VOICE_MIC_I2S, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num  = VOICE_DMA_DESC;
    chan_cfg.dma_frame_num = VOICE_DMA_FRAMES;

    esp_err_t ret = i2s_new_channel(&chan_cfg, NULL, &s_rx);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_VOICE, "i2s_new_channel 失败 0x%x ", ret);
        return ret;
    }

    /* ----  配成 PDM RX 模式 ---- */
    i2s_pdm_rx_config_t pdm_cfg = {
        .clk_cfg  = I2S_PDM_RX_CLK_DEFAULT_CONFIG(VOICE_SR_HZ),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                   I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = VOICE_MIC_CLK,
            .din = VOICE_MIC_DIN,
            .invert_flags = { .clk_inv = false },
        },
    };
    ret = i2s_channel_init_pdm_rx_mode(s_rx, &pdm_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_VOICE, "PDM RX 模式配置失败 0x%x ", ret);
        i2s_del_channel(s_rx);
        s_rx = NULL;
        return ret;
    }

    /* ----  开通道 ---- */
    ret = i2s_channel_enable(s_rx);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_VOICE, "i2s 通道使能失败 0x%x ", ret);
        i2s_del_channel(s_rx);
        s_rx = NULL;
        return ret;
    }

    s_inited = true;
    ESP_LOGI(TAG_VOICE, "麦克风就绪  PDM %d Hz  CLK=IO%d  DAT=IO%d",
             VOICE_SR_HZ, (int)VOICE_MIC_CLK, (int)VOICE_MIC_DIN);
    return ESP_OK;
}

/* ==========================================================================
 *  自检任务：读一段  算指标  打串口
 * ========================================================================== */
static void mic_test_task(void *param)
{
    (void)param;

    int16_t buf[VOICE_READ_N];
    size_t  bytes_read = 0;

    ESP_LOGI(TAG_VOICE, "自检开始  请【对着麦克风说话】看 PP 值有没有跳变 ");

    while (1) {
        esp_err_t ret = i2s_channel_read(s_rx, buf, sizeof(buf), &bytes_read, 1000);
        if (ret != ESP_OK || bytes_read == 0) {
            ESP_LOGW(TAG_VOICE, "读麦克风失败 0x%x ", ret);
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        int n = (int)(bytes_read / sizeof(int16_t));

        /* ---- 先求均值 = 直流偏置  ---- */
        int64_t sum = 0;
        int16_t vmin = buf[0], vmax = buf[0];
        for (int i = 0; i < n; i++) {
            sum += buf[i];
            if (buf[i] < vmin) vmin = buf[i];
            if (buf[i] > vmax) vmax = buf[i];
        }
        int32_t mean = (int32_t)(sum / n);

        /* ---- 去掉直流再算能量  这是关键  ---- */
        int64_t sq = 0;
        for (int i = 0; i < n; i++) {
            int32_t d = (int32_t)buf[i] - mean;      /*  去直流  */
            sq += (int64_t)d * d;
        }
        uint32_t rms = (uint32_t)sqrtf((float)sq / (float)n);
        uint32_t pp  = (uint32_t)((int32_t)vmax - (int32_t)vmin);

        s_last_rms = rms;
        s_last_pp  = pp;

        ESP_LOGI(TAG_VOICE, "RMS=%5u  峰峰=%5u  均值=%6d", (unsigned)rms,
                 (unsigned)pp, (int)mean);

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

esp_err_t voice_mic_test_start(void)
{
    esp_err_t ret = voice_mic_init();
    if (ret != ESP_OK) return ret;
    if (s_test_task != NULL) return ESP_OK;

    if (xTaskCreate(mic_test_task, "mic_test", 4096, NULL, 3, &s_test_task) != pdPASS) {
        ESP_LOGE(TAG_VOICE, "自检任务创建失败 ");
        return ESP_FAIL;
    }
    return ESP_OK;
}
