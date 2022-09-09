/* HTTP GET Example using plain POSIX sockets

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
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


#include "cJSON.h"

#include "weather.h"

/* Littlevgl specific */
#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif
#include "lvgl_helpers.h"

extern const char *NVS_HANDLER;
extern const char *NVS_KEY_ACTIVE;
extern const char *NVS_VALUE_TRUE;
extern const char *NVS_VALUE_FALSE;
extern const char *NVS_KEY_JSON; 

static void weather_analysis_json(void *pJson);
static int weather_refresh_url(void);


/* seniverse api arg */
#define PRIVATE_KEY	"key=S9wcdovBWmKYunRle"
#define CITY_NAME	"location=beijing"
#define LANGUAGE	"language=zh-Hans"
#define UNIT		"unit=c"

/* Constants that aren't configurable in menuconfig */
#define WEB_SERVER "api.seniverse.com"
#define WEB_PORT "80"
#define WEB_PATH "/v3/weather/now.json?" PRIVATE_KEY "&" CITY_NAME "&" LANGUAGE "&" UNIT

static const char *TAG = "weather";

static char REQUEST[256] = "GET " WEB_PATH " HTTP/1.0\r\n"
    "Host: "WEB_SERVER":"WEB_PORT"\r\n"
    "User-Agent: esp-idf/1.0 esp32\r\n"
    "\r\n";

static char recv_buf[1024];		/* 存储天气服务的json */

LV_IMG_DECLARE(weather_cloudy);
LV_IMG_DECLARE(weather_foggy);
LV_IMG_DECLARE(weather_icerain);
LV_IMG_DECLARE(weather_lightrain);
LV_IMG_DECLARE(weather_moderatesnow);
LV_IMG_DECLARE(weather_overcast);
LV_IMG_DECLARE(weather_sleet);
LV_IMG_DECLARE(weather_snow);
LV_IMG_DECLARE(weather_storm);
LV_IMG_DECLARE(weather_sunny);
LV_IMG_DECLARE(weather_thunder);
LV_IMG_DECLARE(weather_windy);
LV_IMG_DECLARE(weather_unknow);


static lv_obj_t * weather_image;
static lv_obj_t * weather_label;

struct weather_struct{
	char private_key[128];			/* 平台私人密钥 */
	char city_wechat[64];				/* 从微信下发的城市名称拼音 */
	char city_network[64];			/* 从天气服务器中返回的城市名称 */
	char text[64];							/* 存储天气状况描述，晴、下雨、下雪之类的 */
	char code[32];								/* 天气代码，用来判断用哪个天气图标 */
	char temperature[32];				/* 存储温度 */
};

struct weather_struct *p_weather;


