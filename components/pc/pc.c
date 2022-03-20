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
static char _ram_load_str[16];
static char _cpu_load_str[16];
static char _gpu_load_str[16];

static int _gpu_temperature = 0;
static int _cpu_temperature = 0;
static int _ram_load = 0;
static int _cpu_load = 0;
static int _gpu_load = 0;


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

    /*-------------------------------------begin---------------------------------------------------*/
    cJSON *root = cJSON_Parse(pJson);
    cJSON *children = cJSON_GetObjectItem(root, "Children");
    cJSON *pc = cJSON_GetArrayItem(children, 0);
    cJSON *pc_name = cJSON_GetObjectItem(pc, "Text");
    cJSON *pc_sensors = cJSON_GetObjectItem(pc, "Children");
    int sensor_count = cJSON_GetArraySize(pc_sensors);
    for (int i = 0 ; i < sensor_count; i++)
    {
        /*================================ 遍历查找cpu 属性 begin ======================================*/
        cJSON *cpu = cJSON_GetArrayItem(pc_sensors, i);
        cJSON *cpu_image = cJSON_GetObjectItem(cpu, "ImageURL");
        if ((cpu_image != NULL) && (strcmp(cpu_image->valuestring, "images_icon/cpu.png") == 0))
        {
            cJSON *cpu_name = cJSON_GetObjectItem(cpu, "Text");
            cJSON *cpu_children = cJSON_GetObjectItem(cpu, "Children");
            int cpu_children_count = cJSON_GetArraySize(cpu_children);
            for (int x = 0; x < cpu_children_count; x++)
            {
                /* cpu 温度 */
                cJSON *cpu_temperatures = cJSON_GetArrayItem(cpu_children, x);
                cJSON *cpu_temperatures_text = cJSON_GetObjectItem(cpu_temperatures, "Text");
                if ((cpu_temperatures_text != NULL) && (strcmp(cpu_temperatures_text->valuestring, "Temperatures") == 0))
                {
                    cJSON *cpu_temper_children = cJSON_GetObjectItem(cpu_temperatures, "Children");
                    int cpu_temper_children_count = cJSON_GetArraySize(cpu_temper_children);
                    cJSON *cpu_package = cJSON_GetArrayItem(cpu_temper_children, cpu_temper_children_count - 1);
                    cJSON *cpu_package_temperature_value = cJSON_GetObjectItem(cpu_package, "Value");

                    memcpy(_pc_name_str, pc_name->valuestring,
                            strlen(pc_name->valuestring)>sizeof(_pc_name_str)?sizeof(_pc_name_str):strlen(pc_name->valuestring));/*获取主机名字*/
                    memcpy(_cpu_name_str, cpu_name->valuestring,
                            strlen(cpu_name->valuestring)>sizeof(_cpu_name_str)?sizeof(_pc_name_str):strlen(cpu_name->valuestring));/*获取cpu名字*/
                    _cpu_temperature = pc_str_to_int(cpu_package_temperature_value->valuestring);/*获取cpu package温度*/
                }

                /* cpu 使用率 */
                cJSON *cpu_load = cJSON_GetArrayItem(cpu_children, x);
                cJSON *cpu_load_text = cJSON_GetObjectItem(cpu_load, "Text");
                if ((cpu_load_text != NULL) && (strcmp(cpu_load_text->valuestring, "Load") == 0))
                {
                    cJSON *cpu_load_children = cJSON_GetObjectItem(cpu_load, "Children");
                    cJSON *cpu_load_total = cJSON_GetArrayItem(cpu_load_children, 0);
                    if (cpu_load_total != NULL)
                    {
                        cJSON *cpu_load_total_value = cJSON_GetObjectItem(cpu_load_total, "Value");
                        if (cpu_load_total_value != NULL)
                        {
                            memcpy(_cpu_load_str, cpu_load_total_value->valuestring,
                                    strlen(cpu_load_total_value->valuestring)>sizeof(_cpu_load_str)?sizeof(_cpu_load_str):strlen(cpu_load_total_value->valuestring));
                            _cpu_load = pc_str_to_int(cpu_load_total_value->valuestring);
                        }

                    }
                }
            }
        }
        /*================================ 遍历查找cpu 属性 end ======================================*/

        /*================================ 遍历查找ram 属性 end ======================================*/
        cJSON *ram = cJSON_GetArrayItem(pc_sensors, i);
        cJSON *ram_image = cJSON_GetObjectItem(ram, "ImageURL");
        if ((ram_image != NULL) && (strcmp(ram_image->valuestring, "images_icon/ram.png") == 0))
        {
            cJSON *ram_children = cJSON_GetObjectItem(ram, "Children");
            int ram_children_count = cJSON_GetArraySize(ram_children);
            for (int x = 0; x < ram_children_count; x++)
            {
                cJSON *ram_load = cJSON_GetArrayItem(ram_children, x);
                cJSON *ram_load_text = cJSON_GetObjectItem(ram_load, "Text");
                if ((ram_load_text != NULL) && (strcmp(ram_load_text->valuestring, "Load") == 0))
                {
                    cJSON *ram_load_value = cJSON_GetObjectItem(ram_load, "Children");
                    if(ram_load_value != NULL)
                    {
                        ram_load_value = cJSON_GetArrayItem(ram_load_value, 0);
                        ram_load_value = cJSON_GetObjectItem(ram_load_value, "Value");
                        if(ram_load_value != NULL)
                        {
                            memcpy(_ram_load_str, ram_load_value->valuestring,
                                    strlen(ram_load_value->valuestring)>sizeof(_ram_load_str)?sizeof(_ram_load_str):strlen(ram_load_value->valuestring));
                            _ram_load = pc_str_to_int(ram_load_value->valuestring);
                        }
                    }
                }
            }
        }
    }

    cJSON_Delete(root);

    ESP_LOGI(TAG, "pc name:[%s] cpu name:[%s] cpu temper:[%d°C] ram load:[%s,int=%d] cpu load:[%s,int=%d]",
            _pc_name_str, _cpu_name_str, _cpu_temperature, _ram_load_str, _ram_load, _cpu_load_str, _cpu_load);

    /*-------------------------------------end---------------------------------------------------*/

    //taskEXIT_CRITICAL(0);
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
        if (pStr[index] >= '0' && pStr[index] <= '9')
        {
            result = result * 10 + (pStr[index] - '0');
        }
        else if (pStr[index] == '.')    /*小数点后面的不要了，取整*/
        {
            break;
        }
        index++;
    }

    return result;
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
 * @brief 获取gpu温度
 * @return 返回gpu温度
 */
int pc_get_gpu_temperature(void)
{
    return _gpu_temperature;
}

/**
 * @brief 获取内存使用率
 * @return 返回内存使用率
 */
int pc_get_ram_load(void)
{
    return _ram_load;
}

/**
 * @brief 获取cpu使用率
 * @return 返回cpu使用率
 */
int pc_get_cpu_load(void)
{
    return _cpu_load;
}

/**
 * @brief 获取gpu使用率
 * @return 返回gpu使用率
 */
int pc_get_gpu_load(void)
{
    return _gpu_load;
}
