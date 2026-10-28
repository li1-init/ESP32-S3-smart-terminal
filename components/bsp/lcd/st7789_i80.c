#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_commands.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "st7789_i80.h"
#include <string.h>
#include "xl9555.h"

static const char* TAG = "st7789";

//lcd操作句柄
static esp_lcd_panel_io_handle_t lcd_io_handle = NULL;

static esp_lcd_panel_handle_t   lcd_handle = NULL;

//刷新完成回调函数
static lcd_flush_done_cb    s_flush_done_cb = NULL;

//背光GPIO
static gpio_num_t   s_bl_gpio = -1;

#define LCD_RST_IO           IO1_3
#define LCD_BL_IO            IO1_2


/** st7789初始化
 * @param st7789_cfg_t  接口参数
 * @return 成功或失败
*/
esp_err_t st7789_i80_hw_init(st7789_i80_cfg_t* cfg)
{
    
    esp_lcd_i80_bus_handle_t i80_bus = NULL;
    esp_lcd_i80_bus_config_t bus_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .dc_gpio_num = cfg->rs,
        .wr_gpio_num = cfg->wr,
        .bus_width = cfg->bus_width,
        .max_transfer_bytes = cfg->height * cfg->width* sizeof(uint16_t),
        .dma_burst_size = 64,
    };
    memcpy(&bus_config.data_gpio_nums[0],&cfg->data[0],cfg->bus_width*sizeof(bus_config.data_gpio_nums[0]));
    ESP_ERROR_CHECK(esp_lcd_new_i80_bus(&bus_config, &i80_bus));
    esp_lcd_panel_io_i80_config_t io_config = {
        .cs_gpio_num = cfg->cs,
        .pclk_hz = 10000000,
        .trans_queue_depth = 10,
        .dc_levels = {
            .dc_idle_level = 0,
            .dc_cmd_level = 0,
            .dc_dummy_level = 0,
            .dc_data_level = 1,
        },
        .flags = {
            .swap_color_bytes = 1, // Swap can be done in LvGL (default) or DMA
        },
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i80(i80_bus, &io_config, &lcd_io_handle));
    s_flush_done_cb = cfg->done_cb; //设置刷新完成回调函数
    
    if(cfg->bl > 0)
    {
        s_bl_gpio = cfg->bl;    //设置背光GPIO
        //初始化GPIO(BL)
        gpio_config_t bl_gpio_cfg = 
        {
            .pull_up_en = GPIO_PULLUP_DISABLE,          //禁止上拉
            .pull_down_en = GPIO_PULLDOWN_DISABLE,      //禁止下拉
            .mode = GPIO_MODE_OUTPUT,                   //输出模式
            .intr_type = GPIO_INTR_DISABLE,             //禁止中断
            .pin_bit_mask = (1<<cfg->bl)                //GPIO脚
        };
        gpio_config(&bl_gpio_cfg);
    }
    //初始化复位脚
    if(cfg->rst > 0)
    {
        gpio_config_t rst_gpio_cfg = 
        {
            .pull_up_en = GPIO_PULLUP_DISABLE,          //禁止上拉
            .pull_down_en = GPIO_PULLDOWN_DISABLE,      //禁止下拉
            .mode = GPIO_MODE_OUTPUT,                   //输出模式
            .intr_type = GPIO_INTR_DISABLE,             //禁止中断
            .pin_bit_mask = (1<<cfg->rst)                //GPIO脚
        };
        gpio_config(&rst_gpio_cfg);
    }

    ESP_LOGI(TAG, "Install LCD driver of st7789");
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = GPIO_NUM_NC,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(lcd_io_handle, &panel_config, &lcd_handle));
    esp_lcd_panel_reset(lcd_handle);
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_lcd_panel_init(lcd_handle);
    #if 0
    esp_lcd_panel_io_tx_param(lcd_io_handle, 0xb2, (uint8_t[]) {0x0c,0x0c,0x00,0x33,0x33},5);
    esp_lcd_panel_io_tx_param(lcd_io_handle, 0xb7, (uint8_t[]) {0x35},1);
    esp_lcd_panel_io_tx_param(lcd_io_handle, 0xbb, (uint8_t[]) {0x28},1);
    esp_lcd_panel_io_tx_param(lcd_io_handle, 0xc0, (uint8_t[]) {0x2C},1);
    esp_lcd_panel_io_tx_param(lcd_io_handle, 0xc2, (uint8_t[]) {0x01},1);
    esp_lcd_panel_io_tx_param(lcd_io_handle, 0xc3, (uint8_t[]) {0x0b},1);
    esp_lcd_panel_io_tx_param(lcd_io_handle, 0xc4, (uint8_t[]) {0x20},1);
    esp_lcd_panel_io_tx_param(lcd_io_handle, 0xc6, (uint8_t[]) {0x0f},1);
    esp_lcd_panel_io_tx_param(lcd_io_handle, 0xd0, (uint8_t[]) {0xa4,0xa1},2);

    esp_lcd_panel_io_tx_param(lcd_io_handle, 0xe0, (uint8_t[]) {0xd0,0x01,0x08,0x0f,0x11,0x2a,0x36,0x55,0x44,0x3a,0x0b,0x06,0x11,0x20},14);
    esp_lcd_panel_io_tx_param(lcd_io_handle, 0xe1, (uint8_t[]) {0xd0,0x02,0x07,0x0a,0x0b,0x18,0x34,0x43,0x4a,0x2b,0x1b,0x1c,0x22,0x1f},14);
    #endif

   

    switch(cfg->spin)
    {
        case ST7789_SPIN_0:
            esp_lcd_panel_swap_xy(lcd_handle, false);
            esp_lcd_panel_mirror(lcd_handle, false, false); 
            break;
        case ST7789_SPIN_90:
            esp_lcd_panel_swap_xy(lcd_handle, true);
            esp_lcd_panel_mirror(lcd_handle, false, true);
            break;
        case ST7789_SPIN_180:
            esp_lcd_panel_swap_xy(lcd_handle, false);
            esp_lcd_panel_mirror(lcd_handle, true, true);
            break;
        case ST7789_SPIN_270:
            esp_lcd_panel_swap_xy(lcd_handle, true);
            esp_lcd_panel_mirror(lcd_handle, true, false);
            break;
        default:break;
    }
    esp_lcd_panel_disp_on_off(lcd_handle, true);

    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = cfg->done_cb,
    };
    /* Register done callback */
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(lcd_io_handle, &cbs, cfg->cb_param));

    return ESP_OK;
}

