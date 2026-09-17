#ifndef PLATFORM_POWER_H
#define PLATFORM_POWER_H

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

rt_err_t platform_power_init(void);
void platform_power_deinit(void);
void platform_power_shutdown(void);
void platform_power_restart(void);

#ifdef __cplusplus
}
#endif

#endif
