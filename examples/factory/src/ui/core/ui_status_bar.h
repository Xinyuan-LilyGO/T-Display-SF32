#ifndef FACTORY_UI_STATUS_BAR_H
#define FACTORY_UI_STATUS_BAR_H

#include <stdbool.h>
#include <rtthread.h>

#include "ui_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_STATUS_BAR_HEIGHT 44

rt_err_t ui_status_bar_init(void);
bool ui_status_bar_is_ready(void);
void ui_status_bar_set_visible(bool visible);
void ui_status_bar_bring_to_front(void);
rt_err_t ui_status_bar_handle_event(const ui_event_t *event);

#ifdef __cplusplus
}
#endif

#endif /* FACTORY_UI_STATUS_BAR_H */
