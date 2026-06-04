/**
 * @file app_mem.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include <stddef.h>
#include <string.h>
#include "app_mem.h"

#ifndef WIN32
    #include "register.h"
#endif

#define CACHE_SIZE 0x200000

#ifdef _MSC_VER
    #define APP_L2_RET_BSS_SECT(section_name, var)         var
#else
    #define APP_L2_RET_BSS_SECT(section_name, var)         var L2_RET_BSS_SECT(section_name)
#endif

/**
for L2_MEM(PSRAM).
*/
L2_RET_BSS_SECT_BEGIN(app_psram_ret_cache)
APP_L2_RET_BSS_SECT(app_psram_ret_cache, ALIGN(4) static uint8_t app_image_psram_cache[CACHE_SIZE]);
L2_RET_BSS_SECT_END

struct rt_memheap app_image_psram_memheap;


/**********************
 *   GLOBAL FUNCTIONS
 **********************/
static int app_cahe_memheap_init(void)
{
    rt_memheap_init(&app_image_psram_memheap, "app_image_psram_memheap", (void *)app_image_psram_cache, CACHE_SIZE);
    return 0;
}
INIT_PREV_EXPORT(app_cahe_memheap_init);


void *app_cache_alloc(size_t size, image_cache_t cache_type)
{
    uint8_t *p = NULL;

    if (!p)
    {
        p = (uint8_t *)rt_memheap_alloc(&app_image_psram_memheap, size);
    }
    return p ;
}

void app_cache_free(void *p)
{
    rt_memheap_free(p);
}

void *app_anim_mem_alloc(unsigned int size, bool anim_data)
{
    return app_cache_alloc(size, IMAGE_CACHE_PSRAM);
}

void app_anim_mem_free(void *p)
{
    app_cache_free(p);
}

#ifndef BSP_USING_PC_SIMULATOR
void *cxx_mem_allocate(size_t size)
{
    return app_cache_alloc(size, IMAGE_CACHE_PSRAM);
}

void cxx_mem_free(void *ptr)
{
    return app_cache_free(ptr);
}
#endif

