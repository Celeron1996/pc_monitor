#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/portmacro.h"
#include "esp_system.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_crt_bundle.h"

#include "esp_http_client.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"
#include <lwip/netdb.h>

#include "cJSON.h"
#include "pc.h"


#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"

#include "esp_http_client.h"

#include "cJSON.h"

static char *TAG = "PC";

static int pc_str_to_int(char *pStr);

static char _pc_name_str[64];
static char _cpu_name_str[64];
static char _cpu_temperature_str[16];
static char _gpu_name_str[64];
static char _gpu_temperature_str[16];


/**
 * @brief 解析json数据，得到基本数据
 * 
 */
void pc_analysis_json(void *pJson)
{
    cJSON *json_root = cJSON_Parse(pJson);
    if (json_root == NULL)
    {
        ESP_LOGE(TAG, "json_root NULL");
    }
        
    cJSON *json_children = cJSON_GetObjectItem(json_root, "Children");
    int children_size = cJSON_GetArraySize(json_children);
    
    for (int i = 0; i < children_size; i++) {
        cJSON *item;
        item = cJSON_GetArrayItem(json_children, i);
        if (cJSON_GetArraySize(item) <= 1) {
            cJSON_Delete(item);
        }
        else {
            cJSON *host_name = cJSON_GetObjectItem(item, "Text");
            cJSON *sensors = cJSON_GetObjectItem(item, "Children");
            cJSON *cpu = cJSON_GetArrayItem(sensors, 1);
            cJSON *cpu_name = cJSON_GetObjectItem(cpu, "Text");
            cJSON *cpu_sensor = cJSON_GetObjectItem(cpu, "Children");
            cJSON *cpu_temperature = cJSON_GetArrayItem(cpu_sensor, 1);
            cJSON *cpu_temper_children = cJSON_GetObjectItem(cpu_temperature, "Children");
            cJSON *cpu_temper_package = cJSON_GetArrayItem(cpu_temper_children, 4);//
            cJSON *cpu_temper_package_value = cJSON_GetObjectItem(cpu_temper_package, "Value");

            ESP_LOGI(TAG, "host name = %s, cpu name = %s, cpu tempera = %s", host_name->valuestring, cpu_name->valuestring, cpu_temper_package_value->valuestring);

            memcpy(_pc_name_str, host_name->valuestring, strlen(host_name->valuestring));/*获取主机名字*/
            memcpy(_cpu_name_str, cpu_name->valuestring, strlen(cpu_name->valuestring));/*获取cpu名字*/
            memcpy(_cpu_temperature_str, cpu_temper_package_value->valuestring, strlen(cpu_temper_package_value->valuestring));/*获取cpu温度*/

        }
    }

    cJSON_Delete(json_root);
}


/**
 * @brief 获取cpu温度
 * @return 返回cpu温度
 */
int pc_get_cpu_temperature(void)
{
    return pc_str_to_int(_cpu_temperature_str);
}



/**
 * @brief 字符串转整数
 * @param   pStr:字符串指针
 * @return 整数
 */
static int pc_str_to_int(char *pStr)
{
    int index = 0;
    int result = 0;

    while (pStr[index] != '\0')
    {
        if (pStr[index] > '0' && pStr[index] <= '9')
        {
            result = result * 10 + (pStr[index] - '0');
        }
        index++;
    }

    return result;
}
