#ifndef __WEATHER
#define __WEATHER


void weather_task(void *pvParameters);
char *weather_get_city(void);
char *weather_get_text(void);
char *weather_get_code(void);
char *weather_get_temperature(void);
void weather_gui_refresh(void);



#endif
