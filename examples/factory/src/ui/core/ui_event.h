#ifndef FACTORY_UI_EVENT_H
#define FACTORY_UI_EVENT_H

#include <stdbool.h>
#include <rtthread.h>

#include "ui_types.h"

#ifdef __cplusplus
extern "C" {
#endif

rt_err_t ui_event_init(void);
bool ui_event_is_ready(void);
rt_err_t ui_event_post(const ui_event_t *event);
rt_err_t ui_event_receive(ui_event_t *event, rt_int32_t timeout);

#ifdef __cplusplus
}
#endif

#endif /* FACTORY_UI_EVENT_H */
