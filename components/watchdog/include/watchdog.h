#ifndef __WATCHDOG_H
#define __WATCHDOG_H


#define WATCHDOG_PERIOD	(5000)


void watchdog_task(void *pvParameter);
void watchdog_reset(void);


#endif
