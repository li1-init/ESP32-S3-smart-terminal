#ifndef _ST7789_I80_H_
#define _ST7789_I80_H_
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_lcd_types.h"

typedef bool(*lcd_flush_done_cb)(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx);

//屏的放置方向
typedef enum
{
    ST7789_SPIN_0,            //不旋转
    ST7789_SPIN_90,           //顺时针90
    ST7789_SPIN_180,          //顺时针180
    ST7789_SPIN_270,          //顺时针270
}ST7789_SPIN;

typedef struct
{
    gpio_num_t  cs;         //片选
    gpio_num_t  rs;         //命令
    gpio_num_t  wr;         //写使能
    gpio_num_t  rst;        //复位
    gpio_num_t  bl;         //背光
    int         data[16];   //数据线IO
    size_t      bus_width;  //数据位宽
    uint16_t    width;      //宽
    uint16_t    height;     //长
    ST7789_SPIN     spin;       //选择方向(0不旋转，1顺时针旋转90, 2旋转180，3顺时针旋转270)
    lcd_flush_done_cb   done_cb;    //数据传输完成回调函数
    void*       cb_param;   //回调函数参数
}st7789_i80_cfg_t;


/** st7789初始化
 * @param st7789_cfg_t  接口参数
 * @return 成功或失败
*/
esp_err_t st7789_i80_hw_init(st7789_i80_cfg_t* cfg);

/** st7789写入显示数据
 * @param x1,x2,y1,y2:显示区域
 * @return 无
*/
void st7789_i80_flush(int x1,int x2,int y1,int y2,uint8_t *data);

void st7789_i80_clear(int width,int height);

/** 控制背光
 * @param enable 是否使能背光
 * @return 无
*/
void st7789_i80_lcd_backlight(bool enable);

#endif
