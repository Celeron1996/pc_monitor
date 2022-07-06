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

/* Littlevgl specific */
#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif
#include "lvgl_helpers.h"

#ifndef CONFIG_LV_TFT_DISPLAY_MONOCHROME
    #if defined CONFIG_LV_USE_DEMO_WIDGETS
        #include "lv_examples/src/lv_demo_widgets/lv_demo_widgets.h"
    #elif defined CONFIG_LV_USE_DEMO_KEYPAD_AND_ENCODER
        #include "lv_examples/src/lv_demo_keypad_encoder/lv_demo_keypad_encoder.h"
    #elif defined CONFIG_LV_USE_DEMO_BENCHMARK
        #include "lv_examples/src/lv_demo_benchmark/lv_demo_benchmark.h"
    #elif defined CONFIG_LV_USE_DEMO_STRESS
        #include "lv_examples/src/lv_demo_stress/lv_demo_stress.h"
    #else
        #error "No demo application selected."
    #endif
#endif


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
static void create_demo_application(void);
static void my_style(void);
static void create_gui_task_loading(void);
static void gui_task_main(void *pvParameters);

/**
 * @brief 创建一个信号量来处理lvgl的并发调用，如果想从其他多个线程/任务调用lvgl
 * 函数或任务，应该向这个信号量发送消息
 */
SemaphoreHandle_t xGuiSemaphore;
/*---------------------------- LVGL about (end) -----------------------------*/

/*--------------------------- http client (begin) -----------------------------*/
#define MAX_HTTP_RECV_BUFFER_ (10*1024) /*用于存储PC httpServer接收到的json字符串的缓存的大小*/
static void http_task(void *pvParameters);  /*http任务，从PC http Server中读取信息json*/

/**
 * @brief 当连接上wifi之后，通过这个信号量通知http task，http task中得到信号量后再建立http client服务
 */
SemaphoreHandle_t xHttpSemaphore = NULL;
/*---------------------------- http client (end) ------------------------------*/

/*--------------------------- wifi (begin) ------------------------------------*/
#define ESP_AP_WIFI_SSID      "PCMonitor"   /*wifi ssid*/
#define ESP_AP_WIFI_PASS      ""            /*wifi pass*/
#define ESP_AP_WIFI_CHANNEL   1             /*wifi 通道数量*/
#define ESP_AP_MAX_STA_COUNT  1             /*wifi 处于ap模式时，最大可连接数量*/

static EventGroupHandle_t s_wifi_event_group;   /*标志wifi各种事件*/
#define WIFI_CONNECTED_BIT BIT0     /*连接 ap 成功事件标志位*/
#define WIFI_FAIL_BIT      BIT1     /*连接失败事件标志位*/
#define WIFI_AP_OK_BIT     BIT2     /*wifi ap 模式创建成功事件标志位*/

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
static const char *NVS_NAME_STR = "config";
static const char *NVS_ACTIVE_STR = "active";
static const char *NVS_TRUE_STR = "true";
static const char *NVS_FALSE_STR = "false";
static const char *NVS_JSON_STR = "json";

static char URL[64] = {0};  /*统一资源定位链接，当PC端生成了http server时，通过此url获取json字符串*/
static const char *TAG = "pc_monitor";

/*-------------------------------- my custom -------------------------------------*/
#define MY_COLOR_SKYBLUE    LV_COLOR_MAKE(135, 206, 235)
#define MY_COLOR_BACKBLACK    LV_COLOR_MAKE(233, 233, 233)

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

    xHttpSemaphore = xSemaphoreCreateBinary();
    if (xHttpSemaphore == NULL)
    {
        while (1);
    }

	button_init();

    pc_init();

    xTaskCreatePinnedToCore(guiTask, "guiTask", 4*1024, (void*)NULL, 0, NULL, 1);

    vTaskDelay(pdMS_TO_TICKS(3000));

    xTaskCreate(wifi_task, "wifi_task", 4*1024, (void*)NULL, 2, NULL);

    xTaskCreate(http_task, "http_task", 4*1024, (void*)NULL, 3, NULL);

}


/**
 * @brief lgvl线程
 * 
 */
