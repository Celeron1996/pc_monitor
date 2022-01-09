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

static const char TAG[] = "oled";








static spi_device_handle_t spi;
static void oled_spi_init(void);
static void oled_gpio_init(void);
void oled_cmd(spi_device_handle_t spi, const uint8_t cmd);
void oled_data(spi_device_handle_t spi, const uint8_t *data, int len);
void oled_spi_pre_transfer_callback(spi_transaction_t *t);

void oled_init(void)
{
    oled_spi_init();
    oled_gpio_init();

    //Reset the display
    gpio_set_level(OLED_RS_PIN_NUM, 0);
    vTaskDelay(100 / portTICK_RATE_MS);
    gpio_set_level(OLED_RS_PIN_NUM, 1);
    vTaskDelay(100 / portTICK_RATE_MS);

	oled_cmd(spi, 0xAE); //Set Display Off
	oled_cmd(spi, 0xD5); //Display divide ratio/osc. freq. mode
	oled_cmd(spi, 0x80);
	oled_cmd(spi, 0xA8); //Multiplex ration mode:63
	oled_cmd(spi, 0x3F);
	oled_cmd(spi, 0xD3); //Set Display Offset
	oled_cmd(spi, 0x00);
	oled_cmd(spi, 0x40); //Set Display Start Line
	oled_cmd(spi, 0x8D); //Charge Pump Setting
	oled_cmd(spi, 0x14); //Enable charge pump during display on
	//oled_write_byte(0x10); //Disable charge pump(RESET)
	oled_cmd(spi, 0xA1); //Segment Remap
	oled_cmd(spi, 0xC8); //Set COM Output Scan Direction
	oled_cmd(spi, 0xDA); //Common pads hardware: alternative
	oled_cmd(spi, 0x12);
	oled_cmd(spi, 0x81); //Contrast control
	oled_cmd(spi, 0xCF);
	oled_cmd(spi, 0xD9); //Set pre-charge period
	oled_cmd(spi, 0xF1);
	oled_cmd(spi, 0xDB); //VCOM deselect level mode
	oled_cmd(spi, 0x40); //Set Vcomh
	oled_cmd(spi, 0xA4); //Set Entire Display On/Off
	oled_cmd(spi, 0xA6); //Set Normal Display
	oled_cmd(spi, 0xAF); //Set Display On

	oled_cmd(spi, 0x20);////
	oled_cmd(spi, 0x00);////

    uint8_t *buffer = malloc(1024);
    

    while (1) {
        memset(buffer, 0xff, 1024);
        oled_data(spi, buffer, 1024);
        vTaskDelay(1000 / portTICK_RATE_MS);

        memset(buffer, 0x00, 1024);
        oled_data(spi, buffer, 1024);
        vTaskDelay(1000 / portTICK_RATE_MS);

        ESP_LOGI(TAG, "oled init run");        
    }


}


static void oled_spi_init(void)
{
    esp_err_t ret;
    spi_bus_config_t buscfg={
        .miso_io_num=OLED_SPI_MISO_PIN_NUM,
        .mosi_io_num=OLED_SPI_MOSI_PIN_NUM,
        .sclk_io_num=OLED_SPI_CLK_PIN_NUM,
        .quadwp_io_num=-1,
        .quadhd_io_num=-1,
        .max_transfer_sz=1024
    };
    spi_device_interface_config_t devcfg={
        .clock_speed_hz=1*1000*1000,           //Clock out at 5 MHz
        .mode=0,                                //SPI mode 0
        .spics_io_num=OLED_SPI_CS_PIN_NUM,               //CS pin
        .queue_size=1,                          //We want to be able to queue 1 transactions at a time
        .pre_cb=oled_spi_pre_transfer_callback,  //Specify pre-transfer callback to handle D/C line
    };
    //Initialize the SPI bus
    ret=spi_bus_initialize(OLED_SPI, &buscfg, OLED_SPI_DMA_CHAN);
    ESP_ERROR_CHECK(ret);
    //Attach the LCD to the SPI bus
    ret=spi_bus_add_device(OLED_SPI, &devcfg, &spi);
    ESP_ERROR_CHECK(ret);
}

static void oled_gpio_init(void)
{
    gpio_config_t io_conf;

    //disable interrupt
    io_conf.intr_type = GPIO_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    //bit mask of the pins that you want to set,e.g.GPIO18/19
    io_conf.pin_bit_mask = ((1ULL<<OLED_DC_PIN_NUM) | (1ULL<<OLED_RS_PIN_NUM));
    //disable pull-down mode
    io_conf.pull_down_en = 0;
    //disable pull-up mode
    io_conf.pull_up_en = 0;
    //configure GPIO with the given settings
    gpio_config(&io_conf);

    gpio_set_level(OLED_DC_PIN_NUM, 0);
    gpio_set_level(OLED_RS_PIN_NUM, 0);
}

//This function is called (in irq context!) just before a transmission starts. It will
//set the D/C line to the value indicated in the user field.
void oled_spi_pre_transfer_callback(spi_transaction_t *t)
{
    int dc=(int)t->user;
    gpio_set_level(OLED_DC_PIN_NUM, dc);
}

/* Send a command to the LCD. Uses spi_device_polling_transmit, which waits
 * until the transfer is complete.
 *
 * Since command transactions are usually small, they are handled in polling
 * mode for higher speed. The overhead of interrupt transactions is more than
 * just waiting for the transaction to complete.
 */
void oled_cmd(spi_device_handle_t spi, const uint8_t cmd)
{
    esp_err_t ret;
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));       //Zero out the transaction
    t.length=8;                     //Command is 8 bits
    t.tx_buffer=&cmd;               //The data is the cmd itself
    t.user=(void*)0;                //D/C needs to be set to 0
    ret=spi_device_polling_transmit(spi, &t);  //Transmit!
    assert(ret==ESP_OK);            //Should have had no issues.
}

/* Send data to the LCD. Uses spi_device_polling_transmit, which waits until the
 * transfer is complete.
 *
 * Since data transactions are usually small, they are handled in polling
 * mode for higher speed. The overhead of interrupt transactions is more than
 * just waiting for the transaction to complete.
 */
void oled_data(spi_device_handle_t spi, const uint8_t *data, int len)
{
    esp_err_t ret;
    spi_transaction_t *rtrans;
    spi_transaction_t t;
    if (len==0) return;             //no need to send anything
    memset(&t, 0, sizeof(t));       //Zero out the transaction
    t.length=len*8;                 //Len is in bytes, transaction length is in bits.
    t.tx_buffer=data;               //Data
    t.user=(void*)1;                //D/C needs to be set to 1
    ret=spi_device_queue_trans(spi, &t, portMAX_DELAY);
    assert(ret==ESP_OK);            //Should have had no issues.
}

