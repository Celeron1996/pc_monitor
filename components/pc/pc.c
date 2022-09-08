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

#define VAR_TO_STR(STRING__)	#STRING__

static char *TAG = "PC";

static int pc_str_to_int(char *pStr);

static char _pc_name_str[64];
static char _cpu_name_str[64];
static char _gpu_name_str[64];
static char _mainboard_name_str[64];


static char _ram_load_str[16] = "0%";
static char _cpu_load_str[16] = "0%";
static char _gpu_load_str[16] = "0%";
static char _gpu_temperature_str[16] = "0°C";
static char _cpu_temperature_str[16] = "0°C";


static int _gpu_temperature = 0;
static int _cpu_temperature = 0;
static int _ram_load = 0;
static int _cpu_load = 0;
static int _gpu_load = 0;

static bool _cpu_temperature_unit_celsius = true;
static bool _gpu_temperature_unit_celsius = true;


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
#define PC_CJSON_CHECK(value)	do{if(value == NULL){goto EXIT;}}while(0)
void pc_analysis_json(void *pJson)
{
    taskENTER_CRITICAL(0);

    /*-------------------------------------begin---------------------------------------------------*/
    cJSON *root = cJSON_Parse(pJson);PC_CJSON_CHECK(root);
    cJSON *children = cJSON_GetObjectItem(root, "Children");PC_CJSON_CHECK(children);
    cJSON *pc = cJSON_GetArrayItem(children, 0);PC_CJSON_CHECK(pc);
    cJSON *pc_name = cJSON_GetObjectItem(pc, "Text");PC_CJSON_CHECK(pc_name);
    cJSON *pc_sensors = cJSON_GetObjectItem(pc, "Children");PC_CJSON_CHECK(pc_sensors);
    int sensor_count = cJSON_GetArraySize(pc_sensors);

    memset(_pc_name_str, 0, sizeof(_pc_name_str));
    memset(_mainboard_name_str, 0, sizeof(_mainboard_name_str));
    memset(_cpu_name_str, 0, sizeof(_cpu_name_str));
    memset(_gpu_name_str, 0, sizeof(_gpu_name_str));
    memset(_cpu_temperature_str, 0, sizeof(_cpu_temperature_str));
    memset(_gpu_temperature_str, 0, sizeof(_gpu_temperature_str));
    memset(_cpu_load_str, 0, sizeof(_cpu_load_str));
    memset(_gpu_load_str, 0, sizeof(_gpu_load_str));
    memset(_ram_load_str, 0, sizeof(_ram_load_str));

    memcpy(_pc_name_str, pc_name->valuestring,
            strlen(pc_name->valuestring)>sizeof(_pc_name_str)?sizeof(_pc_name_str):strlen(pc_name->valuestring));/*获取主机名字*/

    for (int i = 0 ; i < sensor_count; i++)
    {

				/*================================ 遍历查找mainboard 属性 begin ======================================*/
				cJSON *mainboard = cJSON_GetArrayItem(pc_sensors, i);
				cJSON *mainboard_image = cJSON_GetObjectItem(mainboard, "ImageURL");
				if ((mainboard_image != NULL) && (strcmp(mainboard_image->valuestring, "images_icon/mainboard.png") == 0))
				{				
					cJSON *mainboard_load_text = cJSON_GetObjectItem(mainboard, "Text");
					if (mainboard_load_text != NULL)
					{
						memcpy(_mainboard_name_str, mainboard_load_text->valuestring,
							strlen(mainboard_load_text->valuestring)>sizeof(_mainboard_name_str)?sizeof(_mainboard_name_str):strlen(mainboard_load_text->valuestring));
					}
						
				}
				/*================================ 遍历查找mainboard 属性 end ======================================*/

        /*================================ 遍历查找cpu 属性 begin ======================================*/
        cJSON *cpu = cJSON_GetArrayItem(pc_sensors, i);
        cJSON *cpu_image = cJSON_GetObjectItem(cpu, "ImageURL");
        if ((cpu_image != NULL) && (strcmp(cpu_image->valuestring, "images_icon/cpu.png") == 0))
        {
            cJSON *cpu_name = cJSON_GetObjectItem(cpu, "Text");
            cJSON *cpu_children = cJSON_GetObjectItem(cpu, "Children");
            memcpy(_cpu_name_str, cpu_name->valuestring,
                    strlen(cpu_name->valuestring)>sizeof(_cpu_name_str)?sizeof(_cpu_name_str):strlen(cpu_name->valuestring));/*获取cpu名字*/

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

                    memcpy(_cpu_temperature_str, cpu_package_temperature_value->valuestring,
                            strlen(cpu_package_temperature_value->valuestring)>sizeof(_cpu_temperature_str)?sizeof(_cpu_temperature_str):strlen(cpu_package_temperature_value->valuestring));
                    _cpu_temperature = pc_str_to_int(cpu_package_temperature_value->valuestring);/*获取cpu package温度*/

                    if (_cpu_temperature_str[strlen(_cpu_temperature_str) - 1] == 'C')  /*温度单位是摄氏度还是华氏度*/
                    {
                        _cpu_temperature_unit_celsius = true;
                    }
                    else
                    {
                        _cpu_temperature_unit_celsius = false;
                    }
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

        /*================================ 遍历查找ram 属性 begin ======================================*/
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
        /*================================ 遍历查找ram 属性 end ======================================*/

        /*================================ 遍历查找gpu 属性 begin ======================================*/
        cJSON *gpu = cJSON_GetArrayItem(pc_sensors, i);
        cJSON *gpu_image = cJSON_GetObjectItem(gpu, "ImageURL");
        if ((gpu_image != NULL) && ((strcmp(gpu_image->valuestring, "images_icon/nvidia.png") == 0) || (strcmp(gpu_image->valuestring, "images_icon/amd.png") == 0)))
        {

            cJSON *gpu_name = cJSON_GetObjectItem(gpu, "Text");
            cJSON *gpu_children = cJSON_GetObjectItem(gpu, "Children");
            memcpy(_gpu_name_str, gpu_name->valuestring,
                    strlen(gpu_name->valuestring)>sizeof(_gpu_name_str)?sizeof(_gpu_name_str):strlen(gpu_name->valuestring));/*获取gpu名字*/

            int gpu_children_count = cJSON_GetArraySize(gpu_children);
            for (int x = 0; x < gpu_children_count; x++)
            {
            	/* 获取 gpu 使用率，有多个gpu使用率，只提取core */
                cJSON *gpu_load = cJSON_GetArrayItem(gpu_children, x);
                cJSON *gpu_load_text = cJSON_GetObjectItem(gpu_load, "Text");
                if ((gpu_load_text != NULL) && (strcmp(gpu_load_text->valuestring, "Load") == 0))
                {
                    cJSON *gpu_load_children = cJSON_GetObjectItem(gpu_load, "Children");
                    int gpu_load_children_count = cJSON_GetArraySize(gpu_load_children);
					for (int x = 0; x < gpu_load_children_count; x++)
					{
						cJSON *gpu_load_core = cJSON_GetArrayItem(gpu_load_children, x);
						cJSON *gpu_load_core_text = cJSON_GetObjectItem(gpu_load_core, "Text");
						if ((gpu_load_core_text != NULL) && (strcmp(gpu_load_core_text->valuestring, "GPU Core") == 0))
						{
		                    cJSON *gpu_load_core_value = cJSON_GetObjectItem(gpu_load_core, "Value");
		                    if(gpu_load_core_value != NULL)
		                    {    
	                            memcpy(_gpu_load_str, gpu_load_core_value->valuestring,
	                                    strlen(gpu_load_core_value->valuestring)>sizeof(_gpu_load_str)?sizeof(_gpu_load_str):strlen(gpu_load_core_value->valuestring));
	                            _gpu_load = pc_str_to_int(gpu_load_core_value->valuestring);
		                    }							
						}
					}
                }

                /* 获取 gpu 温度 */
                cJSON *gpu_temperatures = cJSON_GetArrayItem(gpu_children, x);
                cJSON *gpu_temperatures_text = cJSON_GetObjectItem(gpu_temperatures, "Text");
                if ((gpu_temperatures_text != NULL) && (strcmp(gpu_temperatures_text->valuestring, "Temperatures") == 0))
                {
                    cJSON *gpu_temperatures_children = cJSON_GetObjectItem(gpu_temperatures, "Children");
                    int gpu_temperatures_children_count = cJSON_GetArraySize(gpu_temperatures_children);
					for (int x = 0; x < gpu_temperatures_children_count; x++)
					{
						cJSON *gpu_temperatures_core = cJSON_GetArrayItem(gpu_temperatures_children, x);
						cJSON *gpu_temperatures_core_text = cJSON_GetObjectItem(gpu_temperatures_core, "Text");
						if ((gpu_temperatures_core_text != NULL) && (strcmp(gpu_temperatures_core_text->valuestring, "GPU Core") == 0))
						{
		                    cJSON *gpu_temperatures_core_value = cJSON_GetObjectItem(gpu_temperatures_core, "Value");
		                    if(gpu_temperatures_core_value != NULL)
		                    {    
	                            memcpy(_gpu_temperature_str, gpu_temperatures_core_value->valuestring,
	                                    strlen(gpu_temperatures_core_value->valuestring)>sizeof(_gpu_temperature_str)?sizeof(_gpu_temperature_str):strlen(gpu_temperatures_core_value->valuestring));
	                            _gpu_temperature = pc_str_to_int(gpu_temperatures_core_value->valuestring);

			                    if (_gpu_temperature_str[strlen(_gpu_temperature_str) - 1] == 'C')  /*温度单位是摄氏度还是华氏度*/
			                    {
			                        _gpu_temperature_unit_celsius = true;
			                    }
			                    else
			                    {
			                        _gpu_temperature_unit_celsius = false;
			                    }                           
		                    }							
						}
					}
                }                
                
            }
        }        
        /*================================ 遍历查找gpu 属性 end ======================================*/
    }

		EXIT:
		
    cJSON_Delete(root);

    taskEXIT_CRITICAL(0);

	ESP_LOGI(TAG, 	"\n---------------------------------------------------------\n"\
					"%s:[%s]\n"\
					"%s:[%s]\n"\
					"%s:[%s]\n"\
					"%s:[%s]\n"\
					"%s:[%s]\n"\
					"%s:[%s]\n"\
					"%s:[%s]\n"\
					"%s:[%s]\n"\
					"%s:[%s]\n"\
					"%s:[%d]\n"\
					"%s:[%d]\n"\
					"%s:[%d]\n"\
					"%s:[%d]\n"\
					"%s:[%d]\n"\
					"%s:[%d]\n"\
					"%s:[%d]\n"\
					"---------------------------------------------------------\n",
					VAR_TO_STR(_pc_name_str),_pc_name_str, \
					VAR_TO_STR(_mainboard_name_str),_mainboard_name_str, \
					VAR_TO_STR(_cpu_name_str),_cpu_name_str, \
					VAR_TO_STR(_gpu_name_str),_gpu_name_str, \
					VAR_TO_STR(_ram_load_str),_ram_load_str, \
					VAR_TO_STR(_cpu_load_str),_cpu_load_str, \
					VAR_TO_STR(_gpu_load_str),_gpu_load_str, \
					VAR_TO_STR(_gpu_temperature_str),_gpu_temperature_str, \
					VAR_TO_STR(_cpu_temperature_str),_cpu_temperature_str, \
					VAR_TO_STR(_gpu_temperature),_gpu_temperature, \
					VAR_TO_STR(_cpu_temperature),_cpu_temperature, \
					VAR_TO_STR(_ram_load),_ram_load, \
					VAR_TO_STR(_cpu_load),_cpu_load, \
					VAR_TO_STR(_gpu_load),_gpu_load, \
					VAR_TO_STR(_cpu_temperature_unit_celsius),_cpu_temperature_unit_celsius, \
					VAR_TO_STR(_gpu_temperature_unit_celsius),_gpu_temperature_unit_celsius);
    /*-------------------------------------end---------------------------------------------------*/

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


/**
 * @brief 获取cpu温度字符串
 * @return 返回字符串指针
 * 
 */
char *pc_get_cpu_temperature_str(void)
{
    return _cpu_temperature_str;
}

/**
 * @brief 获取gpu温度字符串
 * @return 返回字符串指针
 * 
 */
char *pc_get_gpu_temperature_str(void)
{
    return _gpu_temperature_str;
}

/**
 * @brief cpu温度单位是摄氏度
 * @return 返回布尔类型
 * 
 */
bool pc_is_cpu_temperature_unit_celsius(void)
{
    return _cpu_temperature_unit_celsius;
}

/**
 * @brief gpu温度单位是摄氏度
 * @return 返回布尔类型
 * 
 */
bool pc_is_gpu_temperature_unit_celsius(void)
{
    return _gpu_temperature_unit_celsius;
}

/**
 * @brief 获取gpu使用率字符串
 * @return 返回字符串指针
 * 
 */
char *pc_get_gpu_load_str(void)
{
    return _gpu_load_str;
}

/**
 * @brief 获取cpu使用率字符串
 * @return 返回字符串指针
 * 
 */
char *pc_get_cpu_load_str(void)
{
    return _cpu_load_str;
}

/**
 * @brief 获取ram使用率字符串
 * @return 返回字符串指针
 * 
 */
char *pc_get_ram_load_str(void)
{
    return _ram_load_str;
}

/**
 * @brief 获取cpu name string
 * @return 返回字符串指针
 * 
 */
char *pc_get_cpu_name_str(void)
{
    return _cpu_name_str;
}

/**
 * @brief 获取gpu name string
 * @return 返回字符串指针
 * 
 */
char *pc_get_gpu_name_str(void)
{
    return _gpu_name_str;
}

/**
 * @brief 获取pc name string
 * @return 返回字符串指针
 * 
 */
char *pc_get_name_str(void)
{
    return _pc_name_str;
}

/**
 * @brief 获取mainboard name string
 * @return 返回字符串指针
 * 
 */
char *pc_get_mainboard_name_str(void)
{
    return _mainboard_name_str;
}


