#ifndef _TASK_BEGIN_H_
#define _TASK_BEGIN_H_
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include <stdbool.h>

void begin_task(void *arg);
void begin_event_create(void);

extern EventGroupHandle_t begin_event_group;

#define wifi_connected_BIT (1 << 0)
#define sntp_connected_BIT (1 << 1)
#define onenet_connected_BIT (1 << 2)

extern volatile bool wifi_success;
extern volatile bool sntp_success;
extern volatile bool onenet_success;
extern volatile bool begin_success;

bool get_wifi_bool(void);
bool get_sntp_bool(void);
bool get_begin_bool(void);
bool get_onenet_state(void);

#endif