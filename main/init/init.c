/*
 * init.c  初始化总入口
 *
 * 内容：
 *   init_board()    XL9555 扩展 IO（背光/复位/功放都靠它）
 *   init_button()   4 个按键
 *   init_wifi()     NVS + WiFi 连接
 *   init_sntp()     SNTP 时间同步（连上 WiFi 后自动调用）
 *   init_all()      按顺序做完上面全部
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_sntp.h"

#include "board_config.h"
#include "xl9555.h"
#include "button.h"
#include "ap_wifi.h"

#include "init.h"
#include "wdt.h"
#include "time_save.h"   /*  时间断电保存  */
#include "light.h"     /*  LED2 + WS2812  */
#include "voice.h"     /*  麦克风自检 / 语音  */
#include "task_begin.h"
#include "onenet_mqtt.h"   /*  onenet_start() 的声明  你漏了这个 include  */
#include "app_secrets.h"   /*  兜底 WiFi 账号（本机私密文件，已被 .gitignore 忽略） */
#include "task_message.h"


/* ==================== 日志 TAG ==================== */
#define TAG_BOARD  "board"
#define TAG_BUTTON "button"
#define TAG_WIFI   "wifi"
#define TAG_SNTP   "sntp"

/* ==================== 可改的配置 ==================== */
/* 兜底 WiFi 账号（NVS 里没有配网信息时才会用），定义在 main/app_secrets.h，
   该文件已被 .gitignore 忽略，不要写死在这里 */

/* ==========================================================================
 *  XL9555 + 按键
 * ========================================================================== */
/* 按键电平缓存：XL9555 中断回调里更新按键模块轮询读取。
 * 用位表示IO0_1 ~ IO0_4 各占一位（初值全 1 = 都没按下）。*/
static volatile uint16_t s_button_level = 0xFFFF;

/* SNTP 是否已校准（回调里置位UI 层只读） */
static volatile bool s_sntp_synced = false;

/* 按键电平读取回调（button 模块调用） */
static int get_button_level(int gpio)
{
    return (s_button_level & gpio) ? 1 : 0;
}

/* XL9555 电平跳变回调（扩展 IO 有输入变化时被调用）
 * ⚠ 这个回调运行在 xl9555 自己的任务上下文里不要在里面做耗时操作 */
static void xl9555_input_cb(uint16_t io_num, int level)
{
    if (level)
        s_button_level |= io_num;
    else
        s_button_level &= ~io_num;
}

/* 短按 */
static void short_press(int gpio)
{
    ESP_LOGI(TAG_BUTTON, "Button 0x%04X short press", gpio);
}

/* 长按：启动 AP 配网 */
static void long_press(int gpio)
{
    ESP_LOGI(TAG_BUTTON, "Button 0x%04X long press -> AP config", gpio);
    ap_wifi_apcfg(true);
}

/* ==========================================================================
 *  SNTP 成功从 NTP 服务器拿到时间、系统时钟更新完成ESP-IDF SNTP 组件自动调用
 * ========================================================================== */
static void sntp_finish_callback(struct timeval *tv)
{
    (void)tv;
    s_sntp_synced = true;

    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    if (t)
        ESP_LOGI(TAG_SNTP, "time synced: %04d-%02d-%02d %02d:%02d:%02d",
                 t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                 t->tm_hour, t->tm_min, t->tm_sec);
    /* 和 WiFi 那处保持对称：事件组可能还没创建（虽然实际顺序上不会）*/
    if (begin_event_group != NULL)
        xEventGroupSetBits(begin_event_group, sntp_connected_BIT);
}
// 你的 UI / 业务任务主动调用（轮询查询）   给别的模块查询：时间是否已经同步成功。
bool init_sntp_is_synced(void)
{
    return s_sntp_synced;
}

