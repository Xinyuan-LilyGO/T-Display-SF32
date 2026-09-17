#include "sensors_page.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <lvgl.h>

#include "sensor_service.h"
#include "ui_config.h"
#include "ui_navigation.h"
#include "ui_status_bar.h"
#include "ui_theme.h"

#define SENSORS_SIDE_PADDING       16
#define SENSORS_HEADER_Y           (UI_STATUS_BAR_HEIGHT + 4)
#define SENSORS_CONTENT_WIDTH      (UI_SCREEN_WIDTH - 32)
#define SENSORS_IMU_Y              100
#define SENSORS_IMU_HEIGHT         194
#define SENSORS_STEPS_Y            304
#define SENSORS_STEPS_HEIGHT       60
#define SENSORS_BOTTOM_Y           374
#define SENSORS_BOTTOM_HEIGHT      90
#define SENSORS_UPDATE_INTERVAL_MS 100U
#define SENSORS_STALE_TIMEOUT_MS   1500U
#define SENSORS_STEP_GOAL          10000U
#define SENSORS_AXIS_COUNT         3U

static const char *const sensors_axis_names[SENSORS_AXIS_COUNT] = {
    "X", "Y", "Z",
};

static const uint32_t sensors_axis_colors[SENSORS_AXIS_COUNT] = {
    UI_COLOR_PRIMARY, UI_COLOR_SECONDARY, UI_COLOR_WARNING,
};

static lv_obj_t *sensors_back_button;
static lv_obj_t *sensors_device_label;
static lv_obj_t *sensors_acceleration_labels[SENSORS_AXIS_COUNT];
static lv_obj_t *sensors_gyroscope_labels[SENSORS_AXIS_COUNT];
static lv_obj_t *sensors_acceleration_bars[SENSORS_AXIS_COUNT];
static lv_obj_t *sensors_step_value_label;
static lv_obj_t *sensors_step_goal_label;
static lv_obj_t *sensors_step_progress;
#if defined(FACTORY_HW_USING_KEYBOARD)
static lv_obj_t *sensors_temperature_label;
static lv_obj_t *sensors_humidity_label;
static lv_obj_t *sensors_pressure_label;
#else
static lv_obj_t *sensors_motion_chart;
static lv_chart_series_t *sensors_motion_series;
#endif

