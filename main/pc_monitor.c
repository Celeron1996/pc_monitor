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

#include "button.h"

#include "cJSON.h"

#include "pc.h"

#include "weather.h"

#include "my_sntp.h"

#include <time.h>
#include <sys/time.h>

#include "watchdog.h"

#include "daily_word.h"

/* Littlevgl specific */
#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif
#include "lvgl_helpers.h"


/*---------------------------- LVGL about (begin) ----------------------------*/
/**********************
 *  STATIC PROTOTYPES
 **********************/
#define LV_TICK_PERIOD_MS 1

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void lv_tick_task(void *arg);
static void guiTask(void *pvParameter);
static void reboot_task(void *pvParameters);
static void wifi_waiting_connect(const char *wifi_ssid);
void wifi_data_reset(void);


/**
 * @brief 创建一个信号量来处理lvgl的并发调用，如果想从其他多个线程/任务调用lvgl
 * 函数或任务，应该向这个信号量发送消息
 */
SemaphoreHandle_t xGuiSemaphore;
/*---------------------------- LVGL about (end) -----------------------------*/



/*--------------------------- wifi (begin) ------------------------------------*/
#define ESP_AP_WIFI_SSID      "PCMonitor"   /*wifi ssid*/
#define ESP_AP_WIFI_PASS      ""            /*wifi pass*/
#define ESP_AP_WIFI_CHANNEL   1             /*wifi 通道数量*/
#define ESP_AP_MAX_STA_COUNT  1             /*wifi 处于ap模式时，最大可连接数量*/

static EventGroupHandle_t task_event_group;   /*标志各种事件*/
#define WIFI_CONNECTED_BIT 						BIT0     /*连接 ap 成功事件标志位*/
#define WIFI_FAIL_BIT      						BIT1     /*连接失败事件标志位*/
#define WIFI_AP_OK_BIT     						BIT2     /*wifi ap 模式创建成功事件标志位*/
#define WIFI_CONNECTION_START_BIT			BIT3	/*开始连接*/
#define WIFI_DISCONNECTED_BIT					BIT4	/*断开连接*/
#define WIFI_AP_STADISCONNECTED_BIT		BIT5	/*设备断开热点*/
#define WIFI_AP_STACONNECTED_BIT			BIT6	/*设备加入热点*/
#define WIFI_INIT_OK_BIT							BIT8	/*WIFI任务处理完成*/


#define ESP_STA_MAXIMUM_RETRY  16  /*wifi重连次数*/
static int s_retry_num = 0; /*用于记录重连次数*/

#define UDP_SERVER_PORT (8848)  /*在配网时，开启udp server的端口*/
/* 函数声明 */
static void udp_server_task(void *pvParameters);    /*在配网时通过udp server接收来自手机小程序的wifi信息*/
static void wifi_init_softap(void); /*wifi 初始化为ap模式，接收来自手机的udp包，用于配网*/
static void wifi_config(void);  /*wfii 配网函数，调用wifi_init_softap*/
static void wifi_connect(void); /*wifi 连接路由器函数，调用wifi_init_sta*/
static void wifi_init_sta(void);    /*wifi 初始化为sta模式，用于连接wfii路由器*/
static void wifi_task(void *pvParameters);    /*wifi主任务，用于判断wifi是否应该配网，还是直接连接路由器*/
static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
/*---------------------------------- wifi (end) ----------------------------*/

/*存储非易失性存储的字符串*/
const char *NVS_HANDLER = "config";
const char *NVS_KEY_ACTIVE = "active";
const char *NVS_VALUE_TRUE = "true";
const char *NVS_VALUE_FALSE = "false";
const char *NVS_KEY_JSON = "json";

static const char *TAG = "pc_monitor";


/**
 * @brief 应用主函数
 * 
 */
void app_main(void)
{
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    task_event_group = xEventGroupCreate();   /*事件标记组，等待连接上wifi或失败的事件*/

		button_init();

    xTaskCreate(guiTask, "guiTask", 4*1024, (void*)NULL, 0, NULL);

    vTaskDelay(pdMS_TO_TICKS(2000));

    xTaskCreate(wifi_task, "wifi_task", 4*1024, (void*)NULL, 0, NULL);

	  xEventGroupWaitBits(task_event_group,
	          WIFI_INIT_OK_BIT,
	          pdFALSE,
	          pdTRUE,
	          portMAX_DELAY);    
    
		//xTaskCreate(watchdog_task, "watchdog_task", 1*1024, (void*)NULL, 0, NULL);
		
    xTaskCreate(pc_task, "pc_task", 6*1024, (void*)NULL, 1, NULL);

		xTaskCreate(weather_task, "weather_task", 4*1024, (void*)NULL, 3, NULL);
		
    xTaskCreate(sntp_task, "sntp_task", 4*1024, (void*)NULL, 4, NULL);

    //xTaskCreate(reboot_task, "reboot_task", 1*1024, (void*)NULL, 0, NULL);

		//xTaskCreate(daily_word_task, "daily_word_task", 4*1024, (void*)NULL, 0, NULL);
		
}