void my_demo(void);
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
    //create_demo_application();
    my_demo();
    /*--------------my lvgl style init--------------*/
    my_style();
    /*----------------------------------------------*/

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

static void create_demo_application(void)
{
    /* When using a monochrome display we only show "Hello World" centered on the
     * screen */
#if defined CONFIG_LV_TFT_DISPLAY_MONOCHROME || \
    defined CONFIG_LV_TFT_DISPLAY_CONTROLLER_ST7735S

    /* use a pretty small demo for monochrome displays */
    /* Get the current screen  */
    lv_obj_t * scr = lv_disp_get_scr_act(NULL);

    /*Create a Label on the currently active screen*/
    lv_obj_t * label1 =  lv_label_create(scr, NULL);

    /*Modify the Label's text*/
    lv_label_set_text(label1, "Hello\nworld");

    /* Align the Label to the center
     * NULL means align on parent (which is the screen now)
     * 0, 0 at the end means an x, y offset after alignment*/
    lv_obj_align(label1, NULL, LV_ALIGN_CENTER, 0, 0);
#else
    /* Otherwise we show the selected demo */

    #if defined CONFIG_LV_USE_DEMO_WIDGETS
        lv_demo_widgets();
    #elif defined CONFIG_LV_USE_DEMO_KEYPAD_AND_ENCODER
        lv_demo_keypad_encoder();
    #elif defined CONFIG_LV_USE_DEMO_BENCHMARK
        lv_demo_benchmark();
    #elif defined CONFIG_LV_USE_DEMO_STRESS
        lv_demo_stress();
    #else
        #error "No demo application selected."
    #endif
#endif
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
 * @brief 线程：获取pc的json
 * 
 */
static void http_task(void *pvParameters)
{
    BaseType_t error = pdFALSE;
    char *buffer = NULL;
    esp_err_t err;
    uint8_t openerr_count = 0;
    int ip0,ip1,ip2,ip3,port;

    error = xSemaphoreTake(xHttpSemaphore, portMAX_DELAY);  /*当连接上wifi之后，会得到信号量*/
    if (error != pdTRUE)
    {
        while (true);
    }
    vSemaphoreDelete(xHttpSemaphore);

    xTaskCreate(gui_task_main, "gui_task_main", 4*1024, (void*)NULL, 1, NULL);

    esp_http_client_config_t config = {
        .url = URL,
    };
    ESP_LOGI(TAG, "url:%s", config.url);
    esp_http_client_handle_t client;

    while (true)
    {
    	client = esp_http_client_init(&config);
    	if (client != NULL)
    	{
    		esp_http_client_set_timeout_ms(client, 1000);
    		if ((err = esp_http_client_open(client, 0)) == ESP_OK)
    		{
		        int content_length =  esp_http_client_fetch_headers(client);
		        buffer = malloc(content_length + 64);
				if (buffer == NULL) {
					ESP_LOGE(TAG, "malloc failed!");
					esp_http_client_close(client);
				}
				else
				{
					memset(buffer, 0, content_length);
					int total_read_len = 0, read_len;
			        if (total_read_len < content_length && content_length <= MAX_HTTP_RECV_BUFFER_) {
			            read_len = esp_http_client_read(client, buffer, content_length);
			            if (read_len <= 0) {
			                ESP_LOGE(TAG, "Error read data");
			            }
			            ESP_LOGD(TAG, "read_len = %d", read_len);
			        }
			        ESP_LOGI(TAG, "HTTP Stream reader Status = %d, content_length = %d,url:%s",
			                        esp_http_client_get_status_code(client),
			                        esp_http_client_get_content_length(client),
			                        URL);

			        pc_analysis_json(buffer);
			        free(buffer);

			        esp_http_client_close(client);    						
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
    					ip3 = 0;
    				}
    			}
    		}
    	}
    	else
    	{
    		ESP_LOGE(TAG, "esp_http_client_init error");
    	}

    	esp_http_client_cleanup(client);
    	vTaskDelay(pdMS_TO_TICKS(1500));

    	if (openerr_count >= 5)
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


/**
 * @brief 线程：获取pc的json
 * 
 */
static void http_task2(void *pvParameters)
{
    BaseType_t error = pdFALSE;
    char *buffer = NULL;

    error = xSemaphoreTake(xHttpSemaphore, portMAX_DELAY);  /*当连接上wifi之后，会得到信号量*/
    if (error != pdTRUE)
    {
        while (true);
    }
    vSemaphoreDelete(xHttpSemaphore);

    xTaskCreate(gui_task_main, "gui_task_main", 4*1024, (void*)NULL, 1, NULL);
    
	/*
    buffer = malloc(MAX_HTTP_RECV_BUFFER_);
	if (buffer == NULL) {
		ESP_LOGE(TAG, "Cannot malloc http receive buffer");
		while (true);
	}
	*/
    
    esp_http_client_config_t config = {
        .url = URL,
    };
    ESP_LOGI(TAG, "url:%s", config.url);
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL)
    {
        while (true)
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
            ESP_LOGE(TAG, "esp_http_client_init error");
        }
    }

    while (true) {
        esp_err_t err;
        
        while ((err = esp_http_client_open(client, 0)) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
            //free(buffer);
            //return;
            esp_http_client_close(client);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        int content_length =  esp_http_client_fetch_headers(client);
        buffer = malloc(content_length + 64);
		if (buffer == NULL) {
			ESP_LOGE(TAG, "Cannot malloc http receive buffer");
			esp_http_client_close(client);
			vTaskDelay(pdMS_TO_TICKS(1500));
			continue;
		}
		memset(buffer, 0, content_length);

        int total_read_len = 0, read_len;
        if (total_read_len < content_length && content_length <= MAX_HTTP_RECV_BUFFER_) {
            read_len = esp_http_client_read(client, buffer, content_length);
            if (read_len <= 0) {
                ESP_LOGE(TAG, "Error read data");
            }
            ESP_LOGD(TAG, "read_len = %d", read_len);
        }
        ESP_LOGI(TAG, "HTTP Stream reader Status = %d, content_length = %d",
                        esp_http_client_get_status_code(client),
                        esp_http_client_get_content_length(client));

        pc_analysis_json(buffer);
        free(buffer);

        esp_http_client_close(client);
             
        vTaskDelay(pdMS_TO_TICKS(1500));
    }

    esp_http_client_cleanup(client);   

    //free(buffer);
    ESP_LOGI(TAG, "Finish http example");
    vTaskDelete(NULL);
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

    s_wifi_event_group = xEventGroupCreate();   /*事件标记组，等待连接上wifi或失败的事件*/

    err = nvs_open("config", NVS_READWRITE, &nvs_handle);
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs_open error, code = %d", err);
        while(1);
    }

    length = sizeof(buffer);
    err = nvs_get_str(nvs_handle, NVS_ACTIVE_STR, buffer, &length);
    nvs_close(nvs_handle);
    if(err == ESP_OK)
    {
        if(memcmp(buffer, NVS_TRUE_STR, strlen(NVS_TRUE_STR)) == 0)
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

    xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);
    vEventGroupDelete(s_wifi_event_group);

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
    xEventGroupWaitBits(s_wifi_event_group,
            WIFI_AP_OK_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);
    xTaskCreate(udp_server_task, "udp_server", 8*1024, (void*)AF_INET, 1, NULL);
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
    /*--------------------step1:首先，从nvs中获取wifi名称、密码，ip地址等--------------*/
    esp_err_t err;
    char buffer[128];
    size_t length;
    nvs_handle_t nvs_handle;



    /*打开nvs*/
    err = nvs_open(NVS_NAME_STR, NVS_READWRITE, &nvs_handle);
    if(err != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs_open error, code = %d", err);
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
        xSemaphoreGive(xHttpSemaphore); /*向http发送信号，告诉它已连接上wifi，可以开始http client任务了*/
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

    return;
    
    flag_err:
    nvs_set_str(nvs_handle, NVS_ACTIVE_STR, NVS_FALSE_STR);
    nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
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
    char rx_buffer[128];
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
void wifi_reset(void)
{
    nvs_handle_t nvs_handle;

    nvs_open(NVS_NAME_STR, NVS_READWRITE, &nvs_handle);

    nvs_set_str(nvs_handle, NVS_ACTIVE_STR, NVS_FALSE_STR);

    nvs_commit(nvs_handle);

    nvs_close(nvs_handle);
}








/********************************my demo******************************************************/
lv_obj_t * lmeter;
lv_obj_t * label1;

static void my_demo_task(void *pvParameters)
{
    int32_t value = 0;
    bool is_add = true;

    while (true)
    {
        lv_linemeter_set_value(lmeter, value);
        vTaskDelay(pdMS_TO_TICKS(20));

        if (value > 100)
        {
            is_add = false;
        }
        else if (value < 0)
        {
            is_add = true;
        }
        if (is_add)
        {
            value++;
        }
        else
        {
            value--;
        }
    }
}

void linemeter_demo(void)
{
    /*Create a line meter */
    
    lmeter = lv_linemeter_create(lv_scr_act(), NULL);
    lv_linemeter_set_range(lmeter, 0, 100);                   /*Set the range*/
    lv_linemeter_set_value(lmeter, 80);                       /*Set the current value*/
    lv_linemeter_set_scale(lmeter, 240, 30);                  /*Set the angle and number of lines*/
    lv_obj_set_size(lmeter, 240, 240);
    lv_obj_align(lmeter, NULL, LV_ALIGN_CENTER, 0, 0);

    label1 = lv_label_create(lmeter, NULL);
    lv_label_set_text(label1, "Hello World");
    lv_obj_align(label1, NULL, LV_ALIGN_CENTER, 0, 0);

    xTaskCreate(my_demo_task, "my_demo_task", 1024*10, (void*)NULL, 0, NULL);
}

void image_demo(void)
{
    LV_IMG_DECLARE(girl);
    lv_obj_t * img1 = lv_img_create(lv_scr_act(), NULL);
    lv_img_set_src(img1, &girl);
    lv_obj_align(img1, NULL, LV_ALIGN_CENTER, 0, 0);
    lv_img_set_auto_size(img1, true);
}


/**
 * Add a faded area effect to the line chart
 */
void lv_ex_chart_2(void)
{
    /*Create a chart*/
    lv_obj_t * chart;
    chart = lv_chart_create(lv_scr_act(), NULL);
    lv_obj_set_size(chart, 240, 120);
    lv_obj_align(chart, NULL, LV_ALIGN_IN_BOTTOM_LEFT, 0, 0);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);   /*Show lines and points too*/

    /*Add a faded are effect*/
    lv_obj_set_style_local_bg_opa(chart, LV_CHART_PART_SERIES, LV_STATE_DEFAULT, LV_OPA_50); /*Max. opa.*/
    lv_obj_set_style_local_bg_grad_dir(chart, LV_CHART_PART_SERIES, LV_STATE_DEFAULT, LV_GRAD_DIR_VER);
    lv_obj_set_style_local_bg_main_stop(chart, LV_CHART_PART_SERIES, LV_STATE_DEFAULT, 255);    /*Max opa on the top*/
    lv_obj_set_style_local_bg_grad_stop(chart, LV_CHART_PART_SERIES, LV_STATE_DEFAULT, 0);      /*Transparent on the bottom*/


    /*Add two data series*/
    lv_chart_series_t * ser1 = lv_chart_add_series(chart, LV_COLOR_RED);
    lv_chart_series_t * ser2 = lv_chart_add_series(chart, LV_COLOR_GREEN);

    /*Set the next points on 'ser1'*/
    lv_chart_set_next(chart, ser1, 31);
    lv_chart_set_next(chart, ser1, 66);
    lv_chart_set_next(chart, ser1, 10);
    lv_chart_set_next(chart, ser1, 89);
    lv_chart_set_next(chart, ser1, 63);
    lv_chart_set_next(chart, ser1, 56);
    lv_chart_set_next(chart, ser1, 32);
    lv_chart_set_next(chart, ser1, 35);
    lv_chart_set_next(chart, ser1, 57);
    lv_chart_set_next(chart, ser1, 85);

    /*Directly set points on 'ser2'*/
    ser2->points[0] = 92;
    ser2->points[1] = 71;
    ser2->points[2] = 61;
    ser2->points[3] = 15;
    ser2->points[4] = 21;
    ser2->points[5] = 35;
    ser2->points[6] = 35;
    ser2->points[7] = 58;
    ser2->points[8] = 31;
    ser2->points[9] = 53;

    lv_obj_t *label = lv_label_create(chart, NULL);
    lv_label_set_text(label, "cpu");
    lv_obj_align(label, NULL, LV_ALIGN_IN_TOP_RIGHT, 0 ,0);

    lv_chart_refresh(chart); /*Required after direct set*/
}
void my_demo(void)
{
    //linemeter_demo();
    //image_demo();
    //create_gui_task_loading();

    //gui_task_main(NULL);

    //lv_ex_chart_2();
}

/**
 * @brief 设置主屏幕样式
 * 
 */
static void my_style(void)
{
    
    lv_obj_t *obj = lv_scr_act();

    // lv_style_init(&style_main);

    // lv_style_set_bg_color(&style_main, LV_STATE_DEFAULT, LV_COLOR_BLACK);
    // lv_style_set_value_color(&style_main, LV_STATE_DEFAULT, LV_COLOR_BLACK);
    // lv_style_set_text_color(&style_main, LV_STATE_DEFAULT, LV_COLOR_WHITE);
    // lv_style_set_line_color(&style_main, LV_STATE_DEFAULT, LV_COLOR_BLACK);

    // lv_obj_add_style(obj, LV_OBJ_PART_MAIN, &style_main);
}


/**
 * @brief 连接wifi->连接http server，这两个步骤，用gui显示出来
 *          用信号量来判断进度
 * 
 */
static void create_gui_task_loading(void)
{
     lv_style_t style_spinner;
     lv_style_init(&style_spinner);

    lv_style_set_line_width(&style_spinner, LV_STATE_DEFAULT, 10);
    lv_style_set_border_width(&style_spinner, LV_STATE_DEFAULT, 10);

    /*Create a spinner*/
    lv_obj_t * spinner = lv_spinner_create(lv_scr_act(), NULL);
    lv_obj_t *label = lv_label_create(lv_scr_act(), NULL);

    //lv_obj_add_style(spinner, LV_OBJ_PART_ALL, &style_spinner);

    lv_obj_set_size(spinner, 100, 100);
    lv_obj_align(spinner, NULL, LV_ALIGN_CENTER, 0, 0);
    lv_spinner_set_type(spinner, LV_SPINNER_TYPE_FILLSPIN_ARC);


    lv_obj_align(label, NULL, LV_ALIGN_IN_BOTTOM_MID, -50, 0);
    lv_label_set_text(label, "connecting wifi...");


}

/**
 * @brief 创建主gui图标，用于显示温度
 * 
 */
static void create_gui_task_main(void)
{

}


/**
 * @brief 
 * 
 */
static void gui_task_main(void *pvParameters)
{
    int cpu_temper = 0;
    int gpu_temper = 0;
    int ram_load = 0;
    int cpu_load = 0;
    int gpu_load = 0;

    /* 设置表盘样式 */
    static lv_style_t style_cpu_lmeter;
    static lv_style_t style_gpu_lmeter;
    lv_style_init(&style_cpu_lmeter);
    lv_style_set_line_width(&style_cpu_lmeter, LV_STATE_DEFAULT, 4);    /*设置表盘小于value的刻度宽度*/
    lv_style_set_scale_end_line_width(&style_cpu_lmeter, LV_STATE_DEFAULT, 4);    /*设置表盘大于value的刻度宽度*/
    lv_style_set_scale_width(&style_cpu_lmeter, LV_STATE_DEFAULT, 16);  /*设置全部刻度线的长度*/
    lv_style_set_line_color(&style_cpu_lmeter, LV_STATE_DEFAULT, MY_COLOR_SKYBLUE);/*设置刻度小于value部分刻度线的颜色*/
    lv_style_set_scale_end_color(&style_cpu_lmeter, LV_STATE_DEFAULT, MY_COLOR_BACKBLACK);/*设置刻度大于value部分刻度线的颜色*/
    lv_style_copy(&style_gpu_lmeter, &style_cpu_lmeter);
    
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

    /* 创建GPU 表盘 */
    lv_obj_t *lmeter_gpu = lv_linemeter_create(lv_scr_act(), NULL);
    lv_linemeter_set_range(lmeter_gpu, 0, 100);                   /*Set the range*/
    lv_linemeter_set_value(lmeter_gpu, 80);                       /*Set the current value*/
    lv_linemeter_set_scale(lmeter_gpu, 280, 100);                  /*Set the angle and number of lines*/
    lv_obj_set_size(lmeter_gpu, 120, 120);
    lv_obj_align(lmeter_gpu, NULL, LV_ALIGN_IN_TOP_RIGHT, 0, 0);

    lv_obj_t *label_gpu_title = lv_label_create(lmeter_gpu, NULL);
    lv_label_set_text(label_gpu_title, "GPU");
    lv_obj_align(label_gpu_title, NULL, LV_ALIGN_IN_BOTTOM_MID, 0, -16);

    lv_obj_t *label_gpu_temper = lv_label_create(lmeter_gpu, NULL);
    lv_label_set_text_fmt(label_gpu_temper, "%s", pc_get_gpu_temperature_str());
    lv_obj_align(label_gpu_temper, NULL, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_style(lmeter_gpu, LV_OBJ_PART_MAIN, &style_gpu_lmeter);

    /* 创建图表 */
    /*Create a chart*/
    lv_obj_t * chart;
    chart = lv_chart_create(lv_scr_act(), NULL);
    lv_obj_set_size(chart, 240, 120);
    lv_obj_align(chart, NULL, LV_ALIGN_IN_BOTTOM_LEFT, 0, 0);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);   /*Show lines and points too*/
    lv_chart_set_div_line_count(chart, 0, 0);

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
    lv_label_set_recolor(label_chart, true);
    lv_label_set_text_fmt(label_chart, "#ff0000 CPU:%s# #00ff00 GPU:%s#\n#0000ff RAM:%s",
                            pc_get_cpu_load_str(), pc_get_gpu_load_str(), pc_get_ram_load_str());
    lv_obj_align(label_chart, NULL, LV_ALIGN_IN_TOP_LEFT, 0 ,0);

    while (true)
    {
        /* get data */
        cpu_temper = pc_get_cpu_temperature();
        gpu_temper = pc_get_gpu_temperature();
        if (!(pc_is_cpu_temperature_unit_celsius()))
        {
            cpu_temper = ((cpu_temper - 32)*5)/9;   /*表盘是满了是100，按照摄氏度来的，华氏度的话转成摄氏度: 华氏度 = 32℉ + (摄氏度 * 1.8) */
        }
        if (!(pc_is_gpu_temperature_unit_celsius()))
        {
            gpu_temper = ((gpu_temper - 32)*5)/9;   /*表盘是满了是100，按照摄氏度来的，华氏度的话转成摄氏度: 华氏度 = 32℉ + (摄氏度 * 1.8) */
        }

        ram_load = pc_get_ram_load();
        cpu_load = pc_get_cpu_load();
        gpu_load = pc_get_gpu_load();

        /* set data to gui */
        lv_linemeter_set_value(lmeter_cpu, cpu_temper);
        lv_linemeter_set_value(lmeter_gpu, gpu_temper);
        lv_label_set_text_fmt(label_cpu_temper, "%s", pc_get_cpu_temperature_str());
        lv_label_set_text_fmt(label_gpu_temper, "%s", pc_get_gpu_temperature_str());
        lv_obj_align(label_cpu_temper, NULL, LV_ALIGN_CENTER, 0, 0);
        lv_obj_align(label_gpu_temper, NULL, LV_ALIGN_CENTER, 0, 0);

        lv_label_set_text_fmt(label_chart, "#ff0000 CPU:%s# #00ff00 GPU:%s#\n#0000ff RAM:%s",
                                pc_get_cpu_load_str(), pc_get_gpu_load_str(), pc_get_ram_load_str());

        lv_chart_set_next(chart, chart_ser_cpu_load, cpu_load);
        lv_chart_set_next(chart, chart_ser_gpu_load, gpu_load);
        lv_chart_set_next(chart, chart_ser_ram_load, ram_load);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
