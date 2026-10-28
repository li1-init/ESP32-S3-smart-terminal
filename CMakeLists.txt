# ESP32-S3 模板工程（基于买板子配套的 BSP 驱动）
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# ============================================================================
# ★★★ 固件版本号 ★★★（OneNET OTA 要用，格式必须是 x.y.z ✗ 不能带 -dirty ✗）
#   作用：① 开机打印 ② OneNET OTA 上报 ③ 覆盖 git 自动生成的版本
#   ⚠️ 版本号必须【递增】✗ 否则 OneNET 认为"没有新版本"✗ 不会升级 ✓
# ============================================================================
set(PROJECT_VER "1.0.0")

project(esp32s3_template)