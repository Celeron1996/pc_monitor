#ifndef PC_H
#define PC_H

void pc_init(void);
void pc_analysis_json(void *pJson);
int pc_get_cpu_temperature(void);
int pc_get_gpu_temperature(void);
int pc_get_ram_load(void);
int pc_get_cpu_load(void);
int pc_get_gpu_load(void);
char *pc_get_cpu_temperature_str(void);
char *pc_get_gpu_temperature_str(void);
bool pc_is_cpu_temperature_unit_celsius(void);
bool pc_is_gpu_temperature_unit_celsius(void);
char *pc_get_gpu_load_str(void);
char *pc_get_cpu_load_str(void);
char *pc_get_ram_load_str(void);
char *pc_get_cpu_name_str(void);
char *pc_get_gpu_name_str(void);
char *pc_get_name_str(void);
char *pc_get_mainboard_name_str(void);
void pc_task(void *pvParameters);
void pc_gui_refresh(void);
int pc_refresh_url(void);

#endif