/**
 * @brief lgvl线程
 * 
 */
static void guiTask(void *pvParameter) {

    (void) pvParameter;
    xGuiSemaphore = xSemaphoreCreateMutex();

    lv_init();

    /* Initialize SPI or I2C bus used by the drivers */
    lvgl_driver_init();

    lv_color_t* buf1 = heap_caps_malloc(DISP_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA);
    assert(buf1 != NULL);

    /* Use double buffered when not working with monochrome displays */
#ifndef CONFIG_LV_TFT_DISPLAY_MONOCHROME
    lv_color_t* buf2 = heap_caps_malloc(DISP_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA);
    assert(buf2 != NULL);
#else
    static lv_color_t *buf2 = NULL;
#endif

    static lv_disp_buf_t disp_buf;

    uint32_t size_in_px = DISP_BUF_SIZE;

#if defined CONFIG_LV_TFT_DISPLAY_CONTROLLER_IL3820         \
    || defined CONFIG_LV_TFT_DISPLAY_CONTROLLER_JD79653A    \
    || defined CONFIG_LV_TFT_DISPLAY_CONTROLLER_UC8151D     \
    || defined CONFIG_LV_TFT_DISPLAY_CONTROLLER_SSD1306

    /* Actual size in pixels, not bytes. */
    size_in_px *= 8;
#endif

    /* Initialize the working buffer depending on the selected display.
     * NOTE: buf2 == NULL when using monochrome displays. */
    lv_disp_buf_init(&disp_buf, buf1, buf2, size_in_px);

    lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.flush_cb = disp_driver_flush;

    /*celeron: my init code*/
    disp_drv.sw_rotate = 1;
    disp_drv.rotated = LV_DISP_ROT_90;
    /*---------------------*/

    /* When using a monochrome display we need to register the callbacks:
     * - rounder_cb
     * - set_px_cb */
#ifdef CONFIG_LV_TFT_DISPLAY_MONOCHROME
    disp_drv.rounder_cb = disp_driver_rounder;
    disp_drv.set_px_cb = disp_driver_set_px;
#endif

    disp_drv.buffer = &disp_buf;
    lv_disp_drv_register(&disp_drv);

    /* Register an input device when enabled on the menuconfig */
#if CONFIG_LV_TOUCH_CONTROLLER != TOUCH_CONTROLLER_NONE
    lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.read_cb = touch_driver_read;
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    lv_indev_drv_register(&indev_drv);
#endif

    /* Create and start a periodic timer interrupt to call lv_tick_inc */
    const esp_timer_create_args_t periodic_timer_args = {
        .callback = &lv_tick_task,
        .name = "periodic_gui"
    };
    esp_timer_handle_t periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, LV_TICK_PERIOD_MS * 1000));

    /* Create the demo application */
    //...

    while (1) {
        /* Delay 1 tick (assumes FreeRTOS tick is 10ms */
        vTaskDelay(pdMS_TO_TICKS(10));

        /* Try to take the semaphore, call lvgl related function on success */
        if (pdTRUE == xSemaphoreTake(xGuiSemaphore, portMAX_DELAY)) {
            lv_task_handler();
            xSemaphoreGive(xGuiSemaphore);
       }
    }

    /* A task should NEVER return */
    free(buf1);
#ifndef CONFIG_LV_TFT_DISPLAY_MONOCHROME
    free(buf2);
#endif
    vTaskDelete(NULL);
}


/**
 * @brief lvgl心跳计数函数，告诉lvgl经过了1ms
 * 
 */
static void lv_tick_task(void *arg) {
    (void) arg;

    lv_tick_inc(LV_TICK_PERIOD_MS);
}


/**
 * @brief wifi 配置向导，用于获取wifi名称、密码，并进行连接配置
 * 
 */
