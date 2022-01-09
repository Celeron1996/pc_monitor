#ifndef _OLED_H_
#define _OLED_H_


#define OLED_SPI                HSPI_HOST
#define OLED_SPI_DMA_CHAN       2
#define OLED_SPI_MISO_PIN_NUM   25
#define OLED_SPI_MOSI_PIN_NUM   23
#define OLED_SPI_CLK_PIN_NUM    19
#define OLED_SPI_CS_PIN_NUM     22

#define OLED_DC_PIN_NUM 21
#define OLED_RS_PIN_NUM 18





void oled_init(void);



#endif
