#include "charge_page.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <lvgl.h>

#include "battery_service.h"
#include "ui_config.h"
#include "ui_navigation.h"
#include "ui_status_bar.h"
#include "ui_theme.h"

#define CHARGE_SIDE_PADDING       16
#define CHARGE_HEADER_Y           (UI_STATUS_BAR_HEIGHT + 4)
#define CHARGE_SUMMARY_Y          100
#define CHARGE_SUMMARY_HEIGHT     174
#define CHARGE_STATUS_TITLE_Y     288
#define CHARGE_STATUS_LIST_Y      314
#define CHARGE_STATUS_LIST_HEIGHT (UI_SCREEN_HEIGHT - CHARGE_STATUS_LIST_Y)
#define CHARGE_CURRENT_MIN_MA     8
#define CHARGE_CURRENT_MAX_MA     456
#define CHARGE_CURRENT_STEP_MA    8
#define CHARGE_CURRENT_STEP_COUNT \
    ((CHARGE_CURRENT_MAX_MA - CHARGE_CURRENT_MIN_MA) / \
     CHARGE_CURRENT_STEP_MA)
#define CHARGE_STATUS_ROW_COUNT 7U
#define CHARGE_UPDATE_INTERVAL_MS 500U

typedef enum
{
    CHARGE_STATUS_VOLTAGE = 0,
    CHARGE_STATUS_CURRENT,
    CHARGE_STATUS_INPUT_POWER,
    CHARGE_STATUS_THERMAL,
    CHARGE_STATUS_VIN_FAULT,
    CHARGE_STATUS_BATTERY_FAULT,
    CHARGE_STATUS_NTC,
} charge_status_row_t;

static const char *const charge_status_names[CHARGE_STATUS_ROW_COUNT] = {
    "Charge voltage",
    "Charge current",
    "Input power",
    "Thermal regulation",
    "VIN fault",
    "Battery fault",
    "NTC status",
};

static lv_obj_t *charge_arc;
static lv_obj_t *charge_device_label;
static lv_obj_t *charge_percent_label;
static lv_obj_t *charge_voltage_label;
static lv_obj_t *charge_state_label;
static lv_obj_t *charge_current_label;
static lv_obj_t *charge_current_slider;
static lv_obj_t *charge_apply_status_label;
static lv_obj_t *charge_status_values[CHARGE_STATUS_ROW_COUNT];
static int16_t charge_current_ma = 352;
static bool charge_updating_slider;

