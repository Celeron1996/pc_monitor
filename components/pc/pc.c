#include <stdio.h>
#include <stdlib.h>
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
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_tls.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"
#include <lwip/netdb.h>

#include "pc.h"

#include "cJSON.h"

/* Littlevgl specific */
#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif
#include "lvgl_helpers.h"


#define VAR_TO_STR(STRING__)	#STRING__

static char *TAG = "PC";

/*-------------------------------- my custom -------------------------------------*/
#define MY_COLOR_SKYBLUE    LV_COLOR_MAKE(135, 206, 235)
#define MY_COLOR_BACKBLACK    LV_COLOR_MAKE(233, 233, 233)


static char URL[64] = {0};  /*统一资源定位链接，当PC端生成了http server时，通过此url获取json字符串*/

static int pc_str_to_int(char *pStr);
static void update_ip_address_to_flash(int ip0, int ip1, int ip2, int ip3);


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

extern const char *NVS_HANDLER;
extern const char *NVS_KEY_ACTIVE;
extern const char *NVS_VALUE_TRUE;
extern const char *NVS_VALUE_FALSE;
extern const char *NVS_KEY_JSON; 

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
#define PC_CJSON_CHECK(value)	do{if(value == NULL){ESP_LOGE(TAG, "%s,%d,%s", __FILE__, __LINE__, __FUNCTION__);goto EXIT;}}while(0)
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

		/*
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
			*/
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


