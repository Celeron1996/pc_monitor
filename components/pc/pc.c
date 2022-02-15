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


static char *pJSON = NULL;  /*存储json的指针*/
static void pc_task(void *pvParameters);    /*pc任务，获取json*/
static char *pURL = NULL;    /*用于存储外部传进来的pc http server 的url*/


/**
 * @brief 创建一个线程，使用http client获取pc的data.json
 * @param url:data.json的位置
 */
void pc_init(const char *url)
{
    pURL = (char *)url;
    ESP_LOGI(TAG, "get url:%s", pURL);
    xTaskCreate(pc_task, "pc_task", 8192, (void*)AF_INET, 6, NULL);
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
#define MAX_HTTP_RECV_BUFFER_ (10*1024)
static void pc_task(void *pvParameters)
{
    char *buffer = malloc(MAX_HTTP_RECV_BUFFER_ + 1);
    //cJSON *json_root = NULL;

    if (buffer == NULL) {
        ESP_LOGE(TAG, "Cannot malloc http receive buffer");
        return;
    }
    esp_http_client_config_t config = {
        .url = pURL,
    };
    ESP_LOGI(TAG, "url:%s", config.url);
    esp_http_client_handle_t client = esp_http_client_init(&config);

    while (true) {
        esp_err_t err;
        if ((err = esp_http_client_open(client, 0)) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
            free(buffer);
            return;
        }
        int content_length =  esp_http_client_fetch_headers(client);
        int total_read_len = 0, read_len;
        if (total_read_len < content_length && content_length <= MAX_HTTP_RECV_BUFFER_) {
            read_len = esp_http_client_read(client, buffer, content_length);
            if (read_len <= 0) {
                ESP_LOGE(TAG, "Error read data");
            }
            buffer[read_len] = 0;
            ESP_LOGD(TAG, "read_len = %d", read_len);
        }
        ESP_LOGI(TAG, "HTTP Stream reader Status = %d, content_length = %d",
                        esp_http_client_get_status_code(client),
                        esp_http_client_get_content_length(client));

 

        cJSON *json_root = NULL;
        cJSON *json_children;
        int children_size;
        json_root = cJSON_Parse(buffer);
        json_children = cJSON_GetObjectItem(json_root, "Children");
        children_size = cJSON_GetArraySize(json_children);
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
                cJSON *cpu_temp_children = cJSON_GetObjectItem(cpu_temperature, "Children");
                cJSON *cpu_temper_package = cJSON_GetArrayItem(cpu_temp_children, 4);
                cJSON *cpu_temper_package_value = cJSON_GetObjectItem(cpu_temper_package, "Value");

                ESP_LOGI(TAG, "host name = %s, cpu name = %s, cpu tempera = %s", host_name->valuestring, cpu_name->valuestring, cpu_temper_package_value->valuestring);

            }
        }


        // if (json_root) {
        //     ESP_LOGI(TAG, "json_root->string:%s,json_root->type=%d,json_root->valuedouble=%f,json_root->valueint=%d,json_root->valuestring=%s", 
        //     json_root->string,
        //     json_root->type,
        //     json_root->valuedouble,
        //     json_root->valueint,
        //     json_root->valuestring
        //     );
        // }
   

        

        esp_http_client_close(client);
             

        vTaskDelay(1000/portTICK_PERIOD_MS);                   


        cJSON_Delete(json_root);    
    }

    esp_http_client_cleanup(client);   




    free(buffer);
    ESP_LOGI(TAG, "Finish http example");
    vTaskDelete(NULL);
}