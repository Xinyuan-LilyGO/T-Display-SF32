#ifndef FACTORY_UI_THEME_H
#define FACTORY_UI_THEME_H

#include <rtthread.h>

#include <lvgl.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define UI_COLOR_BACKGROUND 0x000000
#define UI_COLOR_TEXT 0xFFFFFF
#define UI_COLOR_MUTED 0xA1A1A1
#define UI_COLOR_TRACK 0x3A3A3A
#define UI_COLOR_PRIMARY 0x00FF00
#define UI_COLOR_SECONDARY 0xFF9D00
#define UI_COLOR_DEFAULT_BLUE 0x43B6FF
#define UI_COLOR_WARM 0xff0027
#define UI_COLOR_PANEL 0x16191F
#define UI_COLOR_SUCCESS 0x39FF88
#define UI_COLOR_ERROR 0xFF3B5C
#define UI_COLOR_WARNING 0xF0CF61

    rt_err_t ui_theme_init(void);
    void ui_theme_apply_screen(lv_obj_t *screen);
    void ui_theme_apply_focus_style(lv_obj_t *object);
    void ui_theme_disable_focus_style(lv_obj_t *object);

#ifdef __cplusplus
}
#endif

#endif /* FACTORY_UI_THEME_H */
