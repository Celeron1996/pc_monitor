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
#include "font.h"

static const char TAG[] = "oled";


static spi_device_handle_t spi;
static oled_err_t oled_spi_init(void);
static void oled_gpio_init(void);
static void oled_cmd(spi_device_handle_t spi, const uint8_t cmd);
static void oled_spi_pre_transfer_callback(spi_transaction_t *t);
static void oled_task(void *pvParameters);

static void oled_set_dot(uint8_t row, uint8_t column, int state, void *this);
static void oled_line(uint8_t srow, uint8_t scolumn, uint8_t erow, uint8_t ecolumn, int line_type, void *this);
static void oled_rectangle(uint8_t srow, uint8_t scolumn, uint8_t erow, uint8_t ecolumn, int state, void *this);
static void oled_circle(uint8_t row, uint8_t column, uint8_t r, int state, void *this);
static void oled_clear(void *this);
static void oled_printf(uint8_t srow, uint8_t scolumn, int inverse, void *this, const char *p_str);

/**
 * @brief oled init
 * @return oled_err_t
 */
oled_err_t oled_init(oled_handler_p *oled)
{
  if (oled_spi_init() != oled_ok)
      return oled_err;

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

    *oled = (oled_handler_p)malloc(sizeof(struct oled_handler_t));
    if(*oled == NULL)
        return oled_err;
    
    (*oled)->buffer = malloc((OLED_WIDTH * OLED_HEIGHT) / 8);
    if((*oled)->buffer == NULL)
        return oled_err;

    (*oled)->bufsize = (OLED_WIDTH * OLED_HEIGHT) / 8;
    (*oled)->spi = &spi;
    (*oled)->fps = OLED_FPS_DEFAULT;
    (*oled)->enable = true;
    (*oled)->width = OLED_WIDTH;
    (*oled)->height = OLED_HEIGHT;

    (*oled)->set_dot = oled_set_dot;
    (*oled)->line = oled_line;
    (*oled)->rectangle = oled_rectangle;
    (*oled)->circle = oled_circle;
    (*oled)->clear = oled_clear;
    (*oled)->printf = oled_printf;

    if (xTaskCreate(oled_task, "oled_task", 4096, (void*)oled, 5, NULL) != pdTRUE)
        return oled_err;

    ESP_LOGI(TAG, "oled init ok! width = %d, height = %d\n", (*oled)->width, (*oled)->height);
    return oled_ok;
}

/**
 * @brief init spi driver
 * @return oled_err_t
 */