void pc_task(void *pvParameters)
{
		char *buffer = NULL;
		esp_err_t err;
		uint8_t openerr_count = 0;
		int ip0,ip1,ip2,ip3,port;
		bool is_scan_mode = false;
		esp_http_client_config_t config = {
				.url = URL,
		};
		int pc_info_counter = 9999;

		while (pc_refresh_url() <= 0)
		{
			vTaskDelay(pdMS_TO_TICKS(1000));
		}

		ESP_LOGI(TAG, "url:%s", config.url);
		esp_http_client_handle_t client;

    /* 设置表盘样式 */
    lv_style_t style_cpu_lmeter;
    lv_style_init(&style_cpu_lmeter);
    lv_style_set_line_width(&style_cpu_lmeter, LV_STATE_DEFAULT, 4);    /*设置表盘小于value的刻度宽度*/
    lv_style_set_scale_end_line_width(&style_cpu_lmeter, LV_STATE_DEFAULT, 4);    /*设置表盘大于value的刻度宽度*/
    lv_style_set_scale_width(&style_cpu_lmeter, LV_STATE_DEFAULT, 16);  /*设置全部刻度线的长度*/
    lv_style_set_line_color(&style_cpu_lmeter, LV_STATE_DEFAULT, MY_COLOR_SKYBLUE);/*设置刻度小于value部分刻度线的颜色*/
    lv_style_set_scale_end_color(&style_cpu_lmeter, LV_STATE_DEFAULT, MY_COLOR_BACKBLACK);/*设置刻度大于value部分刻度线的颜色*/
    
    /* 创建CPU 表盘 */
    lv_obj_t *lmeter_cpu = lv_linemeter_create(lv_scr_act(), NULL);
    lv_linemeter_set_range(lmeter_cpu, 0, 100);                   /*Set the range*/
    lv_linemeter_set_value(lmeter_cpu, 80);                       /*Set the current value*/
    lv_linemeter_set_scale(lmeter_cpu, 280, 100);                  /*Set the angle and number of lines*/
    lv_obj_set_size(lmeter_cpu, 120, 120);
    lv_obj_align(lmeter_cpu, NULL, LV_ALIGN_IN_TOP_LEFT, 0, 0);

    lv_obj_t *label_cpu_title = lv_label_create(lmeter_cpu, NULL);
    lv_label_set_text(label_cpu_title, "CPU");
    lv_obj_align(label_cpu_title, NULL, LV_ALIGN_IN_BOTTOM_MID, 0, -16);
    
    lv_obj_t *label_cpu_temper = lv_label_create(lmeter_cpu, NULL);
    lv_label_set_text_fmt(label_cpu_temper, "%s", pc_get_cpu_temperature_str());
    lv_obj_align(label_cpu_temper, NULL, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_style(lmeter_cpu, LV_OBJ_PART_MAIN, &style_cpu_lmeter);

    /* 创建图表 */
    /*Create a chart*/
    lv_obj_t * chart = lv_chart_create(lv_scr_act(), NULL);
    lv_obj_set_size(chart, 240, 120);
    lv_obj_align(chart, NULL, LV_ALIGN_IN_BOTTOM_LEFT, 0, 0);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);   /*Show lines and points too*/
    lv_chart_set_div_line_count(chart, 3, 5);
    lv_chart_set_point_count(chart, 6);

    /*Add a faded are effect*/
    lv_obj_set_style_local_bg_opa(chart, LV_CHART_PART_SERIES, LV_STATE_DEFAULT, LV_OPA_50); /*Max. opa.*/
    lv_obj_set_style_local_bg_grad_dir(chart, LV_CHART_PART_SERIES, LV_STATE_DEFAULT, LV_GRAD_DIR_VER);
    lv_obj_set_style_local_bg_main_stop(chart, LV_CHART_PART_SERIES, LV_STATE_DEFAULT, 255);    /*Max opa on the top*/
    lv_obj_set_style_local_bg_grad_stop(chart, LV_CHART_PART_SERIES, LV_STATE_DEFAULT, 0);      /*Transparent on the bottom*/

    /*Add two data series*/
    lv_chart_series_t * chart_ser_cpu_load = lv_chart_add_series(chart, LV_COLOR_RED);
    lv_chart_series_t * chart_ser_gpu_load = lv_chart_add_series(chart, LV_COLOR_GREEN);
    lv_chart_series_t * chart_ser_ram_load = lv_chart_add_series(chart, LV_COLOR_BLUE);

    lv_obj_t *label_chart = lv_label_create(chart, NULL);
    lv_obj_align(label_chart, NULL, LV_ALIGN_IN_TOP_LEFT, 0 ,0);
    lv_obj_set_width(label_chart, lv_obj_get_width(chart));
    lv_label_set_recolor(label_chart, true);
    lv_label_set_text_fmt(label_chart, "#ff0000 处理器:%s#\n#00ff00 显卡:%s#\n#0000ff 内存:%s",
    											pc_get_cpu_load_str(), pc_get_gpu_load_str(), pc_get_ram_load_str());

		/* 创建文本，显示pc详细信息 */
    lv_obj_t *label_pc_info = lv_label_create(lv_scr_act(), NULL);
    lv_obj_set_pos(label_pc_info, 120, 100);
		lv_label_set_long_mode(label_pc_info, LV_LABEL_LONG_SROLL_CIRC);
		lv_obj_set_size(label_pc_info, 120, 16);
		lv_label_set_recolor(label_pc_info, true);
    lv_label_set_text_fmt(label_pc_info, "电脑:%s #0000ff 主板:%s ##ff0000 处理器:%s ##00ff00 显卡:%s #",
		    									pc_get_name_str(), pc_get_mainboard_name_str(), pc_get_cpu_name_str(), pc_get_gpu_name_str());

		while (true)
		{
			client = esp_http_client_init(&config);
			if (client != NULL)
			{
				if (is_scan_mode == true)
				{
					esp_http_client_set_timeout_ms(client, 400);
				}
				else
				{
					esp_http_client_set_timeout_ms(client, 1000);
				}
				
				if ((err = esp_http_client_open(client, 0)) == ESP_OK)
				{
					int content_length =	esp_http_client_fetch_headers(client);
					buffer = malloc(content_length + 128);
					if (buffer == NULL) {
						ESP_LOGE(TAG, "malloc failed!");
						esp_http_client_close(client);
					}
					else
					{
						memset(buffer, 0, (content_length + 128));
						int read_len, status_code_, content_length_;
						
						read_len = esp_http_client_read(client, buffer, content_length);
						status_code_ = esp_http_client_get_status_code(client);
						content_length_ = esp_http_client_get_content_length(client);
						if ((read_len <= 0) || (status_code_ <= 0) || (content_length_ <= 0)) {
								ESP_LOGE(TAG, "Error read data");
						}
						else
						{
							pc_analysis_json(buffer);
							/*------ refresh ui -----*/
							/* get data */
							int cpu_temper = pc_get_cpu_temperature();
							if (!(pc_is_cpu_temperature_unit_celsius()))
							{
									cpu_temper = ((cpu_temper - 32)*5)/9; 	/*表盘是满了是100，按照摄氏度来的，华氏度的话转成摄氏度: 华氏度 = 32℉ + (摄氏度 * 1.8) */
							}
							
						  /* set data to gui */
						  lv_linemeter_set_value(lmeter_cpu, cpu_temper);
						  
						  lv_label_set_text_fmt(label_cpu_temper, "%s", pc_get_cpu_temperature_str());
						  lv_obj_align(label_cpu_temper, NULL, LV_ALIGN_CENTER, 0, 0);    			

							if (pc_info_counter > 10)
							{
								pc_info_counter = 0;
						  	lv_label_set_text_fmt(label_pc_info, "电脑:%s #0000ff 主板:%s ##ff0000 处理器:%s ##00ff00 显卡:%s #",
																			pc_get_name_str(), pc_get_mainboard_name_str(), pc_get_cpu_name_str(), pc_get_gpu_name_str());       			
							}															


							lv_label_set_text_fmt(label_chart, "#ff0000 处理器:%s#\n#00ff00 显卡:%s#\n#0000ff 内存:%s",
																		pc_get_cpu_load_str(), pc_get_gpu_load_str(), pc_get_ram_load_str());
							                     
							lv_chart_set_next(chart, chart_ser_cpu_load, pc_get_cpu_load());
							lv_chart_set_next(chart, chart_ser_gpu_load, pc_get_gpu_load());
							lv_chart_set_next(chart, chart_ser_ram_load, pc_get_ram_load());				
													
						}
						
						ESP_LOGI(TAG, "HTTP Stream reader Status = %d, content_length = %d, read_len = %d, url:%s",
														status_code_,
														content_length_,
														read_len,
														URL);

						free(buffer);

						esp_http_client_close(client);								
					}


					if (is_scan_mode == true)
					{
						update_ip_address_to_flash(ip0, ip1, ip2, ip3);
						is_scan_mode = false;
						openerr_count = 0;
					}
				}
				else
				{
					esp_http_client_close(client);
					ESP_LOGE(TAG, "Failed to open HTTP connection: %s,url:%s", esp_err_to_name(err),URL);
					if (openerr_count < 5)
					{
						openerr_count++;
						if (openerr_count >=5)
						{
							ip0 = ip1 = ip2 = ip3 = port = 0;
							sscanf(URL,"http://%d.%d.%d.%d:%d/data.json",&ip0,&ip1,&ip2,&ip3,&port);
							is_scan_mode = true;
						}
					}
				}
			}
			else
			{
				ESP_LOGE(TAG, "esp_http_client_init error");
			}

			esp_http_client_cleanup(client);

			vTaskDelay(pdMS_TO_TICKS((is_scan_mode == true)?(100):(1000)));
			pc_info_counter++;

			if (is_scan_mode == true)
			{
				memset(URL, 0, sizeof(URL));
				ip3++;
				if (ip3 > 255)
				{
					ip3 = 0;
				}
				sprintf(URL, "http://%d.%d.%d.%d:%d/data.json", ip0,ip1,ip2,ip3,port);
			}
		}
		
		vTaskDelete(NULL);
}




