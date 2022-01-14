idf_component_register(SRCS "wifi_wizard.c"
                    INCLUDE_DIRS "include"
                    REQUIRES oled nvs_flash
                    PRIV_REQUIRES oled nvs_flash)