static oled_err_t oled_spi_init(void)
{
    esp_err_t ret;
    spi_bus_config_t buscfg={
        .miso_io_num=OLED_SPI_MISO_PIN_NUM,
        .mosi_io_num=OLED_SPI_MOSI_PIN_NUM,
        .sclk_io_num=OLED_SPI_CLK_PIN_NUM,
        .quadwp_io_num=-1,
        .quadhd_io_num=-1,
        .max_transfer_sz=(OLED_WIDTH * OLED_HEIGHT)
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
    if(ret != ESP_OK)
        return oled_err;
    //Attach the LCD to the SPI bus
    ret=spi_bus_add_device(OLED_SPI, &devcfg, &spi);
    ESP_ERROR_CHECK(ret);

    if(ret != ESP_OK)
        return oled_err;
    
    return oled_ok;
}

/**
 * @brief init gpio
 * 
 */
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
static void oled_spi_pre_transfer_callback(spi_transaction_t *t)
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
static void oled_cmd(spi_device_handle_t spi, const uint8_t cmd)
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

/**
 * @brief oled freertos task
 * @param pvParameters
 * @return  NULL
 * 
 */
static void oled_task(void *pvParameters)
{
    oled_handler_p oled = *(oled_handler_p *)pvParameters;
    esp_err_t ret;
    spi_transaction_t t;

    memset(&t, 0, sizeof(t));       //Zero out the transaction
    t.length=oled->bufsize * 8;                 //Len is in bytes, transaction length is in bits.
    t.tx_buffer=oled->buffer;               //Data
    t.user=(void*)1;                //D/C needs to be set to 1

    while (1) {
        if (oled->enable) {
            ret=spi_device_queue_trans(spi, &t, portMAX_DELAY);
            assert(ret==ESP_OK);            //Should have had no issues.
        }
        vTaskDelay((1000 / oled->fps) / portTICK_RATE_MS);
    }
}


/**
  * @brief  打点函数，点亮或熄灭某像素点
  * @param  row     :行坐标
  *	@param  column  :列坐标
  *	@param  state   :点亮或熄灭
  *	    #1	:点亮
  *	    #0	:熄灭
  * @param  this  :this
  * @return NULL
  */
static void oled_set_dot(uint8_t row, uint8_t column, int state, void *this)
{
  oled_handler_p oled = (oled_handler_p)this;
  uint8_t row_int = row	/	8;	/*整数*/
  uint8_t row_rem = row	%	8;	/*余数*/
  uint8_t *p_temp = oled->buffer + (row_int * oled->width) + column;

  if( (row >= oled->height) || (column >= oled->width)) {
      ESP_LOGE(TAG, "value overflow! row = %d, colum = %d", row, column);
      return;
  }

	if(state)
	{
		*p_temp |= (0x01 << row_rem);
	}
	else
	{
		*p_temp &= (~(0x01 << row_rem));
	}
}


/**
  * @brief  画线
  * @param  srow					:开始行坐标
  *	@param	scolumn				:开始列坐标
  * @param  erow					:结束行坐标
  *	@param	ecolumn				:结束列坐标
  *	@param	line_type			:画线类型
  *						#1	:画线，点亮像素点
  *						#0	:画清除线，熄灭像素点
  * @param  this
  * @return NULL
  */
static void oled_line(uint8_t srow, uint8_t scolumn, uint8_t erow, uint8_t ecolumn, int line_type, void *this)
{
  int8_t dx, dy;
  int8_t inc_x, inc_y;
  int16_t  xerr = 0, yerr = 0;
  uint8_t i, ds;

  dx = erow - srow;
  dy = ecolumn - scolumn;

  if(dx > 0)
  {
  	inc_x = 1;
  }
  else
  {
    if(dx == 0)
    {
    	inc_x = 0;
    }
    else
    {
    	inc_x = -1;
    	dx = -dx;
    }
  }

  if(dy > 0)
  {
  	inc_y = 1;
  }
  else
  {
    if(dy == 0)
    {
    	inc_y = 0;
    }
    else
    {
    	inc_y = -1;
    	dy = -dy;
    }
  }

  if(dx > dy)
  {
  	ds = dx;
  }
  else
  {
  	ds = dy;
  }

  for(i = 0; i <= ds+1; i++)
  {
    oled_set_dot(srow, scolumn, line_type, this);
    xerr += dx;
    yerr += dy;
    if(xerr > ds)
    {
      xerr -= ds;
      srow   += inc_x;
    }
    if(yerr > ds)
    {
      yerr -= ds;
      scolumn   += inc_y;
    }
  }
}

/**
  * @brief  画矩形
  * @param  srow					:开始行坐标
  *	@param	scolumn				:开始列坐标
  * @param  erow					:结束行坐标
  *	@param	ecolumn				:结束列坐标
  * @param  this
  * @return NULL
  */
static void oled_rectangle(uint8_t srow, uint8_t scolumn, uint8_t erow, uint8_t ecolumn, int state, void *this)
{
	oled_line(srow, scolumn, erow, scolumn, state, this);
	oled_line(srow, scolumn, srow, ecolumn, state, this);
	oled_line(erow, scolumn, erow, ecolumn, state, this);
	oled_line(srow, ecolumn, erow, ecolumn, state, this);
}



/**
  * @brief  画圆
  * @param  row						:圆点行坐标
  *	@param	column				:圆点列坐标
  * @param  this
  * @return NULL
  */
static void oled_circle(uint8_t row, uint8_t column, uint8_t r, int state, void *this)
{
	unsigned char dx, dy = r;

	for(dx = 0; dx <= r; dx++)
	{
		while((r * r + 1 - dx * dx) < (dy * dy))
		{
			dy--;
		}
		oled_set_dot(row + dx, column - dy, state, this);
		oled_set_dot(row - dx, column - dy, state, this);
		oled_set_dot(row - dx, column + dy, state, this);
		oled_set_dot(row + dx, column + dy, state, this);

		oled_set_dot(row + dy, column - dx, state, this);
		oled_set_dot(row - dy, column - dx, state, this);
		oled_set_dot(row - dy, column + dx, state, this);
		oled_set_dot(row + dy, column + dx, state, this);
	}
}

/**
  * @brief  清除图形缓存
  * @param  this
  * @return NULL
  */
static void oled_clear(void *this)
{
    oled_handler_p oled = (oled_handler_p)this;
	uint16_t i = oled->bufsize;
	uint8_t *p = oled->buffer;

	while(i--)
	{
		*p++ = 0;
	}
}


/**
  * @brief  显示字符
  * @param  srow					:字符左上角行坐标
  *	@param	scolumn				:字符左上角列坐标
  *	@param	inverse				:是否反色显示
  *						#1	:反色
  *						#0	:不反色
  * @param  this
  *	@param	p_str					:字符指针
  * @return NULL
  */
static void oled_printf(uint8_t srow, uint8_t scolumn, int inverse, void *this, const char *p_str)
{
	uint8_t x,y;
	uint8_t temp;
  oled_handler_p oled = (oled_handler_p)this;

	while(*p_str != '\0')
	{
		for(x = 0; x < 16; x++)
		{
			temp = font_ascii_8_16[(*p_str)*16 + x];
			for(y = 0; y < 8; y++)
			{
				if(temp & 0x80)
				{
          oled_set_dot(srow + x, scolumn + y, !inverse, this);
				}
				else
				{
					oled_set_dot(srow + x, scolumn + y, inverse, this);
				}
				temp <<= 1;
			}
		}
		p_str++;
    scolumn += 8;
    if((scolumn + 8) > oled->width)
    {
      scolumn = 0;
      srow += 16;
      if((srow + 16) > oled->height)
      {
        return;
      }
    }
	}
}
