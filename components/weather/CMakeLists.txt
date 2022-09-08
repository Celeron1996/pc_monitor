file(GLOB_RECURSE SOURCES image/*.c)

idf_component_register(SRCS "weather.c" ${SOURCES}
                    INCLUDE_DIRS "include"
					REQUIRES json lvgl)