int pc_refresh_url(void)
{
		/*--------------------step1:首先，从nvs中获取json--------------*/
		esp_err_t err;
		char *buffer;
		size_t length = 4*1024;
		nvs_handle_t nvs_handle;

		/*打开nvs*/
		err = nvs_open(NVS_HANDLER, NVS_READWRITE, &nvs_handle);
		if(err != ESP_OK)
		{
				ESP_LOGE(TAG, "nvs_open error, code = %d", err);
				return -1;
		}

		buffer = (char *)malloc(length);
		if (buffer == NULL)
		{
			ESP_LOGE(TAG, "pc_refresh_url malloc error");
			nvs_close(nvs_handle);
			return -1;
		}
		memset(buffer, 0, length);

		/*获取json*/
		err = nvs_get_str(nvs_handle, NVS_KEY_JSON, buffer, &length);
		if(err == ESP_OK)
		{
				ESP_LOGI(TAG, "get nvs wifi json success! json = %s", buffer);
		}
		else
		{
				ESP_LOGE(TAG, "nvs active error, code = %d", err);
				nvs_close(nvs_handle);
				free(buffer);
				return -1;

		}

		/*------------------------step2:获取成功，解析json----------------------------------*/
		cJSON *json_root = cJSON_Parse(buffer);
		if (json_root == NULL)
		{
			ESP_LOGE(TAG, "cJSON_Parse  error!");
			nvs_close(nvs_handle);
			free(buffer);
			return -1;
		}

		cJSON *json_ip_address = cJSON_GetObjectItem(json_root, "ip_address");
		cJSON *json_port = cJSON_GetObjectItem(json_root, "port");
		if (json_ip_address == NULL || json_port == NULL)
		{
			ESP_LOGE(TAG, "cJSON_GetObjectItem  error!");
			cJSON_Delete(json_root);
			nvs_close(nvs_handle);
			free(buffer);
			return -1;			
		}

		memset(URL, 0, sizeof(URL));
		strcat(URL, "http://");//"http://192.168.31.99:8085/data.json";
		strcat(URL, json_ip_address->valuestring);
		strcat(URL, ":");
		strcat(URL, json_port->valuestring);
		strcat(URL, "/data.json");
		ESP_LOGI(TAG, "url:%s", URL);

		nvs_close(nvs_handle);
		cJSON_Delete(json_root);
		free(buffer);

		return 1;
}