void wifi_task(void *pvParameters)
{
    esp_err_t err;
    char buffer[16];
    size_t length;
    nvs_handle_t nvs_handle;

    err = nvs_open("config", NVS_READWRITE, &nvs_handle);
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs_open error, code = %d", err);
        while(1);
    }

    length = sizeof(buffer);
    err = nvs_get_str(nvs_handle, NVS_KEY_ACTIVE, buffer, &length);
    nvs_close(nvs_handle);
    if(err == ESP_OK)
    {
        if(memcmp(buffer, NVS_VALUE_TRUE, strlen(NVS_VALUE_TRUE)) == 0)
        {
            /*存储的wifi是正确的，可以使用*/
            wifi_connect();
        }
        else
        {
            /*存储的信息无效，进行配网流程*/
            wifi_config();
        }
    }
    else
    {
        /*可能是没有建立此标签，第一次使用？重新配置一遍*/
        wifi_config();
    }

    vTaskDelete(NULL);
}

/**
 * @brief wifi ap + udp server，receive wifi info from wechat miniprogram
 * 
 */
void wifi_config(void)
{
    wifi_init_softap();

    //vTaskDelay(500 / portTICK_RATE_MS);
    xEventGroupWaitBits(task_event_group,
            WIFI_AP_OK_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);
    xTaskCreate(udp_server_task, "udp_server", 16*1024, (void*)AF_INET, 1, NULL);
}

/**
 * @brief wifi 连接向导，将已存储在flash的wifi名称和密码取出，并连接
 * 
 */
void wifi_connect(void)
{
    wifi_init_sta();
}

/**
 * @brief 事件处理，用于处理wifi在ap/sta模式下的事件
 * 
 */
static void event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
                 MAC2STR(event->mac), event->aid);
        xEventGroupSetBits(task_event_group, WIFI_AP_STACONNECTED_BIT);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
        xEventGroupSetBits(task_event_group, WIFI_AP_STADISCONNECTED_BIT);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START)
    {
        ESP_LOGI(TAG, "wifi ap start!");
        xEventGroupSetBits(task_event_group, WIFI_AP_OK_BIT);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
    		ESP_LOGI(TAG, "wifi connect start!");
        esp_wifi_connect();
        xEventGroupSetBits(task_event_group, WIFI_CONNECTION_START_BIT);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        esp_wifi_connect();
        ESP_LOGI(TAG,"connect to the AP fail");
        xEventGroupSetBits(task_event_group, WIFI_DISCONNECTED_BIT);
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "connect wifi success ! got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(task_event_group, WIFI_CONNECTED_BIT);
    }
}

/**
 * @brief wifi初始化为ap模式，用于配网
 * 
 */
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

/**
 * @brief wifi初始化为sta模式，用于连接路由器
 * 
 */
void wifi_init_sta(void)
{
    /*--------------------step1:首先，从nvs中获取json--------------*/
    esp_err_t err;
    char *buffer;
    size_t length = 8*1024;
    nvs_handle_t nvs_handle;

    /*打开nvs*/
    err = nvs_open(NVS_HANDLER, NVS_READWRITE, &nvs_handle);
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs_open error, code = %d", err);
        while(1);
    }
    
		buffer = (char *)malloc(length);
		if (buffer == NULL)
		{
			ESP_LOGE(TAG, "wifi_init_sta malloc error");
			nvs_close(nvs_handle);
			return ;
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
        goto flag_err;
    }

    /*------------------------step2:获取成功，解析json----------------------------------*/
    cJSON *json_root = cJSON_Parse(buffer);
    cJSON *json_wifi_ssid = cJSON_GetObjectItem(json_root, "wifi_ssid");
    cJSON *json_wifi_pass = cJSON_GetObjectItem(json_root, "wifi_pass");

    if (json_root == NULL)
    {
    	ESP_LOGE(TAG, "wifi JSON data error! will be reset wifi and reboot!");
    	wifi_data_reset();
    	vTaskDelay(pdMS_TO_TICKS(1000));
    	esp_restart();
    }

    ESP_LOGI(TAG, "json data:%s", cJSON_Print(json_root));

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
    wifi_waiting_connect((const char *)wifi_config.sta.ssid);
    EventBits_t bits = xEventGroupGetBits(task_event_group);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 wifi_config.sta.ssid, wifi_config.sta.password);
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

    cJSON_Delete(json_root);

    free(buffer);

    xEventGroupSetBits(task_event_group, WIFI_INIT_OK_BIT);

    return;
    
    flag_err:
    nvs_set_str(nvs_handle, NVS_KEY_ACTIVE, NVS_VALUE_FALSE);
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    free(buffer);
    ESP_LOGE(TAG, "wifi connect error, will be reboot");
    vTaskDelay(4000 / portTICK_RATE_MS);
    esp_restart();
}