static lv_obj_t *sensors_page_create_label(lv_obj_t *parent,
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

static void sensors_page_style_container(lv_obj_t *object)
{
    lv_obj_set_style_bg_color(object, lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_radius(object, 4, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_shadow_width(object, 0, 0);
}

static uint32_t sensors_abs_int32(int32_t value)
{
    if (value >= 0)
        return (uint32_t)value;
    return (uint32_t)(-(int64_t)value);
}

#if defined(FACTORY_HW_USING_KEYBOARD)
static void sensors_format_tenths(char *buffer, size_t buffer_size,
                                  int32_t value, const char *unit)
{
    uint32_t magnitude = sensors_abs_int32(value);

    rt_snprintf(buffer, buffer_size, "%s%u.%u %s",
                value < 0 ? "-" : "",
                (unsigned int)(magnitude / 10U),
                (unsigned int)(magnitude % 10U), unit);
}
#endif

static void sensors_format_mdps(char *buffer, size_t buffer_size,
                                int32_t value)
{
    uint32_t magnitude = sensors_abs_int32(value);

    rt_snprintf(buffer, buffer_size, "%s%u.%03u dps",
                value < 0 ? "-" : "",
                (unsigned int)(magnitude / 1000U),
                (unsigned int)(magnitude % 1000U));
}

static void sensors_format_steps(char *buffer, size_t buffer_size,
                                 uint32_t steps)
{
    if (steps >= 1000000000U)
    {
        rt_snprintf(buffer, buffer_size, "%u,%03u,%03u,%03u",
                    (unsigned int)(steps / 1000000000U),
                    (unsigned int)((steps / 1000000U) % 1000U),
                    (unsigned int)((steps / 1000U) % 1000U),
                    (unsigned int)(steps % 1000U));
    }
    else if (steps >= 1000000U)
    {
        rt_snprintf(buffer, buffer_size, "%u,%03u,%03u",
                    (unsigned int)(steps / 1000000U),
                    (unsigned int)((steps / 1000U) % 1000U),
                    (unsigned int)(steps % 1000U));
    }
    else if (steps >= 1000U)
    {
        rt_snprintf(buffer, buffer_size, "%u,%03u",
                    (unsigned int)(steps / 1000U),
                    (unsigned int)(steps % 1000U));
    }
    else
    {
        rt_snprintf(buffer, buffer_size, "%u", (unsigned int)steps);
    }
}

static void sensors_page_back_clicked(lv_event_t *event)
{
    (void)event;
    ui_navigation_switch_async(UI_PAGE_HOME);
}

static void sensors_page_create_header(ui_page_t *page)
{
    lv_obj_t *title;
    lv_obj_t *back_icon;

    sensors_back_button = lv_button_create(page->root);
    lv_obj_set_size(sensors_back_button, 48, 44);
    lv_obj_set_pos(sensors_back_button, 12, SENSORS_HEADER_Y);
    sensors_page_style_container(sensors_back_button);
    lv_obj_set_style_bg_opa(sensors_back_button, LV_OPA_TRANSP, 0);
    ui_theme_apply_focus_style(sensors_back_button);
    lv_obj_add_event_cb(sensors_back_button, sensors_page_back_clicked,
                        LV_EVENT_CLICKED, RT_NULL);
    back_icon = sensors_page_create_label(sensors_back_button,
                                          LV_SYMBOL_LEFT,
                                          &lv_font_montserrat_24,
                                          UI_COLOR_TEXT);
    lv_obj_center(back_icon);

    title = sensors_page_create_label(page->root, "SENSORS",
                                      &lv_font_montserrat_24,
                                      UI_COLOR_TEXT);
    lv_obj_set_size(title, 220, 34);
    lv_obj_set_pos(title, 72, SENSORS_HEADER_Y + 6);

    sensors_device_label = sensors_page_create_label(
        page->root, "WAITING", &lv_font_montserrat_12, UI_COLOR_MUTED);
    lv_obj_set_size(sensors_device_label, 180, 20);
    lv_obj_set_style_text_align(sensors_device_label,
                                LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(sensors_device_label, UI_SCREEN_WIDTH - 196,
                   SENSORS_HEADER_Y + 13);

    if (page->group != RT_NULL)
        lv_group_add_obj(page->group, sensors_back_button);
}

static void sensors_page_create_axis_row(lv_obj_t *panel,
                                         uint32_t index,
                                         lv_coord_t y)
{
    lv_obj_t *marker;
    lv_obj_t *axis_label;

    marker = lv_obj_create(panel);
    lv_obj_set_size(marker, 4, 36);
    lv_obj_set_pos(marker, 14, y);
    lv_obj_set_style_bg_color(marker,
                              lv_color_hex(sensors_axis_colors[index]), 0);
    lv_obj_set_style_bg_opa(marker, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(marker, 0, 0);
    lv_obj_set_style_radius(marker, 0, 0);
    lv_obj_set_style_pad_all(marker, 0, 0);
    lv_obj_remove_flag(marker, LV_OBJ_FLAG_SCROLLABLE);

    axis_label = sensors_page_create_label(
        panel, sensors_axis_names[index], &lv_font_montserrat_16,
        sensors_axis_colors[index]);
    lv_obj_set_size(axis_label, 24, 24);
    lv_obj_set_pos(axis_label, 27, y + 6);

    sensors_acceleration_labels[index] = sensors_page_create_label(
        panel, "-- mg", &lv_font_montserrat_18, UI_COLOR_TEXT);
    lv_obj_set_size(sensors_acceleration_labels[index], 138, 26);
    lv_obj_set_pos(sensors_acceleration_labels[index], 58, y - 3);

    sensors_acceleration_bars[index] = lv_bar_create(panel);
    lv_obj_set_size(sensors_acceleration_bars[index], 138, 6);
    lv_obj_set_pos(sensors_acceleration_bars[index], 58, y + 27);
    lv_bar_set_range(sensors_acceleration_bars[index], -2000, 2000);
    lv_bar_set_mode(sensors_acceleration_bars[index],
                    LV_BAR_MODE_SYMMETRICAL);
    lv_bar_set_value(sensors_acceleration_bars[index], 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sensors_acceleration_bars[index],
                              lv_color_hex(UI_COLOR_TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sensors_acceleration_bars[index],
                            LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sensors_acceleration_bars[index],
                              lv_color_hex(sensors_axis_colors[index]),
                              LV_PART_INDICATOR);
    lv_obj_set_style_radius(sensors_acceleration_bars[index], 3,
                            LV_PART_MAIN);
    lv_obj_set_style_radius(sensors_acceleration_bars[index], 3,
                            LV_PART_INDICATOR);

    sensors_gyroscope_labels[index] = sensors_page_create_label(
        panel, "-- dps", &lv_font_montserrat_18, UI_COLOR_TEXT);
    lv_obj_set_size(sensors_gyroscope_labels[index], 196, 28);
    lv_obj_set_style_text_align(sensors_gyroscope_labels[index],
                                LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(sensors_gyroscope_labels[index], 232, y + 3);
}

static void sensors_page_create_imu(ui_page_t *page)
{
    lv_obj_t *panel;
    lv_obj_t *section;
    lv_obj_t *acceleration_header;
    lv_obj_t *gyroscope_header;
    uint32_t index;

    panel = lv_obj_create(page->root);
    lv_obj_set_size(panel, SENSORS_CONTENT_WIDTH, SENSORS_IMU_HEIGHT);
    lv_obj_set_pos(panel, SENSORS_SIDE_PADDING, SENSORS_IMU_Y);
    sensors_page_style_container(panel);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    section = sensors_page_create_label(panel, "BHI260AP / 6-AXIS IMU",
                                        &lv_font_montserrat_12,
                                        UI_COLOR_MUTED);
    lv_obj_set_pos(section, 14, 9);

    acceleration_header = sensors_page_create_label(
        panel, "ACCELERATION", &lv_font_montserrat_12, UI_COLOR_MUTED);
    lv_obj_set_size(acceleration_header, 138, 18);
    lv_obj_set_pos(acceleration_header, 58, 27);

    gyroscope_header = sensors_page_create_label(
        panel, "ANGULAR RATE", &lv_font_montserrat_12, UI_COLOR_MUTED);
    lv_obj_set_size(gyroscope_header, 196, 18);
    lv_obj_set_style_text_align(gyroscope_header, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(gyroscope_header, 232, 27);

    for (index = 0U; index < SENSORS_AXIS_COUNT; index++)
        sensors_page_create_axis_row(panel, index,
                                     49 + (lv_coord_t)index * 47);
}

static void sensors_page_create_steps(ui_page_t *page)
{
    lv_obj_t *panel;
    lv_obj_t *icon;
    lv_obj_t *caption;

    panel = lv_obj_create(page->root);
    lv_obj_set_size(panel, SENSORS_CONTENT_WIDTH, SENSORS_STEPS_HEIGHT);
    lv_obj_set_pos(panel, SENSORS_SIDE_PADDING, SENSORS_STEPS_Y);
    sensors_page_style_container(panel);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    icon = sensors_page_create_label(panel, LV_SYMBOL_SHUFFLE,
                                     &lv_font_montserrat_24,
                                     UI_COLOR_PRIMARY);
    lv_obj_set_pos(icon, 14, 18);
    sensors_step_value_label = sensors_page_create_label(
        panel, "--", &lv_font_montserrat_24, UI_COLOR_TEXT);
    lv_obj_set_size(sensors_step_value_label, 145, 30);
    lv_obj_set_pos(sensors_step_value_label, 50, 8);
    caption = sensors_page_create_label(panel, "STEPS",
                                        &lv_font_montserrat_12,
                                        UI_COLOR_MUTED);
    lv_obj_set_pos(caption, 52, 36);

    sensors_step_goal_label = sensors_page_create_label(
        panel, "-- OF 10,000", &lv_font_montserrat_12, UI_COLOR_MUTED);
    lv_obj_set_size(sensors_step_goal_label, 230, 18);
    lv_obj_set_style_text_align(sensors_step_goal_label,
                                LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(sensors_step_goal_label, 200, 12);
    sensors_step_progress = lv_bar_create(panel);
    lv_obj_set_size(sensors_step_progress, 230, 8);
    lv_obj_set_pos(sensors_step_progress, 200, 35);
    lv_bar_set_range(sensors_step_progress, 0, SENSORS_STEP_GOAL);
    lv_bar_set_value(sensors_step_progress, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sensors_step_progress,
                              lv_color_hex(UI_COLOR_TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sensors_step_progress, LV_OPA_COVER,
                            LV_PART_MAIN);
    lv_obj_set_style_bg_color(sensors_step_progress,
                              lv_color_hex(UI_COLOR_PRIMARY),
                              LV_PART_INDICATOR);
    lv_obj_set_style_radius(sensors_step_progress, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(sensors_step_progress, 4, LV_PART_INDICATOR);
}

#if defined(FACTORY_HW_USING_KEYBOARD)
static lv_obj_t *sensors_page_create_environment_cell(
    lv_obj_t *panel, lv_coord_t x, const char *symbol,
    const char *name, uint32_t color)
{
    lv_obj_t *icon;
    lv_obj_t *name_label;
    lv_obj_t *value_label;

    icon = sensors_page_create_label(panel, symbol,
                                     &lv_font_montserrat_20, color);
    lv_obj_set_pos(icon, x + 14, 15);
    name_label = sensors_page_create_label(panel, name,
                                           &lv_font_montserrat_12,
                                           UI_COLOR_MUTED);
    lv_obj_set_pos(name_label, x + 46, 11);
    value_label = sensors_page_create_label(panel, "--",
                                            &lv_font_montserrat_20,
                                            UI_COLOR_TEXT);
    lv_obj_set_size(value_label, 126, 28);
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(value_label, x + 14, 48);
    return value_label;
}

static void sensors_page_create_environment(ui_page_t *page)
{
    lv_obj_t *panel;
    lv_obj_t *separator;

    panel = lv_obj_create(page->root);
    lv_obj_set_size(panel, SENSORS_CONTENT_WIDTH, SENSORS_BOTTOM_HEIGHT);
    lv_obj_set_pos(panel, SENSORS_SIDE_PADDING, SENSORS_BOTTOM_Y);
    sensors_page_style_container(panel);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    sensors_temperature_label = sensors_page_create_environment_cell(
        panel, 0, LV_SYMBOL_PLUS, "TEMPERATURE", UI_COLOR_WARM);
    sensors_humidity_label = sensors_page_create_environment_cell(
        panel, 149, LV_SYMBOL_TINT, "HUMIDITY", UI_COLOR_PRIMARY);
    sensors_pressure_label = sensors_page_create_environment_cell(
        panel, 298, LV_SYMBOL_BARS, "PRESSURE", UI_COLOR_SECONDARY);

    separator = lv_obj_create(panel);
    lv_obj_set_size(separator, 1, 66);
    lv_obj_set_pos(separator, 149, 12);
    lv_obj_set_style_bg_color(separator, lv_color_hex(UI_COLOR_TRACK), 0);
    lv_obj_set_style_bg_opa(separator, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(separator, 0, 0);
    lv_obj_set_style_pad_all(separator, 0, 0);

    separator = lv_obj_create(panel);
    lv_obj_set_size(separator, 1, 66);
    lv_obj_set_pos(separator, 298, 12);
    lv_obj_set_style_bg_color(separator, lv_color_hex(UI_COLOR_TRACK), 0);
    lv_obj_set_style_bg_opa(separator, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(separator, 0, 0);
    lv_obj_set_style_pad_all(separator, 0, 0);
}
#else
static void sensors_page_create_motion_chart(ui_page_t *page)
{
    lv_obj_t *panel;
    lv_obj_t *title;

    panel = lv_obj_create(page->root);
    lv_obj_set_size(panel, SENSORS_CONTENT_WIDTH, SENSORS_BOTTOM_HEIGHT);
    lv_obj_set_pos(panel, SENSORS_SIDE_PADDING, SENSORS_BOTTOM_Y);
    sensors_page_style_container(panel);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    title = sensors_page_create_label(panel, "Z-AXIS TREND / 0.1 g",
                                      &lv_font_montserrat_12,
                                      UI_COLOR_MUTED);
    lv_obj_set_pos(title, 14, 9);

    sensors_motion_chart = lv_chart_create(panel);
    lv_obj_set_size(sensors_motion_chart, 420, 54);
    lv_obj_set_pos(sensors_motion_chart, 14, 28);
    lv_chart_set_type(sensors_motion_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(sensors_motion_chart, 24);
    lv_chart_set_range(sensors_motion_chart,
                       LV_CHART_AXIS_PRIMARY_Y, -20, 20);
    lv_chart_set_div_line_count(sensors_motion_chart, 3, 6);
    lv_obj_set_style_bg_opa(sensors_motion_chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(sensors_motion_chart, 0, 0);
    lv_obj_set_style_pad_all(sensors_motion_chart, 0, 0);
    lv_obj_set_style_line_color(sensors_motion_chart,
                                lv_color_hex(UI_COLOR_TRACK), LV_PART_MAIN);
    lv_obj_set_style_line_opa(sensors_motion_chart, LV_OPA_40,
                              LV_PART_MAIN);
    sensors_motion_series = lv_chart_add_series(
        sensors_motion_chart, lv_color_hex(UI_COLOR_PRIMARY),
        LV_CHART_AXIS_PRIMARY_Y);
}
#endif

static void sensors_page_update_device_status(
    const sensor_service_data_t *data, bool data_available)
{
    uint32_t errors = sensor_service_get_init_errors();
    uint32_t required_mask = SENSOR_DATA_ACCELEROMETER |
                             SENSOR_DATA_GYROSCOPE |
                             SENSOR_DATA_STEP_COUNTER;
#if defined(FACTORY_HW_USING_KEYBOARD)
    required_mask |= SENSOR_DATA_TEMPERATURE |
                     SENSOR_DATA_HUMIDITY |
                     SENSOR_DATA_PRESSURE;
#endif

    if (!sensor_service_is_ready())
    {
        lv_label_set_text(sensors_device_label, "SENSOR ERROR");
        lv_obj_set_style_text_color(sensors_device_label,
                                    lv_color_hex(UI_COLOR_ERROR), 0);
    }
    else if (!data_available)
    {
        lv_label_set_text(sensors_device_label, "WAITING FOR DATA");
        lv_obj_set_style_text_color(sensors_device_label,
                                    lv_color_hex(UI_COLOR_WARNING), 0);
    }
    else if ((rt_tick_get() - data->sample_tick) >
             rt_tick_from_millisecond(SENSORS_STALE_TIMEOUT_MS))
    {
        lv_label_set_text(sensors_device_label, "DATA STALE");
        lv_obj_set_style_text_color(sensors_device_label,
                                    lv_color_hex(UI_COLOR_WARNING), 0);
    }
    else if (errors != SENSOR_INIT_ERROR_NONE ||
             (data->valid_mask & required_mask) != required_mask)
    {
        lv_label_set_text(sensors_device_label, "PARTIAL DATA");
        lv_obj_set_style_text_color(sensors_device_label,
                                    lv_color_hex(UI_COLOR_WARNING), 0);
    }
    else
    {
#if defined(FACTORY_HW_USING_KEYBOARD)
        lv_label_set_text(sensors_device_label, "BHI + BME LIVE");
#else
        lv_label_set_text(sensors_device_label, "BHI260AP LIVE");
#endif
        lv_obj_set_style_text_color(sensors_device_label,
                                    lv_color_hex(UI_COLOR_SUCCESS), 0);
    }
}

static void sensors_page_update_imu(const sensor_service_data_t *data)
{
    bool acceleration_valid =
        (data->valid_mask & SENSOR_DATA_ACCELEROMETER) != 0U;
    bool gyroscope_valid =
        (data->valid_mask & SENSOR_DATA_GYROSCOPE) != 0U;
    uint32_t index;

    for (index = 0U; index < SENSORS_AXIS_COUNT; index++)
    {
        char text[32];

        if (acceleration_valid)
        {
            lv_label_set_text_fmt(sensors_acceleration_labels[index],
                                  "%d mg", data->acceleration[index]);
            lv_bar_set_value(sensors_acceleration_bars[index],
                             data->acceleration[index], LV_ANIM_OFF);
        }
        else
        {
            lv_label_set_text(sensors_acceleration_labels[index], "-- mg");
            lv_bar_set_value(sensors_acceleration_bars[index],
                             0, LV_ANIM_OFF);
        }

        if (gyroscope_valid)
        {
            sensors_format_mdps(text, sizeof(text), data->gyroscope[index]);
            lv_label_set_text(sensors_gyroscope_labels[index], text);
        }
        else
        {
            lv_label_set_text(sensors_gyroscope_labels[index], "-- dps");
        }
    }

#if !defined(FACTORY_HW_USING_KEYBOARD)
    if (acceleration_valid && sensors_motion_series != RT_NULL)
    {
        int32_t trend = data->acceleration[2] / 100;

        if (trend < -20)
            trend = -20;
        else if (trend > 20)
            trend = 20;
        lv_chart_set_next_value(sensors_motion_chart,
                                sensors_motion_series, trend);
    }
#endif
}

static void sensors_page_update_steps(const sensor_service_data_t *data)
{
    char text[24];
    uint32_t progress;
    uint32_t percent;

    if ((data->valid_mask & SENSOR_DATA_STEP_COUNTER) == 0U)
    {
        lv_label_set_text(sensors_step_value_label, "--");
        lv_label_set_text(sensors_step_goal_label, "-- OF 10,000");
        lv_bar_set_value(sensors_step_progress, 0, LV_ANIM_OFF);
        return;
    }

    sensors_format_steps(text, sizeof(text), data->step_count);
    lv_label_set_text(sensors_step_value_label, text);
    progress = data->step_count > SENSORS_STEP_GOAL ?
               SENSORS_STEP_GOAL : data->step_count;
    percent = progress * 100U / SENSORS_STEP_GOAL;
    lv_label_set_text_fmt(sensors_step_goal_label, "%u%% OF 10,000",
                          (unsigned int)percent);
    lv_bar_set_value(sensors_step_progress, (int32_t)progress,
                     LV_ANIM_OFF);
}

#if defined(FACTORY_HW_USING_KEYBOARD)
static void sensors_page_update_environment(
    const sensor_service_data_t *data)
{
    char text[32];

    if ((data->valid_mask & SENSOR_DATA_TEMPERATURE) != 0U)
    {
        sensors_format_tenths(text, sizeof(text), data->temperature,
                              "\xC2\xB0" "C");
        lv_label_set_text(sensors_temperature_label, text);
    }
    else
    {
        lv_label_set_text(sensors_temperature_label, "-- \xC2\xB0" "C");
    }

    if ((data->valid_mask & SENSOR_DATA_HUMIDITY) != 0U)
    {
        sensors_format_tenths(text, sizeof(text), data->humidity, "%");
        lv_label_set_text(sensors_humidity_label, text);
    }
    else
    {
        lv_label_set_text(sensors_humidity_label, "-- %");
    }

    if ((data->valid_mask & SENSOR_DATA_PRESSURE) != 0U &&
            data->pressure >= 0)
    {
        uint32_t pressure = (uint32_t)data->pressure;

        rt_snprintf(text, sizeof(text), "%u.%02u hPa",
                    (unsigned int)(pressure / 100U),
                    (unsigned int)(pressure % 100U));
        lv_label_set_text(sensors_pressure_label, text);
    }
    else
    {
        lv_label_set_text(sensors_pressure_label, "-- hPa");
    }
}
#endif

static void sensors_page_update(void)
{
    sensor_service_data_t data;
    bool data_available;

    memset(&data, 0, sizeof(data));
    data_available = sensor_service_get_data(&data) == RT_EOK;
    sensors_page_update_device_status(&data, data_available);
    sensors_page_update_imu(&data);
    sensors_page_update_steps(&data);
#if defined(FACTORY_HW_USING_KEYBOARD)
    sensors_page_update_environment(&data);
#endif
}

static void sensors_page_on_create(ui_page_t *page)
{
    if (page == RT_NULL || page->root == RT_NULL)
        return;

    sensors_page_create_header(page);
    sensors_page_create_imu(page);
    sensors_page_create_steps(page);
#if defined(FACTORY_HW_USING_KEYBOARD)
    sensors_page_create_environment(page);
#else
    sensors_page_create_motion_chart(page);
#endif

    if (page->group != RT_NULL)
    {
        lv_group_set_wrap(page->group, true);
        lv_group_focus_obj(sensors_back_button);
    }
}

static void sensors_page_on_enter(ui_page_t *page)
{
    (void)page;
    sensors_page_update();
}

static void sensors_page_on_destroy(ui_page_t *page)
{
    (void)page;
    sensors_back_button = RT_NULL;
    sensors_device_label = RT_NULL;
    sensors_step_value_label = RT_NULL;
    sensors_step_goal_label = RT_NULL;
    sensors_step_progress = RT_NULL;
    memset(sensors_acceleration_labels, 0,
           sizeof(sensors_acceleration_labels));
    memset(sensors_gyroscope_labels, 0,
           sizeof(sensors_gyroscope_labels));
    memset(sensors_acceleration_bars, 0,
           sizeof(sensors_acceleration_bars));
#if defined(FACTORY_HW_USING_KEYBOARD)
    sensors_temperature_label = RT_NULL;
    sensors_humidity_label = RT_NULL;
    sensors_pressure_label = RT_NULL;
#else
    sensors_motion_chart = RT_NULL;
    sensors_motion_series = RT_NULL;
#endif
}

static void sensors_page_on_timer(ui_page_t *page)
{
    (void)page;
    sensors_page_update();
}

static void sensors_page_on_gesture(ui_page_t *page,
                                    ui_gesture_t gesture)
{
    (void)page;
    if (gesture == UI_GESTURE_RIGHT)
        ui_navigation_switch_async(UI_PAGE_HOME);
}

static void sensors_page_on_event(ui_page_t *page,
                                  const ui_event_t *event)
{
    (void)page;
    if (event == RT_NULL || event->type != UI_EVENT_KEY)
        return;
    if (event->data == LV_KEY_ESC || event->data == LV_KEY_HOME)
        ui_navigation_switch_async(UI_PAGE_HOME);
}

rt_err_t sensors_page_register(void)
{
    const ui_page_t page = {
        .id = UI_PAGE_SENSORS,
        .app_id = UI_APP_NONE,
        .name = "Sensors Page",
        .on_create = sensors_page_on_create,
        .on_destroy = sensors_page_on_destroy,
        .on_enter = sensors_page_on_enter,
        .on_timer = sensors_page_on_timer,
        .on_gesture = sensors_page_on_gesture,
        .on_event = sensors_page_on_event,
        .timer_interval_ms = SENSORS_UPDATE_INTERVAL_MS,
    };

    return ui_navigation_register_page(&page);
}
