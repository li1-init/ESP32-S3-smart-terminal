#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"

#include "board_config.h"
#include "lv_port.h"
#include "st7789_i80.h" 
#include "ft6336u_driver.h"
#include "xl9555.h"

/* ⚠ 这里不能用 TAG 这个名字！
 * 因为开了 CONFIG_LV_USE_PRIVATE_API 之后lvgl.h 会顺带引入 lvgl_private.h
 * 里面（lv_ft81x_defines.h）定义了 LVGL 自己的宏 TAG(s)
 * 我们再 #define TAG "lv_port" 就会撞名  -Werror 直接编译失败。
 * 所以改名叫 TAG_PORT。 */
#define TAG_PORT "lv_port"

static lv_display_t     *s_disp;
static SemaphoreHandle_t s_lvgl_lock;

/* ==================== 显示 ==================== */
static bool lcd_flush_done(esp_lcd_panel_io_handle_t panel_io,
                              esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_display_flush_ready(s_disp);
    return false;
}

static void disp_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    st7789_i80_flush(area->x1, area->x2, area->y1, area->y2, px_map);
}

static void lv_disp_init(void)
{
    /* 显示缓冲：宽 x (高/8)放内部 DMA 内存 */
    const size_t buf_px = BSP_LCD_WIDTH * (BSP_LCD_HEIGHT / 8);
    lv_color_t *buf = heap_caps_malloc(buf_px * sizeof(lv_color_t),
                                       MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!buf) {
        ESP_LOGE(TAG_PORT, "display buffer malloc failed!");
        return;
    }
    s_disp = lv_display_create(BSP_LCD_WIDTH, BSP_LCD_HEIGHT);
    lv_display_set_flush_cb(s_disp, disp_flush_cb);
    lv_display_set_buffers(s_disp, buf, NULL, buf_px * sizeof(lv_color_t),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
}

/* ==================== 触摸 ==================== */
static void indev_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    int16_t x, y;
    int state;
    ft6336u_read(&x, &y, &state);
    /* 屏幕顺时针旋转了 90°触摸坐标要映射回去 */
    data->point.x = BSP_LCD_WIDTH - y;
    data->point.y = x;
    data->state   = state;
}

static void lv_indev_init(void)
{
    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, indev_read_cb);
}

/* ==================== ST7789 硬件 ==================== */
static void st7789_hw_init(void)
{
    /* LCD / 触摸的复位脚挂在 XL9555 上 */
    xl9555_pin_write(BSP_LCD_RST_IO | BSP_TP_RST_IO, 0);
    vTaskDelay(pdMS_TO_TICKS(200));
    xl9555_pin_write(BSP_LCD_RST_IO | BSP_TP_RST_IO, 1);
    vTaskDelay(pdMS_TO_TICKS(200));

    int data_pins[] = BSP_LCD_DATA_PINS;

    st7789_i80_cfg_t cfg = {
        .cs         = BSP_LCD_CS,
        .rs         = BSP_LCD_RS,
        .wr         = BSP_LCD_WR,
        .rst        = GPIO_NUM_NC,
        .bl         = GPIO_NUM_NC,
        .bus_width  = BSP_LCD_BUS_WIDTH,
        .width      = BSP_LCD_WIDTH,
        .height     = BSP_LCD_HEIGHT,
        .spin       = ST7789_SPIN_90,
        .done_cb    = lcd_flush_done,
        .cb_param   = &s_disp,
    };
    memcpy(cfg.data, data_pins, sizeof(data_pins));

    st7789_i80_hw_init(&cfg);
    ESP_LOGI(TAG_PORT, "ST7789 (I80) hardware init done");
}

/* ==================== FT6336U 硬件 ==================== */
static esp_err_t ft6336u_hw_init(void)
{
    ft6336u_cfg_t cfg = {
        .scl     = BSP_TP_SCL,
        .sda     = BSP_TP_SDA,
        .fre     = BSP_TP_FREQ_HZ,
        .x_limit = BSP_LCD_HEIGHT,
        .y_limit = BSP_LCD_WIDTH,
    };
    return ft6336u_init(&cfg);
}

/* ==================== LVGL tick ==================== */
static void lv_tick_cb(void *arg)
{
    lv_tick_inc(*(uint32_t *)arg);
}

static void lv_tick_init(void)
{
    static uint32_t tick_ms = 5;
    const esp_timer_create_args_t args = {
        .arg                   = &tick_ms,
        .callback              = lv_tick_cb,
        .name                  = "lv_tick",
        .dispatch_method       = ESP_TIMER_TASK,
        .skip_unhandled_events = true,
    };
    esp_timer_handle_t h;
    esp_timer_create(&args, &h);
    esp_timer_start_periodic(h, tick_ms * 1000);
}

/* ==================== 对外接口 ==================== */
void lv_port_init(void)
{
    s_lvgl_lock = xSemaphoreCreateRecursiveMutex();

    lv_init();
    st7789_hw_init();
    lv_disp_init();
    st7789_i80_clear(BSP_LCD_WIDTH, BSP_LCD_HEIGHT);

    if (ft6336u_hw_init() == ESP_OK) {
        lv_indev_init();
    } else {
        ESP_LOGW(TAG_PORT, "FT6336U init failed, touch disabled");
    }

    lv_tick_init();
    ESP_LOGI(TAG_PORT, "LVGL port ready (%dx%d)", BSP_LCD_WIDTH, BSP_LCD_HEIGHT);
}

void lv_port_lock(void) { xSemaphoreTakeRecursive(s_lvgl_lock, portMAX_DELAY); }
void lv_port_unlock(void) { xSemaphoreGiveRecursive(s_lvgl_lock); }