// WiFi 连上后在 wifi_state_callback 里面调用；不会在上电直接跑（没网解析不了 NTP 域名）
void init_sntp(void)
{
    /* 时区必须先设好否则同步回来的时间会差 8 小时 */
    setenv("TZ", "CST-8", 1);
    tzset();

    if (esp_sntp_enabled())
        return;

    ESP_LOGI(TAG_SNTP, "sntp init");
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_setservername(1, "time.asia.apple.com");
    esp_sntp_setservername(2, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(sntp_finish_callback);
    esp_sntp_init();
}

/* ==========================================================================
 *  WiFi 连接状态发生变化时wifi_manager 驱动自动回调
 * ========================================================================== */
/* WiFi 状态回调 */
/*  F9c：断线/放弃只播一次的「吸收器」（边沿不需要软件定时器）*/
static bool s_lost_announced = false;

static void wifi_state_callback(WIFI_STATE state)
{
    if (state == WIFI_STATE_CONNECTED)
    {
        ESP_LOGI(TAG_WIFI, "connected!");
        s_lost_announced = false;                    /* 连上 -> 复位（下次断开才能再播）*/
        if (begin_event_group != NULL)
        {
            xEventGroupSetBits(begin_event_group, wifi_connected_BIT);
        }
        init_sntp();
        onenet_start();
        msg_post(msg_src_system, "WIFI已连接");
        msg_post(msg_src_system, "ONET已连接");
        /* 这里原来还有一句 SNTP已同步已删掉：
           它是在「刚连上」就发的那会儿根本没同步（说谎）。
           真同步在 sntp_finish_callback 里报。*/
    }
    else if (state == WIFI_STATE_DISCONNECTED)
    {
        ESP_LOGW(TAG_WIFI, "disconnected");
        /*  F9c：断线播报
           注意：自动重连会来【6 个】DISCONNECTED
           -> 用 bool 吸收掉只播第 1 次（这就是「边沿」比定时器简单得多）*/
        if (!s_lost_announced)
        {
            s_lost_announced = true;
        }
    }
    else /* WIFI_STATE_GIVEUP：重试用完了 */
    {
        ESP_LOGW(TAG_WIFI, "重试用完放弃连接");
        if (!s_lost_announced)
        {
            s_lost_announced = true;
        }
        /* 提示用户：让他【自己】去点 WiFi 菜单重新配网（不是自动跳屏你定的）*/
        msg_post(msg_src_system, "WiFi连不上请点WiFi菜单重新配网");
    }
}
//**UI 层主动调用（查询 WiFi 是否在线）**
/* 给 UI 层用的强符号：覆盖 custom.c 里的弱实现 */
bool boot_wifi_connected(void)
{
    return wifi_manager_is_connect();
}

void init_wifi(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    wifi_manager_init(wifi_state_callback);

    /*  F9：开机【优先用 NVS 里存的】账号
       为什么这么改：
         esp_wifi_set_config() 之后WiFi 驱动会自动把配置写进 NVS
         （CONFIG_ESP_WIFI_NVS_ENABLED=y）
         -> 所以配网页/屏幕上配过的热点【一直存在里面】
       而原来的写法是每次开机都用写死的 lr. 去 set_config
         -> 把用户配好的顶掉了于是「配了也白配」*/
    wifi_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    if (esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK && cfg.sta.ssid[0] != '\0')
    {
        ESP_LOGI(TAG_WIFI, "从 NVS 读到账号：%s", (char *)cfg.sta.ssid);
        for (int k = 0; k < 5; k++)
        {
            if (wifi_manager_connect((char *)cfg.sta.ssid, (char *)cfg.sta.password) == ESP_OK)
                return;
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    else
    {
        ESP_LOGW(TAG_WIFI, "NVS 里没有账号（第一次开机？）用兜底");
    }

    /* 兜底：用编译时写死的那份（你定的）*/
    ESP_LOGW(TAG_WIFI, "兜底连接：%s", WIFI_SSID);
    for (int k = 0; k < 5; k++)
    {
        if (wifi_manager_connect(WIFI_SSID, WIFI_PASSWORD) == ESP_OK)
        {
            ESP_LOGI(TAG_WIFI, "connecting to %s ...", WIFI_SSID);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    ESP_LOGW(TAG_WIFI, "connect request failed after retries");
}

/* ==========================================================================
 *  对外接口
 * ========================================================================== */
void init_board(void)
{
    /* GPIO17 = XL9555 的中断输出脚（有输入跳变时会拉低）
       注意必须传 GPIO_NUM_17否则按键的中断通知收不到 */
    xl9555_init(BSP_XL9555_SDA, BSP_XL9555_SCL, GPIO_NUM_17, xl9555_input_cb);
    ESP_LOGI(TAG_BOARD, "XL9555 init done");

    /* 0 = 输出1 = 输入；把 LCD/BL/TP/功放 这几个脚配成输出 */
    xl9555_ioconfig((~(BSP_LCD_RST_IO | BSP_LCD_BL_IO |
                       BSP_TP_RST_IO | BSP_SPK_EN_IO)) &
                    0xFFFF);
    /* 背光先不开！等 LCD 初始化+清屏完之后再开否则复位期间会花屏闪一下 */
    xl9555_pin_write(BSP_LCD_BL_IO, 0);
    xl9555_pin_write(BSP_SPK_EN_IO, 0);
}

void init_button(void)
{
    button_config_t button_cfg =
        {
            .active_level = 0,
            .getlevel_cb = get_button_level,
            .gpio_num = IO0_1,
            .long_cb = long_press,
            .long_press_time = 3000,
            .short_cb = short_press,
        };
    button_event_set(&button_cfg);
    button_cfg.gpio_num = IO0_2;
    button_event_set(&button_cfg);
    button_cfg.gpio_num = IO0_3;
    button_event_set(&button_cfg);
    button_cfg.gpio_num = IO0_4;
    button_event_set(&button_cfg);

    ESP_LOGI(TAG_BUTTON, "button init done (IO0_1 ~ IO0_4)");
}

void init_all(void)
{
    /*  时间保存：先把上次的时间捞回来 （不然开机是 1970 ）*/
    time_save_init();

    /*  看门狗最先起来  这样后面的初始化如果卡住也能被抓住  */
    wdt_init();

    init_board();   /* 扩展 IO 最先后面很多脚靠它 */
    init_button();

    /*  灯模块（LED2 + WS2812）
       LED2（IO15）走 LEDC/PWM  可调亮度 
       WS2812（IO18 3）走 RMT  硬件发时序 
       ⚠️ 放在 init_board() 之后：上电时序要先稳定  */
    light_init();

    /*  语音第 0 步：麦克风自检 
       现在只是【每 500ms 打一次 RMS/峰峰值】 不影响别的功能 
        验收：安静时 峰峰 很小  对着说话 明显跳大 
       不过关就别往下搬模型 （模型再好麦克风不动也白搭 ）*/
    voice_mic_test_start();

    /*  语音模块已整体删除  顺手把功放【明确关掉】 免得悬空产生杂音  */
    xl9555_pin_write(BSP_SPK_EN_IO, 0);

    init_wifi();    /* 连上后会自动 init_sntp() */
    time_save_start_auto();   /*  SNTP 成功后自动存 NVS  每 10 分钟一次  */
}