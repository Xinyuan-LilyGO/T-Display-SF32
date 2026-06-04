#include "ui.h"
#include "ui_page.h"
#include "ui_theme.h"


void enter_ui(void)
{
    theme_init();
    new_theme_init_and_set(themes[THEME_LIGHT]);
    page_manager_init();

}