void weather_task(void *pvParameters)
{
    const struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;
    struct in_addr *addr;
    int s, r;

    p_weather = (struct weather_struct *)malloc(sizeof(struct weather_struct));
    if (p_weather == NULL)
    {
    	ESP_LOGE(TAG, "malloc error!");
    	vTaskDelete(NULL);
    }

    weather_image = lv_img_create(lv_scr_act(), NULL);
    lv_obj_set_pos(weather_image, 120, 8);
    lv_img_set_auto_size(weather_image, true);
    
    weather_label = lv_label_create(lv_scr_act(), NULL);
    lv_obj_align(weather_label, weather_image, LV_ALIGN_IN_RIGHT_MID, 8, -8);
    lv_label_set_recolor(weather_label, true);

    weather_refresh_url();

    vTaskDelay(pdMS_TO_TICKS(5000));

    while(1) {
        int err = getaddrinfo(WEB_SERVER, WEB_PORT, &hints, &res);

        if(err != 0 || res == NULL) {
            ESP_LOGE(TAG, "DNS lookup failed err=%d res=%p", err, res);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }

        /* Code to print the resolved IP.

           Note: inet_ntoa is non-reentrant, look at ipaddr_ntoa_r for "real" code */
        addr = &((struct sockaddr_in *)res->ai_addr)->sin_addr;
        ESP_LOGI(TAG, "DNS lookup succeeded. IP=%s", inet_ntoa(*addr));

        s = socket(res->ai_family, res->ai_socktype, 0);
        if(s < 0) {
            ESP_LOGE(TAG, "... Failed to allocate socket.");
            freeaddrinfo(res);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "... allocated socket");

        if(connect(s, res->ai_addr, res->ai_addrlen) != 0) {
            ESP_LOGE(TAG, "... socket connect failed errno=%d", errno);
            close(s);
            freeaddrinfo(res);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }

        ESP_LOGI(TAG, "... connected");
        freeaddrinfo(res);

        if (write(s, REQUEST, strlen(REQUEST)) < 0) {
            ESP_LOGE(TAG, "... socket send failed");
            close(s);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "... socket send success");

        struct timeval receiving_timeout;
        receiving_timeout.tv_sec = 5;
        receiving_timeout.tv_usec = 0;
        if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &receiving_timeout,
                sizeof(receiving_timeout)) < 0) {
            ESP_LOGE(TAG, "... failed to set socket receiving timeout");
            close(s);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "... set socket receiving timeout success");

        /* Read HTTP response */
        bzero(recv_buf, sizeof(recv_buf));
        char *p_tempbuf = recv_buf;
        do {
            
            r = read(s, p_tempbuf, 64);
            p_tempbuf += r;
        } while(r > 0);

				weather_analysis_json(recv_buf);
				weather_gui_refresh();
				ESP_LOGI(TAG, "-----------------------------------------------------------");
				ESP_LOGI(TAG, "city:%s, text:%s, temper:%s", weather_get_city(), weather_get_text(), weather_get_temperature());
				ESP_LOGI(TAG, "-----------------------------------------------------------");
				
        ESP_LOGI(TAG, "... done reading from socket. Last read return=%d errno=%d.", r, errno);
        close(s);
        vTaskDelay(pdMS_TO_TICKS(900000));
        ESP_LOGI(TAG, "Starting again!");
    }
}



/********************************
HTTP/1.1 200 OK
Date: Wed, 07 Sep 2022 18:36:09 GMT
Content-Type: application/json; charset=utf-8
Content-Length: 256
Connection: close
access-control-allow-origin: *
etag: W/"100-pYXDgNxz7W6RNJcYkE0R71620yM"
x-powered-by: Express
x-ratelimit-limit: 20;w=60
x-ratelimit-remaining: 13
x-ratelimit-reset: 12.811
x-request-id: FxKn2o1XWT+gdvtw8kzD
x-tenant-id: 003d9450-946c-4381-b545-2ccb77207347

{"results":[{"location":{"id":"WX4FBXXFKE4F","name":"北京","country":"CN","path":"北京,北京,中国","timezone":"Asia/Shanghai","timezone_offset":"+08:00"},"now":{"text":"晴","code":"1","temperature":"17"},"last_update":"2022-09-08T02:20:11+08:00"}]}
********************************/
#define WEATHER_CJSON_CHECK(value)	do{if(value == NULL){goto EXIT;}}while(0)
static void weather_analysis_json(void *pJson)
{
	char *p_temp = strstr(pJson, "results");
	p_temp -= 2;
	cJSON *root = cJSON_Parse(p_temp);WEATHER_CJSON_CHECK(root);
	cJSON *results = cJSON_GetObjectItem(root, "results");WEATHER_CJSON_CHECK(results);
	cJSON *array_0 = cJSON_GetArrayItem(results, 0);WEATHER_CJSON_CHECK(array_0);
	
	cJSON *location = cJSON_GetObjectItem(array_0, "location");
	cJSON *now = cJSON_GetObjectItem(array_0, "now");
	cJSON *name = cJSON_GetObjectItem(location, "name");
	cJSON *text = cJSON_GetObjectItem(now, "text");
	cJSON *code = cJSON_GetObjectItem(now, "code");
	cJSON *temperature = cJSON_GetObjectItem(now, "temperature");

	if ((name != NULL) && (strlen(name->valuestring) <= sizeof(p_weather->city_network)))
	{
		memset(p_weather->city_network, 0, sizeof(p_weather->city_network));
		strcpy(p_weather->city_network, name->valuestring);
	}

	if ((text != NULL) && (strlen(text->valuestring) <= sizeof(p_weather->text)))
	{
		memset(p_weather->text, 0, sizeof(p_weather->text));
		strcpy(p_weather->text, text->valuestring);
	}

	if ((code != NULL) && (strlen(code->valuestring) <= sizeof(p_weather->code)))
	{
		memset(p_weather->code, 0, sizeof(p_weather->code));
		strcpy(p_weather->code, code->valuestring);
	}

	if ((temperature != NULL) && (strlen(temperature->valuestring) <= sizeof(p_weather->temperature)))
	{
		memset(p_weather->temperature, 0, sizeof(p_weather->temperature));
		strcpy(p_weather->temperature, temperature->valuestring);
	}

	EXIT:
	cJSON_Delete(root);
}



