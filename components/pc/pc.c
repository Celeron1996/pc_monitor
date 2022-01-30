#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "oled.h"
#include "wifi_wizard.h"
#include "esp_err.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"
#include <lwip/netdb.h>

#include "cJSON.h"
#include "pc.h"



static char *pJSON = NULL;  /*存储json的指针*/
static void pc_task(void *pvParameters);    /*pc任务，获取json*/
static char *URL = NULL;    /*用于存储外部传进来的pc http server 的url*/


/**
 * @brief 创建一个线程，使用http client获取pc的data.json
 * @param url:data.json的位置
 */
eps_err_t pc_init(const char *url)
{
    xTaskCreate(pc_task, "pc_task", 4096, (void*)AF_INET, 6, NULL);
}

/**
 * @brief 外部获取pc的json数据
 * 
 */
char *const pc_get_json(void)
{
    return pJSON;
}

/**
 * @brief 线程：获取pc的json
 * 
 */
static void pc_task(void *pvParameters)
{

}