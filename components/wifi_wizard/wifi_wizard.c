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


static char URL[64] = {0};

#define ESP_AP_WIFI_SSID      "PCMonitor"
#define ESP_AP_WIFI_PASS      ""
#define ESP_AP_WIFI_CHANNEL   1
#define ESP_AP_MAX_STA_COUNT  1

#define WIFI_CONNECTED_BIT BIT0     /*连接 ap 成功事件标志位*/
#define WIFI_FAIL_BIT      BIT1     /*连接失败事件标志位*/
#define WIFI_AP_OK_BIT     BIT2     /*wifi ap 模式创建成功事件标志位*/

#define ESP_STA_MAXIMUM_RETRY  16  /*wifi重连次数*/

static const char *TAG = "wifi_wizard";

/*存储非易失性存储的字符串*/
static const char *NVS_NAME_STR = "config";
static const char *NVS_ACTIVE_STR = "active";
static const char *NVS_TRUE_STR = "true";
static const char *NVS_FALSE_STR = "false";
static const char *NVS_JSON_STR = "json";

/**/
static bool sta_state = false;


/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

static int s_retry_num = 0;

static void udp_server_task(void *pvParameters);

void wifi_init_softap(void);
void wifi_wizard_config(void);
void wifi_wizard_connect(void);
void wifi_init_sta(void);


/**
 * @brief wifi 配置向导，用于获取wifi名称、密码，并进行连接配置
 * 
 */
void wifi_wizard_init(void)
{
    esp_err_t err;
    char buffer[16];
    size_t length;
    oled_handler_p oled;
    nvs_handle_t nvs_handle;

    s_wifi_event_group = xEventGroupCreate();   /*事件标记组，等待连接上wifi或失败的事件*/

    oled = oled_get_handler();
    if(oled == NULL)
    {
        ESP_LOGE(TAG, "oled_get_handler error!");
        while(1);
    }
    oled->clear(oled);

    err = nvs_open("config", NVS_READWRITE, &nvs_handle);
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs_open error, code = %d", err);
        oled->printf(0, 0, 0, oled, "wifi config error");
        while(1);
    }

    length = sizeof(buffer);
    err = nvs_get_str(nvs_handle, NVS_ACTIVE_STR, buffer, &length);
    nvs_close(nvs_handle);
    if(err == ESP_OK)
    {
        if(memcmp(buffer, NVS_TRUE_STR, strlen(NVS_TRUE_STR)) == 0)
        //if(false)
        {
            /*存储的wifi是正确的，可以使用*/
            wifi_wizard_connect();
        }
        else
        {
            /*存储的信息无效，进行配网流程*/
            wifi_wizard_config();
        }
    }
    else
    {
        /*可能是没有建立此标签，第一次使用？重新配置一遍*/
        wifi_wizard_config();
    }

    vEventGroupDelete(s_wifi_event_group);
}

/**
 * @brief wifi ap + udp server，receive wifi info from wechat miniprogram
 * 
 */
void wifi_wizard_config(void)
{
    wifi_init_softap();

    //vTaskDelay(500 / portTICK_RATE_MS);
    xEventGroupWaitBits(s_wifi_event_group,
            WIFI_AP_OK_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);
    xTaskCreate(udp_server_task, "udp_server", 4096, (void*)AF_INET, 5, NULL);
}

/**
 * @brief wifi 连接向导，将已存储在flash的wifi名称和密码取出，并连接
 * 
 */
void wifi_wizard_connect(void)
{
    wifi_init_sta();
}