char *weather_get_city(void)
{
	return p_weather->city_network;
}


char *weather_get_text(void)
{
	return p_weather->text;
}

char *weather_get_code(void)
{
	return p_weather->code;
}

char *weather_get_temperature(void)
{
	return p_weather->temperature;
}

void weather_gui_refresh(void)
{
	if (strcmp(weather_get_code(), "0") == 0)  /* 晴（国内城市白天晴） */
		lv_img_set_src(weather_image, &weather_sunny);
	else if(strcmp(weather_get_code(), "1") == 0)  /* 晴（国内城市夜晚晴） */
		lv_img_set_src(weather_image, &weather_sunny);
	else if(strcmp(weather_get_code(), "2") == 0)  /* 晴（国外城市白天晴） */
		lv_img_set_src(weather_image, &weather_sunny);
	else if(strcmp(weather_get_code(), "3") == 0)  /* 晴（国外城市夜晚晴） */
		lv_img_set_src(weather_image, &weather_sunny);
	else if(strcmp(weather_get_code(), "4") == 0)  /* 多云*/
		lv_img_set_src(weather_image, &weather_cloudy);
	else if(strcmp(weather_get_code(), "5") == 0)  /* 晴间多云 */
		lv_img_set_src(weather_image, &weather_cloudy);
	else if(strcmp(weather_get_code(), "6") == 0)  /* 晴间多云*/
		lv_img_set_src(weather_image, &weather_cloudy);
	else if(strcmp(weather_get_code(), "7") == 0)  /* 大部多云 */
		lv_img_set_src(weather_image, &weather_cloudy);
	else if(strcmp(weather_get_code(), "8") == 0)  /* 大部多云 */
		lv_img_set_src(weather_image, &weather_cloudy);
	else if(strcmp(weather_get_code(), "9") == 0)  /* 阴 */
		lv_img_set_src(weather_image, &weather_overcast);
	else if(strcmp(weather_get_code(), "10") == 0)  /* 阵雨 */
		lv_img_set_src(weather_image, &weather_lightrain);
	else if(strcmp(weather_get_code(), "11") == 0)  /* 雷阵雨 */
		lv_img_set_src(weather_image, &weather_thunder);
	else if(strcmp(weather_get_code(), "12") == 0)  /* 雷阵雨伴有冰雹 */
		lv_img_set_src(weather_image, &weather_thunder);
	else if(strcmp(weather_get_code(), "13") == 0)  /* 小雨 */
		lv_img_set_src(weather_image, &weather_lightrain);
	else if(strcmp(weather_get_code(), "14") == 0)  /* 中雨 */
		lv_img_set_src(weather_image, &weather_lightrain);
	else if(strcmp(weather_get_code(), "15") == 0)  /* 大雨 */
		lv_img_set_src(weather_image, &weather_storm);
	else if(strcmp(weather_get_code(), "16") == 0)  /* 暴雨 */
		lv_img_set_src(weather_image, &weather_storm);
	else if(strcmp(weather_get_code(), "17") == 0)  /* 大暴雨 */
		lv_img_set_src(weather_image, &weather_storm);
	else if(strcmp(weather_get_code(), "18") == 0)  /* 特大暴雨 */
		lv_img_set_src(weather_image, &weather_storm);
	else if(strcmp(weather_get_code(), "19") == 0)  /* 冻雨 */
		lv_img_set_src(weather_image, &weather_icerain);
	else if(strcmp(weather_get_code(), "20") == 0)  /* 雨夹雪 */
		lv_img_set_src(weather_image, &weather_sleet);
	else if(strcmp(weather_get_code(), "21") == 0)  /* 阵雪 */
		lv_img_set_src(weather_image, &weather_moderatesnow);
	else if(strcmp(weather_get_code(), "22") == 0)  /* 小雪 */
		lv_img_set_src(weather_image, &weather_moderatesnow);
	else if(strcmp(weather_get_code(), "23") == 0)  /* 中雪 */
		lv_img_set_src(weather_image, &weather_moderatesnow);
	else if(strcmp(weather_get_code(), "24") == 0)  /* 大雪 */
		lv_img_set_src(weather_image, &weather_snow);
	else if(strcmp(weather_get_code(), "25") == 0)  /* 暴雪 */
		lv_img_set_src(weather_image, &weather_snow);
	else if(strcmp(weather_get_code(), "26") == 0)  /* 浮尘 */
		lv_img_set_src(weather_image, &weather_foggy);
	else if(strcmp(weather_get_code(), "27") == 0)  /* 扬沙 */
		lv_img_set_src(weather_image, &weather_foggy);
	else if(strcmp(weather_get_code(), "28") == 0)  /* 沙尘暴 */
		lv_img_set_src(weather_image, &weather_windy);
	else if(strcmp(weather_get_code(), "29") == 0)  /* 强沙尘暴 */
		lv_img_set_src(weather_image, &weather_windy);
	else if(strcmp(weather_get_code(), "30") == 0)  /* 雾 */
		lv_img_set_src(weather_image, &weather_foggy);
	else if(strcmp(weather_get_code(), "31") == 0)  /* 霾 */
		lv_img_set_src(weather_image, &weather_foggy);
	else if(strcmp(weather_get_code(), "32") == 0)  /* 风 */
		lv_img_set_src(weather_image, &weather_windy);
	else if(strcmp(weather_get_code(), "33") == 0)  /* 大风 */
		lv_img_set_src(weather_image, &weather_windy);
	else if(strcmp(weather_get_code(), "34") == 0)  /* 飓风 */
		lv_img_set_src(weather_image, &weather_windy);
	else if(strcmp(weather_get_code(), "35") == 0)  /* 热带风暴 */
		lv_img_set_src(weather_image, &weather_windy);
	else if(strcmp(weather_get_code(), "36") == 0)  /* 龙卷风 */
		lv_img_set_src(weather_image, &weather_windy);
	else if(strcmp(weather_get_code(), "37") == 0)  /* 冰 */
		lv_img_set_src(weather_image, &weather_snow);
	else if(strcmp(weather_get_code(), "38") == 0)  /* 热 */
		lv_img_set_src(weather_image, &weather_sunny);
	else if(strcmp(weather_get_code(), "99") == 0)  /* 未知 */
		lv_img_set_src(weather_image, &weather_unknow);
	else
		lv_img_set_src(weather_image, &weather_unknow);

  lv_label_set_text_fmt(weather_label, "#ff0000 %s#\n#0000ff %s %s°C#",
  											weather_get_city(), weather_get_text(), weather_get_temperature());
}




