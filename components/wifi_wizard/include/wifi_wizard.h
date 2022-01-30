#ifndef WIFI_WIZARD_H
#define WIFI_WIZARD_H

void wifi_wizard_init(void);
void wifi_wizard_connect(void);
void wifi_wizard_config(void);
void wifi_wizard_reset(void);
char *wifi_get_url(void);
bool wifi_get_sta_state(void);

#endif

