#ifndef FACTORY_UI_CONTEXT_H
#define FACTORY_UI_CONTEXT_H

#include <stdbool.h>
#include <stdint.h>
#include <rtthread.h>

#include "ui_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    bool display_ready;
    ui_page_id_t current_page;
    ui_page_id_t previous_page;
    uint8_t brightness_percent;
    bool initialization_complete;
    uint32_t init_error_mask;
} ui_context_t;

rt_err_t ui_context_init(void);
rt_err_t ui_context_get(ui_context_t *context);
rt_err_t ui_context_set_display_ready(bool ready);
rt_err_t ui_context_set_page(ui_page_id_t current,
                             ui_page_id_t previous);
rt_err_t ui_context_set_brightness(uint8_t percent);
rt_err_t ui_context_set_init_status(uint32_t error_mask, bool complete);

#ifdef __cplusplus
}
#endif

#endif /* FACTORY_UI_CONTEXT_H */