/**
 * @brief 线程：将最新连接成功的ip地址写入flash
 * 
 */
static void update_ip_address_to_flash(int ip0, int ip1, int ip2, int ip3)
{
		/*--------------------step1:首先，从nvs中获取wifi名称、密码，ip地址等--------------*/
		esp_err_t err;
		char buffer[128];
		size_t length;
		nvs_handle_t nvs_handle;
		char ip_str[] = "255.255.255.255";

		/*打开nvs*/
		err = nvs_open(NVS_HANDLER, NVS_READWRITE, &nvs_handle);
		if(err != ESP_OK)
		{
				ESP_LOGE(TAG, "nvs_open error, code = %d", err);
				while(1);
		}

		/*获取json*/
		length = sizeof(buffer);
		memset(buffer, 0, sizeof(buffer));
		err = nvs_get_str(nvs_handle, NVS_KEY_JSON, buffer, &length);
		if(err == ESP_OK)
		{
				ESP_LOGI(TAG, "get nvs wifi json success! json = %s", buffer);
		}
		else
		{
				ESP_LOGE(TAG, "nvs active error, code = %d", err);
				goto flag_err;
		}

		/*------------------------step2:获取成功，解析json----------------------------------*/
		cJSON *json_root = cJSON_Parse(buffer);

		memset(ip_str, 0, sizeof(ip_str));
		sprintf(ip_str,"%d.%d.%d.%d",ip0,ip1,ip2,ip3);
		cJSON_SetValuestring(cJSON_GetObjectItem(json_root, "ip_address"), (const char *)ip_str);

		err = nvs_set_str(nvs_handle, NVS_KEY_JSON, cJSON_PrintUnformatted(json_root));
		if (err == ESP_OK)
		{
			ESP_LOGI(TAG, "update json to flash success! json = %s", cJSON_PrintUnformatted(json_root));
		}
		else
		{
			ESP_LOGE(TAG, "update json to flash error, code = %d", err);
			goto flag_err;
		}

		nvs_close(nvs_handle);

		cJSON_Delete(json_root);

		return;
		
		flag_err:
		nvs_set_str(nvs_handle, NVS_KEY_ACTIVE, NVS_VALUE_FALSE);
		nvs_commit(nvs_handle);
		nvs_close(nvs_handle);
		ESP_LOGE(TAG, "update_ip_address_to_flash error");
}

