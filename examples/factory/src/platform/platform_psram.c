#include "platform_psram.h"

#include <stdbool.h>
#include <stdint.h>

#include "mem_section.h"

#define T_SF32_PSRAM_POOL_SIZE (64U * 1024U)

#if defined(BSP_USING_PSRAM) && defined(RT_USING_MEMHEAP)
L2_NON_RET_BSS_SECT_BEGIN(t_sf32_dynamic_heap)
L2_NON_RET_BSS_SECT(
    t_sf32_dynamic_heap,
    ALIGN(8) static uint8_t t_sf32_psram_pool[T_SF32_PSRAM_POOL_SIZE]);
L2_NON_RET_BSS_SECT_END

static struct rt_memheap t_sf32_psram_heap;
static bool t_sf32_psram_ready;

static int t_sf32_psram_init(void)
{
    rt_err_t result;

    result = rt_memheap_init(&t_sf32_psram_heap, "t_sf32_ps",
                             t_sf32_psram_pool,
                             sizeof(t_sf32_psram_pool));
    t_sf32_psram_ready = result == RT_EOK;
    return result;
}
INIT_PREV_EXPORT(t_sf32_psram_init);
#endif

void *t_sf32_psram_alloc(rt_size_t size)
{
#if defined(BSP_USING_PSRAM) && defined(RT_USING_MEMHEAP)
    if (!t_sf32_psram_ready || size == 0U)
        return RT_NULL;
    return rt_memheap_alloc(&t_sf32_psram_heap, size);
#else
    (void)size;
    return RT_NULL;
#endif
}

void *t_sf32_psram_calloc(rt_size_t count, rt_size_t size)
{
#if defined(BSP_USING_PSRAM) && defined(RT_USING_MEMHEAP)
    if (!t_sf32_psram_ready || count == 0U || size == 0U)
        return RT_NULL;
    return rt_memheap_calloc(&t_sf32_psram_heap, count, size);
#else
    (void)count;
    (void)size;
    return RT_NULL;
#endif
}

void t_sf32_psram_free(void *pointer)
{
#if defined(BSP_USING_PSRAM) && defined(RT_USING_MEMHEAP)
    if (pointer != RT_NULL)
        rt_memheap_free(pointer);
#else
    (void)pointer;
#endif
}
