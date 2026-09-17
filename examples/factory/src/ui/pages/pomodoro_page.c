#include "pomodoro_page.h"

#include <stdbool.h>
#include <stdint.h>

#include <lvgl.h>

#include "pomodoro_service.h"
#include "ui_config.h"
#include "ui_navigation.h"
#include "ui_status_bar.h"
#include "ui_theme.h"

#define POMODORO_HEADER_Y       (UI_STATUS_BAR_HEIGHT + 4)
#define POMODORO_UPDATE_MS      250U
#define POMODORO_PRESET_COUNT   3U

static const uint8_t pomodoro_presets[POMODORO_PRESET_COUNT] = {
    15U, 25U, 45U,
};

static lv_obj_t *pomodoro_arc;
static lv_obj_t *pomodoro_time_label;
static lv_obj_t *pomodoro_state_label;
static lv_obj_t *pomodoro_preset_buttons[POMODORO_PRESET_COUNT];
static lv_obj_t *pomodoro_preset_labels[POMODORO_PRESET_COUNT];
static lv_obj_t *pomodoro_start_button;
static lv_obj_t *pomodoro_start_icon;

static lv_obj_t *pomodoro_page_create_label(lv_obj_t *parent,
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

static void pomodoro_page_style_button(lv_obj_t *button,
                                        uint32_t color)
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

static void pomodoro_page_update(void)
{
    pomodoro_status_t status;
    uint32_t color = UI_COLOR_MUTED;
    const char *state_text = "READY";

    if (pomodoro_service_get_status(&status) != RT_EOK ||
            pomodoro_time_label == RT_NULL)
        return;

    lv_label_set_text_fmt(pomodoro_time_label, "%02lu:%02lu",
                          (unsigned long)(status.remaining_seconds / 60U),
                          (unsigned long)(status.remaining_seconds % 60U));
    lv_arc_set_range(pomodoro_arc, 0, (int32_t)status.duration_seconds);
    lv_arc_set_value(pomodoro_arc, (int32_t)status.remaining_seconds);

    switch (status.state)
    {
    case POMODORO_STATE_RUNNING:
        state_text = "FOCUSING";
        color = UI_COLOR_SUCCESS;
        lv_label_set_text(pomodoro_start_icon, LV_SYMBOL_PAUSE);
        break;
    case POMODORO_STATE_PAUSED:
        state_text = "PAUSED";
        color = UI_COLOR_WARNING;
        lv_label_set_text(pomodoro_start_icon, LV_SYMBOL_PLAY);
        break;
    case POMODORO_STATE_FINISHED:
        state_text = "COMPLETE";
        color = UI_COLOR_PRIMARY;
        lv_label_set_text(pomodoro_start_icon, LV_SYMBOL_PLAY);
        break;
    case POMODORO_STATE_READY:
    default:
        lv_label_set_text(pomodoro_start_icon, LV_SYMBOL_PLAY);
        break;
    }
    lv_label_set_text(pomodoro_state_label, state_text);
    lv_obj_set_style_text_color(pomodoro_state_label,
                                lv_color_hex(color), 0);

    for (uint32_t index = 0U; index < POMODORO_PRESET_COUNT; index++)
    {
        bool selected = status.duration_seconds ==
                        (uint32_t)pomodoro_presets[index] * 60U;
        lv_obj_set_style_bg_color(
            pomodoro_preset_buttons[index],
            lv_color_hex(selected ? UI_COLOR_PRIMARY : UI_COLOR_PANEL), 0);
        lv_obj_set_style_text_color(
            pomodoro_preset_labels[index],
            lv_color_hex(selected ? UI_COLOR_BACKGROUND : UI_COLOR_TEXT), 0);
    }
}

static void pomodoro_page_back_clicked(lv_event_t *event)
{
    (void)event;
    ui_navigation_switch_async(UI_PAGE_HOME);
}

static void pomodoro_page_preset_clicked(lv_event_t *event)
{
    uint32_t index = (uint32_t)(uintptr_t)lv_event_get_user_data(event);

    if (index < POMODORO_PRESET_COUNT)
    {
        (void)pomodoro_service_set_duration(pomodoro_presets[index]);
        pomodoro_page_update();
    }
}

static void pomodoro_page_start_clicked(lv_event_t *event)
{
    pomodoro_status_t status;

    (void)event;
    if (pomodoro_service_get_status(&status) != RT_EOK)
        return;
    if (status.state == POMODORO_STATE_RUNNING)
        (void)pomodoro_service_pause();
    else
        (void)pomodoro_service_start();
    pomodoro_page_update();
}

static void pomodoro_page_reset_clicked(lv_event_t *event)
{
    (void)event;
    (void)pomodoro_service_reset();
    pomodoro_page_update();
}

static lv_obj_t *pomodoro_page_create_icon_button(
    ui_page_t *page, int32_t x, const char *symbol,
    uint32_t color, lv_event_cb_t callback)
{
    lv_obj_t *button = lv_button_create(page->root);
    lv_obj_t *label;

    lv_obj_set_size(button, 72, 58);
    lv_obj_set_pos(button, x, 390);
    pomodoro_page_style_button(button, color);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, RT_NULL);
    label = pomodoro_page_create_label(button, symbol,
                                       &lv_font_montserrat_24,
                                       UI_COLOR_TEXT);
    lv_obj_center(label);
    if (page->group != RT_NULL)
        lv_group_add_obj(page->group, button);
    return button;
}

