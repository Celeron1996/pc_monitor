#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/portmacro.h"
#include "freertos/semphr.h"
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
static char _gpu_name_str[64];

static int _gpu_temperature = 0;
static int _cpu_temperature = 0;


/**
 * @brief 初始化
 * 
 */
void pc_init(void)
{

}


/**
 * @brief 解析json数据，得到基本数据
 * 
 */
void pc_analysis_json(void *pJson)
{
    //taskENTER_CRITICAL(0);

    // cJSON *json_root = cJSON_Parse(pJson);
    // if (json_root == NULL)
    // {
    //     ESP_LOGE(TAG, "json_root NULL");
    // }

    // cJSON *json_children = cJSON_GetObjectItem(json_root, "Children");
    // int children_size = cJSON_GetArraySize(json_children);
    
    // for (int i = 0; i < children_size; i++) {
    //     cJSON *item;
    //     item = cJSON_GetArrayItem(json_children, i);
    //     if (cJSON_GetArraySize(item) <= 1) {
    //         cJSON_Delete(item);
    //     }
    //     else {
    //         cJSON *host_name = cJSON_GetObjectItem(item, "Text");
    //         cJSON *sensors = cJSON_GetObjectItem(item, "Children");
    //         cJSON *cpu = cJSON_GetArrayItem(sensors, 1);
    //         cJSON *cpu_name = cJSON_GetObjectItem(cpu, "Text");
    //         cJSON *cpu_sensor = cJSON_GetObjectItem(cpu, "Children");
    //         cJSON *cpu_temperature = cJSON_GetArrayItem(cpu_sensor, 1);
    //         cJSON *cpu_temper_children = cJSON_GetObjectItem(cpu_temperature, "Children");
    //         cJSON *cpu_temper_package = cJSON_GetArrayItem(cpu_temper_children, 4);//
    //         cJSON *cpu_temper_package_value = cJSON_GetObjectItem(cpu_temper_package, "Value");

    //         memcpy(_pc_name_str, host_name->valuestring,
    //                 strlen(host_name->valuestring)>sizeof(_pc_name_str)?sizeof(_pc_name_str):strlen(host_name->valuestring));/*获取主机名字*/

    //         memcpy(_cpu_name_str, cpu_name->valuestring,
    //                 strlen(cpu_name->valuestring)>sizeof(_cpu_name_str)?sizeof(_pc_name_str):strlen(cpu_name->valuestring));/*获取cpu名字*/

    //         _cpu_temperature = pc_str_to_int(cpu_temper_package_value->valuestring);

    //         ESP_LOGI(TAG, "children_size = %d, i = %d, name = %s", children_size, i, _pc_name_str);
    //     }
    // }

    // cJSON_Delete(json_root);

    /*-------------------------------------begin---------------------------------------------------*/
    cJSON *root = cJSON_Parse(pJson);
    cJSON *children = cJSON_GetObjectItem(root, "Children");
    cJSON *pc = cJSON_GetArrayItem(children, 0);
    cJSON *pc_name = cJSON_GetObjectItem(pc, "Text");
    ESP_LOGI(TAG, "pc name = %s", pc_name->valuestring);
    cJSON *pc_sensors = cJSON_GetObjectItem(pc, "Children");
    int sensor_count = cJSON_GetArraySize(pc_sensors);
    for (int i = 0 ; i < sensor_count; i++)
    {
        cJSON *cpu = cJSON_GetArrayItem(pc_sensors, i);
        cJSON *cpu_image = cJSON_GetObjectItem(cpu, "ImageURL");
        if ((cpu_image != NULL) && (strcmp(cpu_image->valuestring, "images_icon/cpu.png") == 0))
        {
            cJSON *cpu_name = cJSON_GetObjectItem(cpu, "Text");
            cJSON *cpu_aattributes = cJSON_GetObjectItem(cpu, "Children");
            int cpu_aattributes_count = cJSON_GetArraySize(cpu_aattributes);
            for (int x = 0; x < cpu_aattributes_count; x++)
            {
                cJSON *cpu_temperatures = cJSON_GetArrayItem(cpu_aattributes, x);
                cJSON *cpu_temperatures_text = cJSON_GetObjectItem(cpu_temperatures, "Text");
                if ((cpu_temperatures_text != NULL) && (strcmp(cpu_temperatures_text->valuestring, "Temperatures") == 0))
                {
                    cJSON *cpu_items = cJSON_GetObjectItem(cpu_temperatures, "Children");
                    int cpu_items_count = cJSON_GetArraySize(cpu_items);
                    cJSON *cpu_package = cJSON_GetArrayItem(cpu_items, cpu_items_count - 1);
                    cJSON *cpu_package_temperature = cJSON_GetObjectItem(cpu_package, "Value");
                    ESP_LOGI(TAG, "cpu temperature:%s", cpu_package_temperature->valuestring);
                }
            }
        }
    }

    cJSON_Delete(root);

    /*-------------------------------------end---------------------------------------------------*/

    //taskEXIT_CRITICAL(0);
}


/**
 * @brief 获取cpu温度
 * @return 返回cpu温度
 */
int pc_get_cpu_temperature(void)
{
    return _cpu_temperature;
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