static int weather_refresh_url(void)
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
			ESP_LOGE(TAG, "weather_refresh_url malloc error");
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
			ESP_LOGE(TAG, "cJSON_Parse	error!");
			nvs_close(nvs_handle);
			free(buffer);
			return -1;
		}

		cJSON *json_city = cJSON_GetObjectItem(json_root, "city");
		cJSON *json_private_key = cJSON_GetObjectItem(json_root, "private_key");
		if (json_city == NULL || json_private_key == NULL)
		{
			ESP_LOGE(TAG, "cJSON_GetObjectItem	error!");
			cJSON_Delete(json_root);
			nvs_close(nvs_handle);
			free(buffer);
			return -1;			
		}

		memset(REQUEST, 0, sizeof(REQUEST));
		sprintf(REQUEST, "GET /v3/weather/now.json?%s&%s&%s&%s HTTP/1.0\r\nHost: %s:%s\r\nUser-Agent: esp-idf/1.0 esp32\r\n\r\n",
											json_private_key->valuestring, json_city->valuestring, LANGUAGE, UNIT, WEB_SERVER, WEB_PORT);
		
		ESP_LOGI(TAG, "url:%s", REQUEST);

		nvs_close(nvs_handle);
		cJSON_Delete(json_root);
		free(buffer);
		
		return 1;
}



