/**
 * @file app_mem.h
 *
 */

#ifndef APP_MEM_H
#define APP_MEM_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************
 *      INCLUDES
 *********************/
#include <stdbool.h>
#include <rtthread.h>
#include "lvgl.h"
//#include "lv_img_buf.h"
#include "mem_section.h"

/*********************
 *      DEFINES
 *********************/
typedef enum
{
    IMAGE_CACHE_HEAP,
    IMAGE_CACHE_SRAM,
    IMAGE_CACHE_PSRAM
} image_cache_t;

/**
@brief apply cache mem for solution applicaiton.
@param[in] size Size of cache mem
@param[in] cache_type Cache type of cache mem be applied
@retval Pointer of the successsful applicaiton.
*/
void *app_cache_alloc(size_t size, image_cache_t cache_type);

/**
@brief free cache mem which successsful apply by app_cache_alloc.
@param[in] p Pointer of free mem
*/
void app_cache_free(void *p);


#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*LV_IMG_BUF_H*/
