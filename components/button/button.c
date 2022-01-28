#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "button.h"

#include "wifi_wizard.h"

static const char *TAG = "button";

#define BUTTON_SET_GPIO_PIN 4
#define BUTTON_SET_GPIO_MASK    (1ULL << BUTTON_SET_GPIO_PIN)
#define ESP_INTR_FLAG_DEFAULT 0

static xQueueHandle button_evt_queue = NULL;

TaskHandle_t button_task_handle;

static void IRAM_ATTR button_isr_handler(void* arg)
{
    BaseType_t pxHigherPriorityTaskWoken;
    vTaskNotifyGiveFromISR(button_task_handle, &pxHigherPriorityTaskWoken);
}

static void button_task(void* arg)
{
    while(1) 
    {
        if(ulTaskNotifyTake(pdTRUE, portMAX_DELAY)) {
            ESP_LOGI(TAG, "button triger");

            vTaskDelay(100 / portTICK_RATE_MS); /*消抖*/
            size_t counter = 0;
            while(gpio_get_level(BUTTON_SET_GPIO_PIN) == 0)
            {
                counter++;
                if(counter >= 10)
                {
                    ESP_LOGI(TAG, "button reset operation");
                    wifi_wizard_reset();
                    ESP_LOGI(TAG, "will be reboot");
                    vTaskDelay(3000 / portTICK_RATE_MS);
                    esp_restart();
                }
                else
                {
                    ESP_LOGI(TAG, "button counter = %d", counter);
                }
                vTaskDelay(500 / portTICK_RATE_MS);
            }
        }
    }
}

void button_init(void)
{
    gpio_config_t io_conf;

    //interrupt of rising edge
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    //bit mask of the pins
    io_conf.pin_bit_mask = BUTTON_SET_GPIO_MASK;
    //set as input mode
    io_conf.mode = GPIO_MODE_INPUT;
    //enable pull-up mode
    io_conf.pull_up_en = 1;
    //disable pull-down mode
    io_conf.pull_down_en = 0;
    gpio_config(&io_conf);

    //create a queue to handle gpio event from isr
    button_evt_queue = xQueueCreate(10, sizeof(uint32_t));

    //start gpio task
    xTaskCreate(button_task, "button_task", 2048, NULL, 10, &button_task_handle);

    //install gpio isr service
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);

    //hook isr handler for specific gpio pin
    gpio_isr_handler_add(BUTTON_SET_GPIO_PIN, button_isr_handler, (void*) BUTTON_SET_GPIO_PIN);
}
