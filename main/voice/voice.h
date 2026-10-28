/* ==========================================================================
 *  voice.h  语音模块（第一版：麦克风自检）
 *
 *   为什么第一步只做"自检"：不印到波形后面搬模型全是白做
 *    之前实测麦克风读到过【死值】(956974225) 又读到过【全 0】
 *     那是 PDM 的直流偏置  不减均值算能量必然看到"不动" 
 *     所以这里算的是【去掉直流后的 RMS/峰峰值】
 *
 *  硬件：PDM 麦克风  CLK = IO3   DAT = IO42   I2S_NUM_0  RX
 * ========================================================================== */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化 PDM 麦克风（I2S_NUM_0 RX） 不启动任务  */
esp_err_t voice_mic_init(void);

/* 启动自检任务：每 500ms 往串口打一次 RMS / 峰峰值 
    验收标准：安静时数值很小  对着说话时明显跳大（10 倍以上） */
esp_err_t voice_mic_test_start(void);

/* 最近一次测量结果（外部也能取  以后做"音量指示"可用 ）*/
uint32_t voice_mic_last_rms(void);
uint32_t voice_mic_last_pp(void);      /* peak-to-peak 峰峰值  */

#ifdef __cplusplus
}
#endif
