#ifndef _CHIP_WORK_MODE_H_
#define _CHIP_WORK_MODE_H_
#include <stdbool.h>

void enable_ldo(bool enable);
void set_wakeup_src(void);
void into_sleep(void);
void into_deepsleep(void);
void into_standby(void);
void into_hibernate(void);
void wdt_dog_reset(void);

#endif