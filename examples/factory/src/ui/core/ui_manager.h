#ifndef FACTORY_UI_MANAGER_H
#define FACTORY_UI_MANAGER_H

#include <stdbool.h>
#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

rt_err_t ui_manager_init(void);
bool ui_manager_is_ready(void);
bool ui_manager_is_suspended(void);
rt_err_t ui_manager_set_suspended(bool suspended);

void ui_manager_lock(void);
void ui_manager_unlock(void);

#ifdef __cplusplus
}
#endif

#endif /* FACTORY_UI_MANAGER_H */
