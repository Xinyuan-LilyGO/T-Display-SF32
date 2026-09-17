#ifndef PLATFORM_PSRAM_H
#define PLATFORM_PSRAM_H

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

void *t_sf32_psram_alloc(rt_size_t size);
void *t_sf32_psram_calloc(rt_size_t count, rt_size_t size);
void t_sf32_psram_free(void *pointer);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_PSRAM_H */