static void event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START)
    {
        ESP_LOGI(TAG, "wifi ap start!");
        xEventGroupSetBits(s_wifi_event_group, WIFI_AP_OK_BIT);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        // if (s_retry_num < ESP_STA_MAXIMUM_RETRY)
        // {
        //     esp_wifi_connect();
        //     s_retry_num++;
        //     ESP_LOGI(TAG, "retry to connect to the AP");
        // }
        // else
        // {
        //     xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        // }
        esp_wifi_connect();
        ESP_LOGI(TAG,"connect to the AP fail");
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_softap(void)
{
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = ESP_AP_WIFI_SSID,
            .ssid_len = strlen(ESP_AP_WIFI_SSID),
            .channel = ESP_AP_WIFI_CHANNEL,
            .password = ESP_AP_WIFI_PASS,
            .max_connection = ESP_AP_MAX_STA_COUNT,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    if (strlen(ESP_AP_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             ESP_AP_WIFI_SSID, ESP_AP_WIFI_PASS, ESP_AP_WIFI_CHANNEL);
}

void wifi_init_sta(void)
{
    /*--------------------step1:首先，从nvs中获取wifi名称、密码，ip地址等--------------*/
    esp_err_t err;
    char buffer[128];
    size_t length;
    oled_handler_p oled;
    nvs_handle_t nvs_handle;

    /*获取oled、清屏*/
    oled = oled_get_handler();
    if(oled == NULL)
    {
        ESP_LOGE(TAG, "oled_get_handler error!");
        while(1);
    }
    oled->clear(oled);

    /*打开nvs*/
    err = nvs_open(NVS_NAME_STR, NVS_READWRITE, &nvs_handle);
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs_open error, code = %d", err);
        oled->printf(0, 0, 0, oled, "nvs_open error!!!");
        while(1);
    }

    /*获取json*/
    length = sizeof(buffer);
    err = nvs_get_str(nvs_handle, NVS_JSON_STR, buffer, &length);
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
    cJSON *json_wifi_ssid = cJSON_GetObjectItem(json_root, "wifi_ssid");
    cJSON *json_wifi_pass = cJSON_GetObjectItem(json_root, "wifi_pass");
    cJSON *json_ip_address = cJSON_GetObjectItem(json_root, "ip_address");
    cJSON *json_port = cJSON_GetObjectItem(json_root, "port");

    ESP_LOGI(TAG, "wifi ssid:%s, wifi pass:%s, ip:%s, port:%s", 
            json_wifi_ssid->valuestring,
            json_wifi_pass->valuestring,
            json_ip_address->valuestring, json_port->valuestring);
    memset(URL, 0, sizeof(URL));
    strcat(URL, "http://");//"http://192.168.31.99:8085/data.json";
    strcat(URL, json_ip_address->valuestring);
    strcat(URL, ":");
    strcat(URL, json_port->valuestring);
    strcat(URL, "/data.json");
    ESP_LOGI(TAG, "url:%s", URL);

    /*--------------------------step3:连接wifi--------------------------------------*/

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            //  .ssid = json_wifi_ssid->valuestring,
            //  .password = json_wifi_pass->valuestring,
            /* Setting a password implies station will connect to all security modes including WEP/WPA.
             * However these modes are deprecated and not advisable to be used. Incase your Access point
             * doesn't support WPA2, these mode can be enabled by commenting below line */
	     .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            

            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };

    memcpy(wifi_config.sta.ssid, json_wifi_ssid->valuestring, strlen(json_wifi_ssid->valuestring));
    memcpy(wifi_config.sta.password, json_wifi_pass->valuestring, strlen(json_wifi_pass->valuestring));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 wifi_config.sta.ssid, wifi_config.sta.password);
        sta_state = true;
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 wifi_config.sta.ssid, wifi_config.sta.password);
        
        goto flag_err;
    }
    else
    {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
        goto flag_err;
    }

    /* The event will not be processed after unregister */
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));

    nvs_close(nvs_handle);

    // cJSON_Delete(json_port);
    // cJSON_Delete(json_ip_address);
    // cJSON_Delete(json_wifi_pass);
    // cJSON_Delete(json_wifi_ssid);
    cJSON_Delete(json_root);

    return;
    
    flag_err:
    nvs_set_str(nvs_handle, NVS_ACTIVE_STR, NVS_FALSE_STR);
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    oled->clear(oled);
    oled->printf(0, 0, 0, oled, "connecting err,reboot...");
    ESP_LOGE(TAG, "wifi connect error, will be reboot");
    vTaskDelay(4000 / portTICK_RATE_MS);
    esp_restart();
}



