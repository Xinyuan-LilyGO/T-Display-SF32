#include "ui_theme.h"
#include "ui_page.h"

ThemeConfig themes[] = {
    [THEME_LIGHT] = {
        .num_themes = THEME_LIGHT,
        .primary_color = LV_COLOR_GARY_1,
        .secondary_color = LV_COLOR_BIGSTONE,
        .bg_color = LV_COLOR_WHITE,
        .font_color = LV_COLOR_BLACK,
        .font_large = &lv_font_montserrat_36,
        .font_normal = &lv_font_montserrat_36,
        .font_small = &lv_font_montserrat_12,
    },
    [THEME_DARK] = {
        .num_themes = THEME_DARK,
        .primary_color = LV_COLOR_MERCURY,
        .secondary_color = LV_COLOR_MIRAGE,
        .bg_color = LV_COLOR_BLACK,
        .font_color = LV_COLOR_WHITE,
        .font_large = &lv_font_montserrat_36,
        .font_normal = &lv_font_montserrat_36,
        .font_small = &lv_font_montserrat_12,
    }};

static lv_style_t style_theme;
ThemeConfig *current_theme = NULL;
uint8_t current_theme_id = 0;

void theme_init(void)
{
    // 初始化默认主题
    current_theme = &themes[THEME_LIGHT];
    current_theme_id = THEME_LIGHT;
    lv_style_init(&style_theme);
}

ThemeConfig *theme_get_current(void)
{
    return current_theme;
}

uint8_t theme_get_current_id(void)
{
    return current_theme_id;
}

/*Will be called when the styles of the base theme are already added
  to add new styles*/
static void new_theme_apply_cb(lv_theme_t *th, lv_obj_t *obj)
{
    LV_UNUSED(th);

    if (lv_obj_check_type(obj, &lv_button_class))
    {
        lv_obj_add_style(obj, &style_theme, 0);
    }

    if (lv_obj_check_type(obj, &lv_label_class))
    {
        lv_obj_add_style(obj, &style_theme, 0);
    }

    if (lv_obj_check_type(obj, &lv_obj_class))
    {
        lv_obj_add_style(obj, &style_theme, 0);
    }

    if (lv_obj_check_type(obj, &lv_bar_class))
    {
        lv_obj_add_style(obj, &style_theme, 0);
    }
}

void new_theme_init_and_set(ThemeConfig themeconfig)
{
    current_theme = &themeconfig;
    current_theme_id = themeconfig.num_themes;
    /*Initialize the styles*/
    lv_style_set_bg_color(&style_theme, themeconfig.primary_color);
    lv_style_set_text_color(&style_theme, themeconfig.font_color);
    lv_style_set_text_font(&style_theme, themeconfig.font_normal);
    /*Initialize the new theme from the current theme*/
    lv_theme_t *th_act = lv_display_get_theme(NULL);
    static lv_theme_t th_new;
    th_new = *th_act;

    /*Set the parent theme and the style apply callback for the new theme*/
    lv_theme_set_parent(&th_new, th_act);
    lv_theme_set_apply_cb(&th_new, new_theme_apply_cb);

    /*Assign the new theme to the current display*/
    lv_display_set_theme(NULL, &th_new);
}