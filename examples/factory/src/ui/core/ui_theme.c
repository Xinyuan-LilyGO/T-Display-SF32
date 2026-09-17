#include "ui_theme.h"

#include <stdbool.h>

#include "ui_config.h"

static bool theme_initialized;
static lv_style_t screen_style;

static void ui_theme_apply_focus_state(lv_obj_t *object,
                                       lv_style_selector_t selector)
{
    lv_obj_set_style_outline_color(object,
                                   lv_color_hex(UI_COLOR_WARNING),
                                   selector);
    lv_obj_set_style_outline_opa(object, LV_OPA_COVER, selector);
    lv_obj_set_style_outline_width(object, 1, selector);
    lv_obj_set_style_outline_pad(object, 1, selector);
    lv_obj_set_style_border_opa(object, LV_OPA_TRANSP, selector);
    lv_obj_set_style_shadow_width(object, 0, selector);
}

rt_err_t ui_theme_init(void)
{
    if (theme_initialized)
        return RT_EOK;

    lv_style_init(&screen_style);
    lv_style_set_size(&screen_style, UI_SCREEN_WIDTH, UI_SCREEN_HEIGHT);
    lv_style_set_border_width(&screen_style, 0);
    lv_style_set_radius(&screen_style, 0);
    lv_style_set_pad_all(&screen_style, 0);
    lv_style_set_bg_opa(&screen_style, LV_OPA_COVER);
    lv_style_set_bg_color(&screen_style,
                          lv_color_hex(UI_COLOR_BACKGROUND));
    lv_style_set_text_color(&screen_style, lv_color_hex(UI_COLOR_TEXT));
    theme_initialized = true;
    return RT_EOK;
}

void ui_theme_apply_screen(lv_obj_t *screen)
{
    if (screen == RT_NULL || !theme_initialized)
        return;

    lv_obj_add_style(screen, &screen_style, 0);
}

void ui_theme_apply_focus_style(lv_obj_t *object)
{
    if (object == RT_NULL)
        return;

    ui_theme_apply_focus_state(object, LV_STATE_FOCUSED);
    ui_theme_apply_focus_state(object, LV_STATE_FOCUS_KEY);
    ui_theme_apply_focus_state(object,
                               LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
    ui_theme_apply_focus_state(object, LV_STATE_EDITED);
    ui_theme_apply_focus_state(object,
                               LV_STATE_FOCUSED | LV_STATE_EDITED);
}

static void ui_theme_disable_focus_state(lv_obj_t *object,
                                         lv_style_selector_t selector)
{
    lv_obj_set_style_outline_width(object, 0, selector);
    lv_obj_set_style_outline_opa(object, LV_OPA_TRANSP, selector);
    lv_obj_set_style_shadow_width(object, 0, selector);
}

void ui_theme_disable_focus_style(lv_obj_t *object)
{
    if (object == RT_NULL)
        return;

    ui_theme_disable_focus_state(object, LV_STATE_FOCUSED);
    ui_theme_disable_focus_state(object, LV_STATE_FOCUS_KEY);
    ui_theme_disable_focus_state(object,
                                 LV_STATE_FOCUSED | LV_STATE_FOCUS_KEY);
    ui_theme_disable_focus_state(object, LV_STATE_EDITED);
    ui_theme_disable_focus_state(object,
                                 LV_STATE_FOCUSED | LV_STATE_EDITED);
}