static void pomodoro_page_on_create(ui_page_t *page)
{
    lv_obj_t *back;
    lv_obj_t *label;

    if (page == RT_NULL || page->root == RT_NULL)
        return;
    if (page->group != RT_NULL)
        lv_group_set_wrap(page->group, true);

    back = lv_button_create(page->root);
    lv_obj_set_size(back, 48, 44);
    lv_obj_set_pos(back, 12, POMODORO_HEADER_Y);
    pomodoro_page_style_button(back, UI_COLOR_BACKGROUND);
    lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(back, pomodoro_page_back_clicked,
                        LV_EVENT_CLICKED, RT_NULL);
    label = pomodoro_page_create_label(back, LV_SYMBOL_LEFT,
                                       &lv_font_montserrat_24,
                                       UI_COLOR_TEXT);
    lv_obj_center(label);
    if (page->group != RT_NULL)
        lv_group_add_obj(page->group, back);

    label = pomodoro_page_create_label(page->root, "POMODORO",
                                       &lv_font_montserrat_24,
                                       UI_COLOR_TEXT);
    lv_obj_set_pos(label, 72, POMODORO_HEADER_Y + 6);

    label = pomodoro_page_create_label(page->root, "DEEP FOCUS",
                                       &lv_font_montserrat_14,
                                       UI_COLOR_MUTED);
    lv_obj_set_size(label, 150, 24);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(label, 312, POMODORO_HEADER_Y + 10);

    pomodoro_arc = lv_arc_create(page->root);
    lv_obj_set_size(pomodoro_arc, 206, 206);
    lv_obj_set_pos(pomodoro_arc, 137, 94);
    lv_obj_remove_flag(pomodoro_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_bg_angles(pomodoro_arc, 130, 50);
    lv_obj_set_style_bg_opa(pomodoro_arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_width(pomodoro_arc, 12, LV_PART_MAIN);
    lv_obj_set_style_arc_color(pomodoro_arc,
                               lv_color_hex(UI_COLOR_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_width(pomodoro_arc, 12, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(pomodoro_arc,
                               lv_color_hex(UI_COLOR_PRIMARY),
                               LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(pomodoro_arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(pomodoro_arc, LV_OPA_TRANSP, LV_PART_KNOB);

    pomodoro_time_label = pomodoro_page_create_label(
        page->root, "25:00", &lv_font_montserrat_48, UI_COLOR_TEXT);
    lv_obj_set_size(pomodoro_time_label, 210, 58);
    lv_obj_set_style_text_align(pomodoro_time_label,
                                LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(pomodoro_time_label, 135, 162);

    pomodoro_state_label = pomodoro_page_create_label(
        page->root, "READY", &lv_font_montserrat_14, UI_COLOR_MUTED);
    lv_obj_set_size(pomodoro_state_label, 180, 24);
    lv_obj_set_style_text_align(pomodoro_state_label,
                                LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(pomodoro_state_label, 150, 224);

    for (uint32_t index = 0U; index < POMODORO_PRESET_COUNT; index++)
    {
        lv_obj_t *button = lv_button_create(page->root);

        pomodoro_preset_buttons[index] = button;
        lv_obj_set_size(button, 104, 48);
        lv_obj_set_pos(button, 64 + (int32_t)index * 124, 316);
        pomodoro_page_style_button(button, UI_COLOR_PANEL);
        lv_obj_add_event_cb(button, pomodoro_page_preset_clicked,
                            LV_EVENT_CLICKED, (void *)(uintptr_t)index);
        pomodoro_preset_labels[index] = pomodoro_page_create_label(
            button, "", &lv_font_montserrat_16, UI_COLOR_TEXT);
        lv_label_set_text_fmt(pomodoro_preset_labels[index], "%u MIN",
                              (unsigned int)pomodoro_presets[index]);
        lv_obj_center(pomodoro_preset_labels[index]);
        if (page->group != RT_NULL)
            lv_group_add_obj(page->group, button);
    }

    pomodoro_start_button = pomodoro_page_create_icon_button(
        page, 158, LV_SYMBOL_PLAY, UI_COLOR_PRIMARY,
        pomodoro_page_start_clicked);
    pomodoro_start_icon = lv_obj_get_child(pomodoro_start_button, 0);
    pomodoro_page_create_icon_button(page, 250, LV_SYMBOL_REFRESH,
                                     UI_COLOR_PANEL,
                                     pomodoro_page_reset_clicked);
    pomodoro_page_update();
}

static void pomodoro_page_on_destroy(ui_page_t *page)
{
    (void)page;
    pomodoro_arc = RT_NULL;
    pomodoro_time_label = RT_NULL;
    pomodoro_state_label = RT_NULL;
    pomodoro_start_button = RT_NULL;
    pomodoro_start_icon = RT_NULL;
    for (uint32_t index = 0U; index < POMODORO_PRESET_COUNT; index++)
    {
        pomodoro_preset_buttons[index] = RT_NULL;
        pomodoro_preset_labels[index] = RT_NULL;
    }
}

static void pomodoro_page_on_timer(ui_page_t *page)
{
    (void)page;
    pomodoro_page_update();
}

static void pomodoro_page_on_gesture(ui_page_t *page,
                                     ui_gesture_t gesture)
{
    (void)page;
    if (gesture == UI_GESTURE_RIGHT)
        ui_navigation_switch_async(UI_PAGE_HOME);
}

static void pomodoro_page_on_event(ui_page_t *page,
                                   const ui_event_t *event)
{
    (void)page;
    if (event != RT_NULL && event->type == UI_EVENT_KEY &&
            (event->data == LV_KEY_ESC || event->data == LV_KEY_HOME))
        ui_navigation_switch_async(UI_PAGE_HOME);
}

rt_err_t pomodoro_page_register(void)
{
    const ui_page_t page = {
        .id = UI_PAGE_POMODORO,
        .app_id = UI_APP_NONE,
        .name = "Pomodoro Page",
        .on_create = pomodoro_page_on_create,
        .on_destroy = pomodoro_page_on_destroy,
        .on_timer = pomodoro_page_on_timer,
        .on_gesture = pomodoro_page_on_gesture,
        .on_event = pomodoro_page_on_event,
        .timer_interval_ms = POMODORO_UPDATE_MS,
    };

    return ui_navigation_register_page(&page);
}
