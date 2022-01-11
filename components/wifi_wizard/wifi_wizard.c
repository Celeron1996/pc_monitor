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

/**
 * @brief wifi 配置向导，用于获取wifi名称、密码，并进行连接配置
 * 
 */
void wifi_wizard_init(void)
{
    oled_handler_p oled;
    oled = oled_get_handler();

    oled->clear(oled);
    oled->printf(0, 0, 0, oled, "wifi wizard init");
    vTaskDelay(1000 / portTICK_RATE_MS);
}

/**
 * @brief wifi 连接向导，将已存储在flash的wifi名称和密码取出，并连接
 * 
 */
void wifi_wizard_connect(void)
{

}