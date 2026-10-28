# ============================================================================
#  main 组件的源文件与头文件搜索路径
#
#  约定：每个模块一个文件夹，新增模块就往 includes 里加一行
#    board/   板级引脚定义（board_config.h）
#    port/    LVGL 移植层（屏幕 + 触摸 + 锁）
#    init/    初始化总入口
#    task/    FreeRTOS 任务（lvgl_task / living / 传感器任务…）
#    button/  按键驱动
#    adc/     ADC 采集 + 温度/光照换算
#    iot/     OneNET：token / mqtt / dm / ota
#    ui/      GUI Guider 生成的界面 + custom.c（唯一手改的 UI 文件）
# ============================================================================
set(includes
    "."
    "./board"
    "./port"
    "./init"
    "./task"
    "./button"
    "./adc"
    "./iot"
    "./ota"
    "./light"
    "./voice"
    "./wdt"
    "./ui2"
    "./ui2/fonts"
    "./ui2/icons"
    "./weather"
    "./timesave"
    "./ai"
    "./game"
)

file(GLOB_RECURSE sources ./*.c)
idf_component_register(SRCS ${sources}
                       INCLUDE_DIRS ${includes})
# ⚠️ 注意：main 组件【不用写 REQUIRES】✓ 它会自动链接构建里所有组件 ✓
#    （CMake 注释是 # 而不是 /* */ ✗ 我在这儿踩过一次 ✓）

# 配网页面（apcfg.html）烧进 html 分区
spiffs_create_partition_image(html ../html FLASH_IN_PROJECT)

# 图片资源烧进 image 分区
spiffs_create_partition_image(image ../image FLASH_IN_PROJECT)

# 方式 B：写在 CMake 里（你以前的做法 ✓）
add_definitions(-DFW_VERSION="1.0.0")