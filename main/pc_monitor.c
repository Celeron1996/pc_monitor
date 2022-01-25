#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "oled.h"
#include "wifi_wizard.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"

static const char *TAG = "pc_monitor";

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
    

	oled_handler_p oled;

	if( oled_init() != oled_ok)
		while(1);
	
	oled = oled_get_handler();

	wifi_wizard_init();

	//int i;
	while (1) {
		ESP_LOGI(TAG, "hello world");
		// memset(oled->buffer, 0x00, oled->bufsize);
		// vTaskDelay(500 / portTICK_RATE_MS);

		// memset(oled->buffer, 0xff, oled->bufsize);
		// vTaskDelay(500 / portTICK_RATE_MS);

		// memset(oled->buffer, 0x55, oled->bufsize);
		// vTaskDelay(500 / portTICK_RATE_MS);

		// memset(oled->buffer, 0xaa, oled->bufsize);
		// vTaskDelay(500 / portTICK_RATE_MS);

		// memset(oled->buffer, 0x11, oled->bufsize);
		// vTaskDelay(500 / portTICK_RATE_MS);

		// memset(oled->buffer, 0x88, oled->bufsize);
		// vTaskDelay(500 / portTICK_RATE_MS);

		vTaskDelay(500 / portTICK_RATE_MS);
		//oled->clear(oled);

		oled->printf(0, 0, 1, oled, "hello world! my name is zhu shi liang ! how are you? fucking your monther");
		
		 vTaskDelay(500 / portTICK_RATE_MS);

		 oled->printf(0, 0, 0, oled, "hello world! my name is zhu shi liang ! how are you? fucking your monther");
		// oled->clear(oled);

		// for (i = 0 ; i < 64;) {
		// 	oled->line(0, 0, i, 127, 1, oled);
		// 	vTaskDelay(50 / portTICK_RATE_MS);
		// 	i += 4;
		// }
		// for (i = 127; i > 0;) {
		// 	oled->line(0, 0, 63, i, 1, oled);
		// 	vTaskDelay(50 / portTICK_RATE_MS);
		// 	i -= 4;
		// }

		// vTaskDelay(500 / portTICK_RATE_MS);
		// oled->clear(oled);

		// for (i = 0 ; i < 64;) {
		// 	oled->rectangle(0, 0, i, i*2, 1, oled);
		// 	vTaskDelay(50 / portTICK_RATE_MS);
		// 	i += 4;
		// }

		// vTaskDelay(500 / portTICK_RATE_MS);
		// oled->clear(oled);

		// for (i = 0; i < 32;) {
		// 	oled->circle(31, 63, i, 1, oled);
		// 	vTaskDelay(80 / portTICK_RATE_MS);
		// 	i += 4;
		// }
	}
}
