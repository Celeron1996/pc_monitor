#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_freertos_hooks.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

#include "esp_system.h"

#include "esp_log.h"

#include "watchdog.h"

static const char *TAG = "watchdog";


static void vTimerCallback( TimerHandle_t xTimer );


static TimerHandle_t timer_handle;


void watchdog_task(void *pvParameter)
{
	timer_handle = xTimerCreate("watchdog_timer",
															WATCHDOG_PERIOD,
															pdTRUE,
															(void *)0,
															vTimerCallback
															);

	xTimerStart(timer_handle, 0);															

	vTaskDelete(NULL);															
}


static void vTimerCallback( TimerHandle_t xTimer )
{
	ESP_LOGI(TAG, "watchdog task esp restart");
	esp_restart();
}

void watchdog_reset(void)
{
	xTimerReset(timer_handle, 0);
}