#define PORT (8848)
static void udp_server_task(void *pvParameters)
{
    char rx_buffer[128];
    char addr_str[128];
    int addr_family = (int)pvParameters;
    int ip_protocol = 0;
    struct sockaddr_in6 dest_addr;
    oled_handler_p oled;
    nvs_handle_t nvs_handle;

    oled = oled_get_handler();
    if (oled == NULL)
    {
        ESP_LOGE(TAG, "oled get handler error!");
        while(1);
    }

    oled->clear(oled);

    oled->printf(0, 0, 0, oled, "wifi config ing...");

    while (1) {

        if (addr_family == AF_INET) {
            struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
            dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
            dest_addr_ip4->sin_family = AF_INET;
            dest_addr_ip4->sin_port = htons(PORT);
            ip_protocol = IPPROTO_IP;
        } else if (addr_family == AF_INET6) {
            bzero(&dest_addr.sin6_addr.un, sizeof(dest_addr.sin6_addr.un));
            dest_addr.sin6_family = AF_INET6;
            dest_addr.sin6_port = htons(PORT);
            ip_protocol = IPPROTO_IPV6;
        }

        int sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket created");

#if defined(CONFIG_EXAMPLE_IPV4) && defined(CONFIG_EXAMPLE_IPV6)
        if (addr_family == AF_INET6) {
            // Note that by default IPV6 binds to both protocols, it is must be disabled
            // if both protocols used at the same time (used in CI)
            int opt = 1;
            setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
            setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt));
        }
#endif

        int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err < 0) {
            ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        }
        ESP_LOGI(TAG, "Socket bound, port %d", PORT);

        while (1) {

            ESP_LOGI(TAG, "Waiting for data");
            struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
            socklen_t socklen = sizeof(source_addr);
            int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);

            // Error occurred during receiving
            if (len < 0) {
                ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
                break;
            }
            // Data received
            else {
                // Get the sender's ip address as string
                if (source_addr.ss_family == PF_INET) {
                    inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr, addr_str, sizeof(addr_str) - 1);
                } else if (source_addr.ss_family == PF_INET6) {
                    inet6_ntoa_r(((struct sockaddr_in6 *)&source_addr)->sin6_addr, addr_str, sizeof(addr_str) - 1);
                }

                rx_buffer[len] = 0; // Null-terminate whatever we received and treat like a string...
                ESP_LOGI(TAG, "Received %d bytes from %s:", len, addr_str);
                ESP_LOGI(TAG, "%s", rx_buffer);
            
                err = nvs_open("config", NVS_READWRITE, &nvs_handle);
                if (err != ESP_OK)
                    ESP_LOGE(TAG, "nvs open err = %d", err);

                err = nvs_set_str(nvs_handle, NVS_JSON_STR, rx_buffer);
                if (err != ESP_OK)
                    ESP_LOGE(TAG, "nvs set json err = %d", err);

                err = nvs_set_str(nvs_handle, NVS_ACTIVE_STR, NVS_TRUE_STR);
                if(err != ESP_OK)
                    ESP_LOGE(TAG, "nvs set active err = %d", err);
                nvs_commit(nvs_handle);
                nvs_close(nvs_handle);

                oled->printf(0, 0, 0, oled, "wifi config ok!");
                ESP_LOGI(TAG, "get json success, reboot now!");

                vTaskDelay(3000 / portTICK_RATE_MS);
                esp_restart();
            }
        }

        if (sock != -1) {
            ESP_LOGE(TAG, "Shutting down socket and restarting...");
            shutdown(sock, 0);
            close(sock);
        }
    }
    vTaskDelete(NULL);
}


/**
 * @brief 将nvs的标志位清空
 * 
 */
void wifi_wizard_reset(void)
{
    nvs_handle_t nvs_handle;

    nvs_open(NVS_NAME_STR, NVS_READWRITE, &nvs_handle);

    nvs_set_str(nvs_handle, NVS_ACTIVE_STR, NVS_FALSE_STR);

    nvs_commit(nvs_handle);

    nvs_close(nvs_handle);
}


char *wifi_get_url(void)
{
    return URL;
}


/**
 * @brief 获取是否处于sta模式
 * 
 */
bool wifi_get_sta_state(void)
{
    return sta_state;
}