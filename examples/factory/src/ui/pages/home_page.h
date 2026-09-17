#ifndef FACTORY_HOME_PAGE_H
#define FACTORY_HOME_PAGE_H

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    HOME_MENU_STYLE_CAROUSEL = 0,
    HOME_MENU_STYLE_HONEYCOMB,
    HOME_MENU_STYLE_COUNT,
} home_menu_style_t;

rt_err_t home_page_register(void);
rt_err_t home_page_set_menu_style(home_menu_style_t style);
home_menu_style_t home_page_get_menu_style(void);

#ifdef __cplusplus
}
#endif

#endif /* FACTORY_HOME_PAGE_H */