static lv_obj_t *charge_page_create_label(lv_obj_t *parent,
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

static void charge_page_style_container(lv_obj_t *object)
{
    lv_obj_set_style_bg_color(object, lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_radius(object, 4, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_shadow_width(object, 0, 0);
}

static void charge_page_style_button(lv_obj_t *button,
                                     uint32_t color)
{
    charge_page_style_container(button);
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    ui_theme_apply_focus_style(button);
    lv_obj_set_style_bg_color(button, lv_color_hex(UI_COLOR_TRACK),
                              LV_STATE_PRESSED);
}

static int32_t charge_page_current_to_step(int16_t current_ma)
{
    int32_t step;

    if (current_ma <= CHARGE_CURRENT_MIN_MA)
        return 0;
    if (current_ma >= CHARGE_CURRENT_MAX_MA)
        return CHARGE_CURRENT_STEP_COUNT;
    step = ((int32_t)current_ma - CHARGE_CURRENT_MIN_MA) /
           CHARGE_CURRENT_STEP_MA;
    return step;
}

static int16_t charge_page_step_to_current(int32_t step)
{
    if (step < 0)
        step = 0;
    else if (step > CHARGE_CURRENT_STEP_COUNT)
        step = CHARGE_CURRENT_STEP_COUNT;
    return (int16_t)(CHARGE_CURRENT_MIN_MA +
                     step * CHARGE_CURRENT_STEP_MA);
}

static void charge_page_set_current_controls(int16_t current_ma,
                                             lv_anim_enable_t animation)
{
    int32_t step = charge_page_current_to_step(current_ma);
    int16_t normalized_ma = charge_page_step_to_current(step);

    charge_current_ma = normalized_ma;
    charge_updating_slider = true;
    lv_slider_set_value(charge_current_slider, step, animation);
    charge_updating_slider = false;
    lv_label_set_text_fmt(charge_current_label, "%d mA",
                          (int)normalized_ma);
}

static void charge_page_set_status_value(charge_status_row_t row,
                                         const char *text,
                                         uint32_t color)
{
    lv_label_set_text(charge_status_values[row], text);
    lv_obj_set_style_text_color(charge_status_values[row],
                                lv_color_hex(color), 0);
}

static void charge_page_set_unavailable(void)
{
    uint32_t index;

    lv_arc_set_value(charge_arc, 0);
    lv_label_set_text(charge_percent_label, "--%");
    lv_label_set_text(charge_voltage_label, "Voltage unavailable");
    lv_label_set_text(charge_state_label, "Unavailable");
    lv_obj_set_style_text_color(charge_state_label,
                                lv_color_hex(UI_COLOR_ERROR), 0);
    for (index = 0U; index < CHARGE_STATUS_ROW_COUNT; index++)
    {
        charge_page_set_status_value((charge_status_row_t)index,
                                     "Unavailable", UI_COLOR_MUTED);
    }
}

static bool charge_page_status_is_normal(const char *status)
{
    return status != RT_NULL && strcmp(status, "Normal") == 0;
}

static void charge_page_update(void)
{
    battery_info_t info;
    rt_err_t result;
    uint32_t state_color;

    memset(&info, 0, sizeof(info));
    result = battery_service_get_info(&info);
    if (result == RT_EOK)
    {
        lv_arc_set_value(charge_arc, info.percent);
        lv_label_set_text_fmt(charge_percent_label, "%u%%",
                              (unsigned int)info.percent);
        lv_label_set_text_fmt(charge_voltage_label, "%ld mV",
                              (long)info.voltage_mv);
    }
    else
    {
        lv_arc_set_value(charge_arc, 0);
        lv_label_set_text(charge_percent_label, "--%");
        lv_label_set_text(charge_voltage_label, "Voltage unavailable");
    }

    if (!info.charger_available)
    {
        lv_label_set_text(charge_device_label,
                          LV_SYMBOL_CHARGE "  SGM41562B ERROR");
        lv_obj_set_style_text_color(charge_device_label,
                                    lv_color_hex(UI_COLOR_ERROR), 0);
        lv_label_set_text(charge_state_label, "Unavailable");
        lv_obj_set_style_text_color(charge_state_label,
                                    lv_color_hex(UI_COLOR_ERROR), 0);
        for (uint32_t index = 0U; index < CHARGE_STATUS_ROW_COUNT; index++)
        {
            charge_page_set_status_value((charge_status_row_t)index,
                                         "Unavailable", UI_COLOR_MUTED);
        }
        return;
    }

    lv_label_set_text(charge_device_label,
                      LV_SYMBOL_CHARGE "  SGM41562B");
    lv_obj_set_style_text_color(charge_device_label,
                                lv_color_hex(UI_COLOR_SUCCESS), 0);
    lv_label_set_text(charge_state_label, info.charge_status);
    if (info.charging)
        state_color = UI_COLOR_SUCCESS;
    else if (strcmp(info.charge_status, "Charge Done") == 0)
        state_color = UI_COLOR_PRIMARY;
    else
        state_color = UI_COLOR_MUTED;
    lv_obj_set_style_text_color(charge_state_label,
                                lv_color_hex(state_color), 0);

    lv_label_set_text_fmt(charge_status_values[CHARGE_STATUS_VOLTAGE],
                          "%d mV", (int)info.charge_voltage_mv);
    lv_obj_set_style_text_color(
        charge_status_values[CHARGE_STATUS_VOLTAGE],
        lv_color_hex(UI_COLOR_TEXT), 0);
    lv_label_set_text_fmt(charge_status_values[CHARGE_STATUS_CURRENT],
                          "%d mA", (int)info.charge_current_ma);
    lv_obj_set_style_text_color(
        charge_status_values[CHARGE_STATUS_CURRENT],
        lv_color_hex(UI_COLOR_TEXT), 0);
    charge_page_set_status_value(
        CHARGE_STATUS_INPUT_POWER, info.power_status,
        strcmp(info.power_status, "Power Good") == 0 ?
            UI_COLOR_SUCCESS : UI_COLOR_WARNING);
    charge_page_set_status_value(
        CHARGE_STATUS_THERMAL, info.thermal_status,
        charge_page_status_is_normal(info.thermal_status) ?
            UI_COLOR_SUCCESS : UI_COLOR_WARNING);
    charge_page_set_status_value(
        CHARGE_STATUS_VIN_FAULT, info.vin_fault_status,
        charge_page_status_is_normal(info.vin_fault_status) ?
            UI_COLOR_SUCCESS : UI_COLOR_ERROR);
    charge_page_set_status_value(
        CHARGE_STATUS_BATTERY_FAULT, info.battery_fault_status,
        charge_page_status_is_normal(info.battery_fault_status) ?
            UI_COLOR_SUCCESS : UI_COLOR_ERROR);
    charge_page_set_status_value(
        CHARGE_STATUS_NTC, info.ntc_status,
        charge_page_status_is_normal(info.ntc_status) ?
            UI_COLOR_SUCCESS : UI_COLOR_ERROR);

    if (!charge_updating_slider)
        charge_page_set_current_controls(info.charge_current_ma,
                                         LV_ANIM_OFF);
}

static void charge_page_commit_current(int32_t step)
{
    int16_t requested_ma = charge_page_step_to_current(step);
    int16_t previous_ma = charge_current_ma;
    rt_err_t result;

    result = battery_service_set_charge_current(requested_ma);
    if (result == RT_EOK)
    {
        charge_page_set_current_controls(requested_ma, LV_ANIM_OFF);
        lv_label_set_text(charge_apply_status_label, "APPLIED");
        lv_obj_set_style_text_color(charge_apply_status_label,
                                    lv_color_hex(UI_COLOR_SUCCESS), 0);
        lv_label_set_text_fmt(
            charge_status_values[CHARGE_STATUS_CURRENT], "%d mA",
            (int)charge_current_ma);
    }
    else
    {
        charge_page_set_current_controls(previous_ma, LV_ANIM_OFF);
        lv_label_set_text(charge_apply_status_label, "APPLY ERROR");
        lv_obj_set_style_text_color(charge_apply_status_label,
                                    lv_color_hex(UI_COLOR_ERROR), 0);
    }
}

static void charge_page_slider_changed(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_target_obj(event);

    if (!charge_updating_slider)
        charge_page_commit_current(lv_slider_get_value(slider));
}

static void charge_page_step_clicked(lv_event_t *event)
{
    bool increment = (bool)(uintptr_t)lv_event_get_user_data(event);
    int32_t step = lv_slider_get_value(charge_current_slider);

    step += increment ? 1 : -1;
    if (step < 0)
        step = 0;
    else if (step > CHARGE_CURRENT_STEP_COUNT)
        step = CHARGE_CURRENT_STEP_COUNT;
    charge_page_commit_current(step);
}

static void charge_page_back_clicked(lv_event_t *event)
{
    (void)event;
    ui_navigation_switch_async(UI_PAGE_HOME);
}

static lv_obj_t *charge_page_create_header_button(ui_page_t *page)
{
    lv_obj_t *button;
    lv_obj_t *icon;

    button = lv_button_create(page->root);
    lv_obj_set_size(button, 48, 44);
    lv_obj_set_pos(button, 12, CHARGE_HEADER_Y);
    charge_page_style_button(button, UI_COLOR_BACKGROUND);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(button, charge_page_back_clicked,
                        LV_EVENT_CLICKED, RT_NULL);
    icon = charge_page_create_label(button, LV_SYMBOL_LEFT,
                                    &lv_font_montserrat_24,
                                    UI_COLOR_TEXT);
    lv_obj_center(icon);
    return button;
}

static void charge_page_create_header(ui_page_t *page)
{
    lv_obj_t *back = charge_page_create_header_button(page);
    lv_obj_t *title;

    title = charge_page_create_label(page->root, "POWER",
                                     &lv_font_montserrat_24,
                                     UI_COLOR_TEXT);
    lv_obj_set_size(title, 220, 34);
    lv_obj_set_pos(title, 72, CHARGE_HEADER_Y + 6);

    charge_device_label = charge_page_create_label(
        page->root, LV_SYMBOL_CHARGE "  SGM41562B",
        &lv_font_montserrat_14, UI_COLOR_SUCCESS);
    lv_obj_set_size(charge_device_label, 190, 24);
    lv_obj_set_style_text_align(charge_device_label,
                                LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(charge_device_label, UI_SCREEN_WIDTH - 206,
                   CHARGE_HEADER_Y + 10);

    if (page->group != RT_NULL)
        lv_group_add_obj(page->group, back);
}

static void charge_page_create_arc(lv_obj_t *panel)
{
    charge_arc = lv_arc_create(panel);
    lv_obj_set_size(charge_arc, 158, 158);
    lv_obj_set_pos(charge_arc, 8, 8);
    lv_obj_remove_flag(charge_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_mode(charge_arc, LV_ARC_MODE_NORMAL);
    lv_arc_set_range(charge_arc, 0, 100);
    lv_arc_set_bg_angles(charge_arc, 135, 45);
    lv_arc_set_rotation(charge_arc, 0);
    lv_obj_set_style_bg_opa(charge_arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_width(charge_arc, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(charge_arc, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_arc_color(charge_arc,
                               lv_color_hex(UI_COLOR_PRIMARY),
                               LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(charge_arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_width(charge_arc, 10, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(charge_arc,
                               lv_color_hex(UI_COLOR_PRIMARY),
                               LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(charge_arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(charge_arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_pad_all(charge_arc, 0, LV_PART_KNOB);

    charge_percent_label = charge_page_create_label(
        panel, "--%", &lv_font_montserrat_34, UI_COLOR_TEXT);
    lv_obj_set_size(charge_percent_label, 120, 42);
    lv_obj_set_style_text_align(charge_percent_label,
                                LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(charge_percent_label, 27, 53);

    charge_voltage_label = charge_page_create_label(
        panel, "Voltage unavailable", &lv_font_montserrat_14,
        UI_COLOR_MUTED);
    lv_obj_set_size(charge_voltage_label, 140, 22);
    lv_obj_set_style_text_align(charge_voltage_label,
                                LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(charge_voltage_label, 17, 97);

    charge_state_label = charge_page_create_label(
        panel, "Unavailable", &lv_font_montserrat_14,
        UI_COLOR_MUTED);
    lv_obj_set_size(charge_state_label, 140, 22);
    lv_obj_set_style_text_align(charge_state_label,
                                LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(charge_state_label, 17, 120);
}

static lv_obj_t *charge_page_create_step_button(lv_obj_t *panel,
                                                int32_t x,
                                                const char *symbol,
                                                bool increment,
                                                ui_page_t *page)
{
    lv_obj_t *button = lv_button_create(panel);
    lv_obj_t *icon;

    lv_obj_set_size(button, 40, 40);
    lv_obj_set_pos(button, x, 112);
    charge_page_style_button(button, UI_COLOR_TRACK);
    lv_obj_add_event_cb(button, charge_page_step_clicked,
                        LV_EVENT_CLICKED,
                        (void *)(uintptr_t)increment);
    icon = charge_page_create_label(button, symbol,
                                    &lv_font_montserrat_18,
                                    UI_COLOR_TEXT);
    lv_obj_center(icon);
    if (page->group != RT_NULL)
        lv_group_add_obj(page->group, button);
    return button;
}

static void charge_page_create_current_control(lv_obj_t *panel,
                                               ui_page_t *page)
{
    lv_obj_t *caption;

    caption = charge_page_create_label(panel, "CHARGE CURRENT",
                                       &lv_font_montserrat_14,
                                       UI_COLOR_MUTED);
    lv_obj_set_pos(caption, 184, 20);

    charge_current_label = charge_page_create_label(
        panel, "350 mA", &lv_font_montserrat_24, UI_COLOR_TEXT);
    lv_obj_set_size(charge_current_label, 220, 34);
    lv_obj_set_pos(charge_current_label, 184, 45);

    charge_apply_status_label = charge_page_create_label(
        panel, "8 mA STEP", &lv_font_montserrat_12,
        UI_COLOR_MUTED);
    lv_obj_set_size(charge_apply_status_label, 238, 18);
    lv_obj_set_pos(charge_apply_status_label, 184, 80);

    charge_page_create_step_button(panel, 184, LV_SYMBOL_MINUS,
                                   false, page);

    charge_current_slider = lv_slider_create(panel);
    lv_obj_set_size(charge_current_slider, 142, 16);
    lv_obj_set_pos(charge_current_slider, 232, 124);
    lv_slider_set_range(charge_current_slider, 0,
                        CHARGE_CURRENT_STEP_COUNT);
    lv_obj_set_style_bg_color(charge_current_slider,
                               lv_color_hex(UI_COLOR_TRACK),
                               LV_PART_MAIN);
    lv_obj_set_style_bg_color(charge_current_slider,
                               lv_color_hex(UI_COLOR_PRIMARY),
                               LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(charge_current_slider,
                               lv_color_hex(UI_COLOR_TEXT),
                               LV_PART_KNOB);
    lv_obj_set_style_width(charge_current_slider, 14, LV_PART_KNOB);
    lv_obj_set_style_height(charge_current_slider, 14, LV_PART_KNOB);
    lv_obj_set_style_pad_all(charge_current_slider, 0, LV_PART_KNOB);
    ui_theme_apply_focus_style(charge_current_slider);
    lv_obj_add_event_cb(charge_current_slider,
                        charge_page_slider_changed,
                        LV_EVENT_VALUE_CHANGED, RT_NULL);
    if (page->group != RT_NULL)
        lv_group_add_obj(page->group, charge_current_slider);

    charge_page_create_step_button(panel, 390, LV_SYMBOL_PLUS,
                                   true, page);
}

static void charge_page_create_summary(ui_page_t *page)
{
    lv_obj_t *panel = lv_obj_create(page->root);

    lv_obj_set_size(panel, UI_SCREEN_WIDTH - CHARGE_SIDE_PADDING * 2,
                    CHARGE_SUMMARY_HEIGHT);
    lv_obj_set_pos(panel, CHARGE_SIDE_PADDING, CHARGE_SUMMARY_Y);
    charge_page_style_container(panel);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    charge_page_create_arc(panel);
    charge_page_create_current_control(panel, page);
}

static void charge_page_create_status_list(ui_page_t *page)
{
    lv_obj_t *title;
    lv_obj_t *list;
    uint32_t index;

    title = charge_page_create_label(page->root, "CHARGER STATUS",
                                     &lv_font_montserrat_16,
                                     UI_COLOR_TEXT);
    lv_obj_set_pos(title, CHARGE_SIDE_PADDING, CHARGE_STATUS_TITLE_Y);

    list = lv_obj_create(page->root);
    lv_obj_set_size(list, UI_SCREEN_WIDTH, CHARGE_STATUS_LIST_HEIGHT);
    lv_obj_set_pos(list, 0, CHARGE_STATUS_LIST_Y);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_radius(list, 0, 0);
    lv_obj_set_style_pad_left(list, CHARGE_SIDE_PADDING, 0);
    lv_obj_set_style_pad_right(list, CHARGE_SIDE_PADDING, 0);
    lv_obj_set_style_pad_bottom(list, 12, 0);
    lv_obj_set_style_pad_top(list, 0, 0);
    lv_obj_set_style_pad_row(list, 0, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    for (index = 0U; index < CHARGE_STATUS_ROW_COUNT; index++)
    {
        lv_obj_t *row = lv_obj_create(list);
        lv_obj_t *name;
        lv_obj_t *separator;

        lv_obj_set_size(row, UI_SCREEN_WIDTH - CHARGE_SIDE_PADDING * 2,
                        30);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        name = charge_page_create_label(row, charge_status_names[index],
                                        &lv_font_montserrat_14,
                                        UI_COLOR_MUTED);
        lv_obj_set_size(name, 210, 22);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 8, -1);

        charge_status_values[index] = charge_page_create_label(
            row, "Unavailable", &lv_font_montserrat_14, UI_COLOR_TEXT);
        lv_obj_set_size(charge_status_values[index], 206, 22);
        lv_obj_set_style_text_align(charge_status_values[index],
                                    LV_TEXT_ALIGN_RIGHT, 0);
        lv_label_set_long_mode(charge_status_values[index],
                               LV_LABEL_LONG_DOT);
        lv_obj_align(charge_status_values[index], LV_ALIGN_RIGHT_MID,
                     -8, -1);

        separator = lv_obj_create(row);
        lv_obj_set_size(separator,
                        UI_SCREEN_WIDTH - CHARGE_SIDE_PADDING * 2 - 16,
                        1);
        lv_obj_align(separator, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_color(separator,
                                  lv_color_hex(UI_COLOR_TRACK), 0);
        lv_obj_set_style_bg_opa(separator, LV_OPA_60, 0);
        lv_obj_set_style_border_width(separator, 0, 0);
        lv_obj_set_style_pad_all(separator, 0, 0);
        lv_obj_remove_flag(separator, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(separator, LV_OBJ_FLAG_CLICKABLE);
    }
}

static void charge_page_on_create(ui_page_t *page)
{
    if (page == RT_NULL || page->root == RT_NULL)
        return;

    memset(charge_status_values, 0, sizeof(charge_status_values));
    if (page->group != RT_NULL)
        lv_group_set_wrap(page->group, true);
    charge_page_create_header(page);
    charge_page_create_summary(page);
    charge_page_create_status_list(page);
    charge_page_set_current_controls(charge_current_ma, LV_ANIM_OFF);
    charge_page_set_unavailable();
    charge_page_update();
}

static void charge_page_on_enter(ui_page_t *page)
{
    (void)page;
    charge_page_update();
}

static void charge_page_on_timer(ui_page_t *page)
{
    (void)page;
    charge_page_update();
}

static void charge_page_on_destroy(ui_page_t *page)
{
    (void)page;
    charge_arc = RT_NULL;
    charge_device_label = RT_NULL;
    charge_percent_label = RT_NULL;
    charge_voltage_label = RT_NULL;
    charge_state_label = RT_NULL;
    charge_current_label = RT_NULL;
    charge_current_slider = RT_NULL;
    charge_apply_status_label = RT_NULL;
    memset(charge_status_values, 0, sizeof(charge_status_values));
}

static void charge_page_on_gesture(ui_page_t *page,
                                   ui_gesture_t gesture)
{
    (void)page;
    if (gesture == UI_GESTURE_RIGHT)
        ui_navigation_switch_async(UI_PAGE_HOME);
}

static void charge_page_on_event(ui_page_t *page,
                                 const ui_event_t *event)
{
    (void)page;
    if (event == RT_NULL || event->type != UI_EVENT_KEY)
        return;
    if (event->data == LV_KEY_ESC || event->data == LV_KEY_HOME)
        ui_navigation_switch_async(UI_PAGE_HOME);
}

rt_err_t charge_page_register(void)
{
    const ui_page_t page = {
        .id = UI_PAGE_CHARGE,
        .app_id = UI_APP_NONE,
        .name = "Charge Page",
        .on_create = charge_page_on_create,
        .on_enter = charge_page_on_enter,
        .on_destroy = charge_page_on_destroy,
        .on_timer = charge_page_on_timer,
        .on_gesture = charge_page_on_gesture,
        .on_event = charge_page_on_event,
        .timer_interval_ms = CHARGE_UPDATE_INTERVAL_MS,
    };

    return ui_navigation_register_page(&page);
}
