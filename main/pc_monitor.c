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


void app_main(void)
{
	oled_handler_p oled;

	if( oled_init(&oled) != oled_ok)
		while(1);

	int i;
	while (1) {
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
