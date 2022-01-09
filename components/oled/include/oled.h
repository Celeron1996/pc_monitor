#ifndef _OLED_H_
#define _OLED_H_


#define OLED_HEIGHT (64u)
#define OLED_WIDTH  (128u)
#define OLED_FPS_DEFAULT (20u)

#define OLED_SPI                HSPI_HOST
#define OLED_SPI_DMA_CHAN       2
#define OLED_SPI_MISO_PIN_NUM   25
#define OLED_SPI_MOSI_PIN_NUM   23
#define OLED_SPI_CLK_PIN_NUM    19
#define OLED_SPI_CS_PIN_NUM     22

#define OLED_DC_PIN_NUM 21
#define OLED_RS_PIN_NUM 18

typedef enum {
    oled_ok = 0,
    oled_err = -1
} oled_err_t;




struct oled_handler_t {
    uint16_t height;
    uint16_t width;
    uint8_t *buffer;
    uint16_t bufsize;
    uint8_t fps;
    spi_device_handle_t *spi;
    bool enable;

    void (*set_dot)(uint8_t row, uint8_t column, int state, void *this);
    void (*line)(uint8_t srow, uint8_t scolumn, uint8_t erow, uint8_t ecolumn, int line_type, void *this);
    void (*rectangle)(uint8_t srow, uint8_t scolumn, uint8_t erow, uint8_t ecolumn, int state, void *this);
    void (*circle)(uint8_t row, uint8_t column, uint8_t r, int state, void *this);
    void (*clear)(void *this);
};

typedef struct oled_handler_t * oled_handler_p;



oled_err_t oled_init(oled_handler_p *oled);



#endif
