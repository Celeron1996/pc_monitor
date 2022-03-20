#ifndef PC_H
#define PC_H

void pc_init(void);
void pc_analysis_json(void *pJson);
int pc_get_cpu_temperature(void);
int pc_get_gpu_temperature(void);
int pc_get_ram_load(void);

#endif