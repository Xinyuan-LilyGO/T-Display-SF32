#ifndef FACTORY_START_PAGE_H
#define FACTORY_START_PAGE_H

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*start_page_complete_callback_t)(void *user_data);

rt_err_t start_page_register(void);
void start_page_set_complete_callback(start_page_complete_callback_t callback,
                                      void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* FACTORY_START_PAGE_H */
