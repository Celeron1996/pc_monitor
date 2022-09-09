idf_component_register(SRCS "pc.c"
                    INCLUDE_DIRS "include"
	    REQUIRES json mbedtls esp_http_client esp-tls lvgl spi_flash)