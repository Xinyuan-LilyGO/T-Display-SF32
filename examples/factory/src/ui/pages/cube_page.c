#include "cube_page.h"

#include <stdbool.h>
#include <stdint.h>

#include <lvgl.h>

#include "ui_3d_view.h"
#include "ui_config.h"
#include "ui_navigation.h"
#include "ui_status_bar.h"
#include "ui_theme.h"

#define CUBE_HEADER_Y       (UI_STATUS_BAR_HEIGHT + 4)
#define CUBE_VIEW_Y         96
#define CUBE_VIEW_HEIGHT   300
#define CUBE_KEY_STEP       0.12f

static lv_obj_t *cube_view;
static lv_obj_t *cube_auto_switch;

static lv_obj_t *cube_page_create_label(lv_obj_t *parent,
                                        const char *text,
                                        const lv_font_t *font,
                                        uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_label_set_text(label, text);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(label, 0, 0);
    lv_obj_set_style_pad_all(label, 0, 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_letter_space(label, 0, 0);
    return label;
}

static void cube_page_style_button(lv_obj_t *button, uint32_t color)
{
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_radius(button, 4, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(UI_COLOR_TRACK),
                              LV_STATE_PRESSED);
    ui_theme_apply_focus_style(button);
}

static void cube_page_back_clicked(lv_event_t *event)
{
    (void)event;
    ui_navigation_switch_async(UI_PAGE_HOME);
}

static void cube_page_auto_changed(lv_event_t *event)
{
    lv_obj_t *control = lv_event_get_target_obj(event);

    ui_3d_view_set_auto_rotate(
        cube_view, lv_obj_has_state(control, LV_STATE_CHECKED));
}

static void cube_page_reset_clicked(lv_event_t *event)
{
    (void)event;
    ui_3d_view_reset(cube_view);
}

static lv_obj_t *cube_page_create_back_button(ui_page_t *page)
{
    lv_obj_t *button = lv_button_create(page->root);
    lv_obj_t *icon;

    lv_obj_set_size(button, 48, 44);
    lv_obj_set_pos(button, 12, CUBE_HEADER_Y);
    cube_page_style_button(button, UI_COLOR_BACKGROUND);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(button, cube_page_back_clicked,
                        LV_EVENT_CLICKED, RT_NULL);
    icon = cube_page_create_label(button, LV_SYMBOL_LEFT,
                                  &lv_font_montserrat_24,
                                  UI_COLOR_TEXT);
    lv_obj_center(icon);
    if (page->group != RT_NULL)
        lv_group_add_obj(page->group, button);
    return button;
}

static void cube_page_create_header(ui_page_t *page)
{
    lv_obj_t *title;
    lv_obj_t *status;

    cube_page_create_back_button(page);
    title = cube_page_create_label(page->root, "3D CUBE",
                                   &lv_font_montserrat_24,
                                   UI_COLOR_TEXT);
    lv_obj_set_pos(title, 72, CUBE_HEADER_Y + 6);

    status = cube_page_create_label(page->root, "REAL-TIME",
                                    &lv_font_montserrat_14,
                                    UI_COLOR_SUCCESS);
    lv_obj_set_size(status, 140, 24);
    lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(status, 322, CUBE_HEADER_Y + 10);
}

static void cube_page_create_controls(ui_page_t *page)
{
    lv_obj_t *label;
    lv_obj_t *reset;
    lv_obj_t *icon;

    label = cube_page_create_label(page->root, "AUTO ROTATE",
                                   &lv_font_montserrat_16,
                                   UI_COLOR_TEXT);
    lv_obj_set_size(label, 160, 26);
    lv_obj_set_pos(label, 28, 421);

    cube_auto_switch = lv_switch_create(page->root);
    lv_obj_set_size(cube_auto_switch, 62, 32);
    lv_obj_set_pos(cube_auto_switch, 184, 416);
    lv_obj_set_style_bg_color(cube_auto_switch,
                              lv_color_hex(UI_COLOR_TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_color(cube_auto_switch,
                              lv_color_hex(UI_COLOR_PRIMARY),
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(cube_auto_switch,
                              lv_color_hex(UI_COLOR_TEXT), LV_PART_KNOB);
    lv_obj_add_state(cube_auto_switch, LV_STATE_CHECKED);
    ui_theme_apply_focus_style(cube_auto_switch);
    lv_obj_add_event_cb(cube_auto_switch, cube_page_auto_changed,
                        LV_EVENT_VALUE_CHANGED, RT_NULL);
    if (page->group != RT_NULL)
        lv_group_add_obj(page->group, cube_auto_switch);

    reset = lv_button_create(page->root);
    lv_obj_set_size(reset, 132, 48);
    lv_obj_set_pos(reset, 320, 408);
    cube_page_style_button(reset, UI_COLOR_PANEL);
    lv_obj_add_event_cb(reset, cube_page_reset_clicked,
                        LV_EVENT_CLICKED, RT_NULL);
    icon = cube_page_create_label(reset, LV_SYMBOL_REFRESH,
                                  &lv_font_montserrat_20,
                                  UI_COLOR_TEXT);
    lv_obj_set_pos(icon, 18, 13);
    label = cube_page_create_label(reset, "RESET",
                                   &lv_font_montserrat_14,
                                   UI_COLOR_TEXT);
    lv_obj_set_pos(label, 52, 15);
    if (page->group != RT_NULL)
        lv_group_add_obj(page->group, reset);
}

static void cube_page_on_create(ui_page_t *page)
{
    if (page == RT_NULL || page->root == RT_NULL)
        return;
    if (page->group != RT_NULL)
        lv_group_set_wrap(page->group, true);

    cube_page_create_header(page);
    cube_view = ui_3d_view_create(page->root);
    if (cube_view != RT_NULL)
    {
        lv_obj_set_size(cube_view, UI_SCREEN_WIDTH - 32,
                        CUBE_VIEW_HEIGHT);
        lv_obj_set_pos(cube_view, 16, CUBE_VIEW_Y);
        lv_obj_set_style_bg_color(cube_view,
                                  lv_color_hex(UI_COLOR_PANEL), 0);
        lv_obj_set_style_bg_opa(cube_view, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(cube_view, 0, 0);
        lv_obj_set_style_radius(cube_view, 4, 0);
        lv_obj_set_style_pad_all(cube_view, 0, 0);
        lv_obj_set_style_shadow_width(cube_view, 0, 0);
    }
    cube_page_create_controls(page);
}

static void cube_page_on_destroy(ui_page_t *page)
{
    (void)page;
    cube_view = RT_NULL;
    cube_auto_switch = RT_NULL;
}

static void cube_page_on_event(ui_page_t *page,
                               const ui_event_t *event)
{
    uint32_t key;

    (void)page;
    if (event == RT_NULL || event->type != UI_EVENT_KEY)
        return;
    key = (uint32_t)event->data & UI_EVENT_KEY_CODE_MASK;
    if (key == LV_KEY_ESC || key == LV_KEY_HOME)
    {
        ui_navigation_switch_async(UI_PAGE_HOME);
    }
    else if (key == LV_KEY_LEFT)
    {
        ui_3d_view_rotate(cube_view, 0.0f, -CUBE_KEY_STEP);
    }
    else if (key == LV_KEY_RIGHT)
    {
        ui_3d_view_rotate(cube_view, 0.0f, CUBE_KEY_STEP);
    }
    else if (key == LV_KEY_UP)
    {
        ui_3d_view_rotate(cube_view, -CUBE_KEY_STEP, 0.0f);
    }
    else if (key == LV_KEY_DOWN)
    {
        ui_3d_view_rotate(cube_view, CUBE_KEY_STEP, 0.0f);
    }
}

rt_err_t cube_page_register(void)
{
    const ui_page_t page = {
        .id = UI_PAGE_CUBE,
        .app_id = UI_APP_NONE,
        .name = "3D Cube Page",
        .on_create = cube_page_on_create,
        .on_destroy = cube_page_on_destroy,
        .on_event = cube_page_on_event,
    };

    return ui_navigation_register_page(&page);
}
