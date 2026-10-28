#ifndef _ONENET_DM_H_
#define _ONENET_DM_H_
#include "cJSON.h"

/*  OneNET 物模型（Data Model） 已按我们的传感器改写 
   原来的版本是【灯的】（LightSwitch/Brightness/RGBColor  还依赖 led_ws2812 ）
   现在只有两个属性：
       temp   温度  float  ℃
       light  光照  int32  %
   ⚠️ 这两个标识符必须和 OneNET 控制台上【一字不差】（大小写敏感 ）*/

void   onenet_dm_init(void);                      /* 初始化（打印一下 ）*/
cJSON* onenet_property_upload_dm(void);           /*  拼【上报】的 JSON  */
void   onenet_property_handle(cJSON* property_js);/*  处理【下发】的 JSON （if/else 在这里写 ）*/

#endif