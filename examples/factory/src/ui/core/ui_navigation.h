#ifndef FACTORY_UI_NAVIGATION_H
#define FACTORY_UI_NAVIGATION_H

#include <rtthread.h>

#include "ui_types.h"

#ifdef __cplusplus
extern "C" {
#endif

rt_err_t ui_navigation_register_page(const ui_page_t *page);
rt_err_t ui_navigation_init(void);

rt_err_t ui_navigation_switch_async(ui_page_id_t page_id);
rt_err_t ui_navigation_handle_event(const ui_event_t *event);
void ui_navigation_tick(uint32_t elapsed_ms);

ui_page_t *ui_navigation_get_page(ui_page_id_t page_id);
ui_page_t *ui_navigation_get_current_page(void);
ui_page_id_t ui_navigation_get_current_page_id(void);
rt_err_t ui_navigation_destroy_page(ui_page_id_t page_id);

#ifdef __cplusplus
}
#endif

#endif /* FACTORY_UI_NAVIGATION_H */