/** st7789写入显示数据
 * @param x1,x2,y1,y2:显示区域
 * @return 无
*/
void st7789_i80_flush(int x1,int x2,int y1,int y2,uint8_t *data)
{
    esp_lcd_panel_draw_bitmap(lcd_handle, x1, y1, x2 + 1, y2 + 1, data);
}

void st7789_i80_clear(int width,int height)
{
    const int clear_line = 20;
    int clear_len = width*clear_line*sizeof(uint16_t);
    uint8_t *clear_data = (uint8_t*)malloc(clear_len);
    int begin_y = 0;
    int end_y = clear_line;
    if(!clear_data)
    {
        ESP_LOGE(TAG,"No enought memory to clear lcd");
        return;
    }
    memset(clear_data,0,clear_len);
    for(int i = 0;i < clear_line;i++)
    {
        st7789_i80_flush(0,width,begin_y,end_y,clear_data);
        vTaskDelay(pdMS_TO_TICKS(5));
        begin_y += clear_line;
        end_y += clear_line;
    }
    free(clear_data);
}

/** 控制背光
 * @param enable 是否使能背光
 * @return 无
*/
void st7789_i80_lcd_backlight(bool enable)
{
    if(enable)
    {
        //gpio_set_level(s_bl_gpio,1);
        xl9555_pin_write(LCD_BL_IO,1);
    }
    else
    {
        //gpio_set_level(s_bl_gpio,0);
        xl9555_pin_write(LCD_BL_IO,0);
    }
}