/**
 * @brief 在配网时通过udp server接收来自手机小程序的wifi信息
 * 
 */
static void udp_server_task(void *pvParameters)
{
    char rx_buffer[8*1024];
    char addr_str[128];
    int addr_family = (int)pvParameters;
    int ip_protocol = 0;
    struct sockaddr_in6 dest_addr;
    nvs_handle_t nvs_handle;

    LV_IMG_DECLARE(girl);
    lv_obj_t * img1 = lv_img_create(lv_scr_act(), NULL);
    lv_img_set_src(img1, &girl);
    lv_img_set_auto_size(img1, true);

    while (1) {

        if (addr_family == AF_INET) {
            struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
            dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
            dest_addr_ip4->sin_family = AF_INET;
            dest_addr_ip4->sin_port = htons(UDP_SERVER_PORT);
            ip_protocol = IPPROTO_IP;
        } else if (addr_family == AF_INET6) {
            bzero(&dest_addr.sin6_addr.un, sizeof(dest_addr.sin6_addr.un));
            dest_addr.sin6_family = AF_INET6;
            dest_addr.sin6_port = htons(UDP_SERVER_PORT);
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
        ESP_LOGI(TAG, "Socket bound, port %d", UDP_SERVER_PORT);

				memset(rx_buffer, 0, sizeof(rx_buffer));
				
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

                err = nvs_set_str(nvs_handle, NVS_KEY_JSON, rx_buffer);
                if (err != ESP_OK)
                    ESP_LOGE(TAG, "nvs set json err = %d", err);

                err = nvs_set_str(nvs_handle, NVS_KEY_ACTIVE, NVS_VALUE_TRUE);
                if(err != ESP_OK)
                    ESP_LOGE(TAG, "nvs set active err = %d", err);
                nvs_commit(nvs_handle);
                nvs_close(nvs_handle);

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
void wifi_data_reset(void)
{
    nvs_handle_t nvs_handle;

    nvs_open(NVS_HANDLER, NVS_READWRITE, &nvs_handle);

    nvs_set_str(nvs_handle, NVS_KEY_ACTIVE, NVS_VALUE_FALSE);

    nvs_commit(nvs_handle);

    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "wifi_data_reset");
}

static void reboot_task(void *pvParameters)
{
	int counter = 0;
	
	while (true)
	{
		watchdog_reset();
		
		vTaskDelay(pdMS_TO_TICKS(1000));
		counter++;

		if (counter >= 21600)
		{
			ESP_LOGI(TAG, "reboot_task esp restart");
			esp_restart();
		}
	}
}


static void wifi_waiting_connect(const char *wifi_ssid)
{
  lv_obj_t * bar1 = lv_bar_create(lv_scr_act(), NULL);
  lv_obj_set_size(bar1, 200, 30);
  lv_obj_align(bar1, NULL, LV_ALIGN_CENTER, 0, 0);
  lv_bar_set_value(bar1, 0, LV_ANIM_OFF);

  lv_obj_t * label = lv_label_create(lv_scr_act(), NULL);
  lv_obj_align(label, NULL, LV_ALIGN_CENTER, 0, 38);
  lv_obj_set_auto_realign(label, true);

	lv_label_set_text(label, "开始连接wifi...");
	lv_bar_set_value(bar1, 20, LV_ANIM_OFF); 
  xEventGroupWaitBits(task_event_group,
          WIFI_CONNECTION_START_BIT,
          pdFALSE,
          pdTRUE,
          portMAX_DELAY);
	         
	vTaskDelay(pdMS_TO_TICKS(200));

	lv_label_set_text_fmt(label, "正在连接至wifi:\n<%s>...", wifi_ssid);
	lv_bar_set_value(bar1, 50, LV_ANIM_OFF); 
  xEventGroupWaitBits(task_event_group,
          WIFI_CONNECTED_BIT,
          pdFALSE,
          pdTRUE,
          portMAX_DELAY);
          
	vTaskDelay(pdMS_TO_TICKS(1000));
  
	lv_label_set_text(label, "连接wifi成功");
	lv_bar_set_value(bar1, 100, LV_ANIM_OFF); 

  vTaskDelay(pdMS_TO_TICKS(2000));

  lv_obj_del(label);
  lv_obj_del(bar1);
}
