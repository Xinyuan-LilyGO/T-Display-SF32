#include "gps_page.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <lvgl.h>
#include <ulog.h>

#include "communication_service.h"
#include "ui_config.h"
#include "ui_navigation.h"
#include "ui_status_bar.h"
#include "ui_theme.h"

#define GPS_SIDE_PADDING       24
#define GPS_CONTENT_WIDTH      (UI_SCREEN_WIDTH - GPS_SIDE_PADDING * 2)
#define GPS_HEADER_Y           (UI_STATUS_BAR_HEIGHT + 8)
#define GPS_FIX_Y              (UI_STATUS_BAR_HEIGHT + 60)
#define GPS_COORDINATE_Y       (UI_STATUS_BAR_HEIGHT + 106)
#define GPS_VISUAL_Y           (UI_STATUS_BAR_HEIGHT + 176)
#define GPS_METRICS_Y          (UI_STATUS_BAR_HEIGHT + 348)
#define GPS_COMPASS_SIZE       154
#define GPS_COMPASS_CENTER     77
#define GPS_SIGNAL_BAR_COUNT   8U
#define GPS_UPDATE_INTERVAL_MS 500U

typedef struct
{
    int8_t x;
    int8_t y;
} gps_direction_vector_t;

static const gps_direction_vector_t gps_direction_vectors[16] = {
    {0, -45}, {17, -42}, {32, -32}, {42, -17},
    {45, 0}, {42, 17}, {32, 32}, {17, 42},
    {0, 45}, {-17, 42}, {-32, 32}, {-42, 17},
    {-45, 0}, {-42, -17}, {-32, -32}, {-17, -42},
};

static const char *const gps_cardinal_directions[16] = {
    "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
    "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW",
};

static lv_point_precise_t gps_needle_points[2] = {
    {GPS_COMPASS_CENTER, GPS_COMPASS_CENTER},
    {GPS_COMPASS_CENTER, GPS_COMPASS_CENTER - 45},
};
static lv_point_precise_t gps_needle_tail_points[2] = {
    {GPS_COMPASS_CENTER, GPS_COMPASS_CENTER},
    {GPS_COMPASS_CENTER, GPS_COMPASS_CENTER + 22},
};

static lv_obj_t *gps_back_button;
static lv_obj_t *gps_technology_label;
static lv_obj_t *gps_rx_count_label;
static lv_obj_t *gps_fix_indicator;
static lv_obj_t *gps_fix_label;
static lv_obj_t *gps_details_label;
static lv_obj_t *gps_latitude_label;
static lv_obj_t *gps_longitude_label;
static lv_obj_t *gps_needle;
static lv_obj_t *gps_needle_tail;
static lv_obj_t *gps_course_label;
static lv_obj_t *gps_signal_label;
static lv_obj_t *gps_signal_bars[GPS_SIGNAL_BAR_COUNT];
static lv_obj_t *gps_altitude_label;
static lv_obj_t *gps_speed_label;
static lv_obj_t *gps_hdop_label;
static uint32_t gps_diagnostic_last_rx_count;
static uint8_t gps_diagnostic_ticks;
static bool gps_diagnostic_initialized;
static bool gps_diagnostic_last_fix;

static void gps_page_log_diagnostics(
    rt_err_t rx_result, uint32_t rx_count,
    rt_err_t status_result, const communication_service_status_t *status,
    rt_err_t info_result, const communication_gps_info_t *info)
{
    uint32_t delta = 0U;
    bool rx_changed = false;
    bool fix_changed = false;

    if (rx_result == RT_EOK)
    {
        if (gps_diagnostic_initialized)
        {
            delta = rx_count - gps_diagnostic_last_rx_count;
            rx_changed = delta != 0U;
        }
        else
        {
            delta = rx_count;
            rx_changed = true;
        }
    }
    if (info_result == RT_EOK)
    {
        fix_changed = !gps_diagnostic_initialized ||
                      info->valid != gps_diagnostic_last_fix;
    }

    gps_diagnostic_ticks++;
    if (rx_changed || fix_changed || gps_diagnostic_ticks >= 4U)
    {
        if (rx_result == RT_EOK)
        {
            if (rx_changed)
            {
                LOG_I("GPS UART2 RX: total=%u delta=%u",
                      (unsigned int)rx_count, (unsigned int)delta);
            }
            else
            {
                LOG_W("GPS UART2 RX idle: total=%u, no new data for 2 s",
                      (unsigned int)rx_count);
            }
        }
        else
        {
            LOG_E("GPS UART2 RX counter unavailable: %d", rx_result);
        }

        if (status_result == RT_EOK)
        {
            LOG_I("GPS mux: uart=%u active=%s gps=%u esp=%u esp_busy=%u",
                  status->uart_active ? 1U : 0U,
                  status->active_device == COMM_DEVICE_GPS ? "GPS" : "ESP32C6",
                  status->gps_available ? 1U : 0U,
                  status->esp_available ? 1U : 0U,
                  status->esp_busy ? 1U : 0U);
        }
        else
        {
            LOG_E("GPS communication status unavailable: %d",
                  status_result);
        }

        if (info_result == RT_EOK)
        {
            LOG_I("GPS parsed: fix=%u sats=%u lat=%.6f lon=%.6f "
                  "hdop=%.2f utc=%04d-%02d-%02d %02d:%02d:%02d",
                  info->valid ? 1U : 0U,
                  (unsigned int)info->satellites,
                  info->latitude, info->longitude, info->hdop,
                  info->time.year, info->time.month, info->time.day,
                  info->time.hour, info->time.minute, info->time.second);
        }
        else
        {
            LOG_E("GPS parsed data unavailable: %d", info_result);
        }
        gps_diagnostic_ticks = 0U;
    }

    if (rx_result == RT_EOK)
        gps_diagnostic_last_rx_count = rx_count;
    if (info_result == RT_EOK)
        gps_diagnostic_last_fix = info->valid;
    gps_diagnostic_initialized = true;
}

static lv_obj_t *gps_page_create_label(lv_obj_t *parent,
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

static void gps_page_style_button(lv_obj_t *button)
{
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_radius(button, 4, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    ui_theme_apply_focus_style(button);
}

static void gps_page_add_separator(lv_obj_t *parent,
                                   lv_coord_t x,
                                   lv_coord_t y,
                                   lv_coord_t width,
                                   lv_coord_t height)
{
    lv_obj_t *separator = lv_obj_create(parent);

    lv_obj_set_size(separator, width, height);
    lv_obj_set_pos(separator, x, y);
    lv_obj_set_style_bg_color(separator, lv_color_hex(UI_COLOR_TRACK), 0);
    lv_obj_set_style_bg_opa(separator, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(separator, 0, 0);
    lv_obj_set_style_radius(separator, 0, 0);
    lv_obj_set_style_pad_all(separator, 0, 0);
    lv_obj_remove_flag(separator, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(separator, LV_OBJ_FLAG_CLICKABLE);
}

static uint32_t gps_abs_scaled(double value, uint32_t scale)
{
    double magnitude = value < 0.0 ? -value : value;

    return (uint32_t)(magnitude * (double)scale + 0.5);
}

static void gps_format_fixed(char *buffer, size_t buffer_size,
                             double value, uint32_t decimals,
                             const char *unit)
{
    uint32_t scale = decimals == 2U ? 100U : 10U;
    uint32_t scaled = gps_abs_scaled(value, scale);

    if (decimals == 2U)
    {
        rt_snprintf(buffer, buffer_size, "%s%u.%02u%s",
                    value < 0.0 ? "-" : "",
                    (unsigned int)(scaled / scale),
                    (unsigned int)(scaled % scale), unit);
    }
    else
    {
        rt_snprintf(buffer, buffer_size, "%s%u.%u%s",
                    value < 0.0 ? "-" : "",
                    (unsigned int)(scaled / scale),
                    (unsigned int)(scaled % scale), unit);
    }
}

static void gps_format_coordinate(char *buffer, size_t buffer_size,
                                  double value, bool latitude)
{
    uint32_t scaled = gps_abs_scaled(value, 1000000U);
    char hemisphere;

    if (latitude)
        hemisphere = value < 0.0 ? 'S' : 'N';
    else
        hemisphere = value < 0.0 ? 'W' : 'E';
    rt_snprintf(buffer, buffer_size, "%u.%06u %c",
                (unsigned int)(scaled / 1000000U),
                (unsigned int)(scaled % 1000000U), hemisphere);
}

static uint32_t gps_course_sector(double course)
{
    int32_t normalized = (int32_t)(course * 10.0 + 0.5);

    while (normalized < 0)
        normalized += 3600;
    normalized %= 3600;
    return (uint32_t)((normalized + 112) / 225) % 16U;
}

static void gps_page_back_clicked(lv_event_t *event)
{
    (void)event;
    ui_navigation_switch_async(UI_PAGE_HOME);
}

static void gps_page_create_header(ui_page_t *page)
{
    lv_obj_t *back_icon;
    lv_obj_t *title;

    gps_back_button = lv_button_create(page->root);
    lv_obj_set_size(gps_back_button, 48, 44);
    lv_obj_set_pos(gps_back_button, 16, GPS_HEADER_Y);
    gps_page_style_button(gps_back_button);
    lv_obj_add_event_cb(gps_back_button, gps_page_back_clicked,
                        LV_EVENT_CLICKED, RT_NULL);

    back_icon = gps_page_create_label(gps_back_button, LV_SYMBOL_LEFT,
                                      &lv_font_montserrat_24,
                                      UI_COLOR_TEXT);
    lv_obj_center(back_icon);

    title = gps_page_create_label(page->root, "GPS",
                                  &lv_font_montserrat_24,
                                  UI_COLOR_TEXT);
    lv_obj_set_size(title, 180, 34);
    lv_obj_set_pos(title, 76, GPS_HEADER_Y + 5);

    gps_technology_label = gps_page_create_label(
        page->root, "UART2 / GPS", &lv_font_montserrat_14,
        UI_COLOR_MUTED);
    lv_obj_set_size(gps_technology_label, 180, 24);
    lv_obj_set_style_text_align(gps_technology_label,
                                LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(gps_technology_label, 276, GPS_HEADER_Y + 9);

    gps_rx_count_label = gps_page_create_label(
        page->root, "RX -- chars", &lv_font_montserrat_12,
        UI_COLOR_MUTED);
    lv_obj_set_size(gps_rx_count_label, 180, 20);
    lv_obj_set_style_text_align(gps_rx_count_label,
                                LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(gps_rx_count_label, 276, GPS_HEADER_Y + 29);

    if (page->group != RT_NULL)
        lv_group_add_obj(page->group, gps_back_button);
}

static void gps_page_create_fix_status(ui_page_t *page)
{
    gps_fix_indicator = lv_obj_create(page->root);
    lv_obj_set_size(gps_fix_indicator, 10, 10);
    lv_obj_set_pos(gps_fix_indicator, GPS_SIDE_PADDING, GPS_FIX_Y + 8);
    lv_obj_set_style_bg_color(gps_fix_indicator,
                              lv_color_hex(UI_COLOR_WARNING), 0);
    lv_obj_set_style_bg_opa(gps_fix_indicator, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(gps_fix_indicator, 0, 0);
    lv_obj_set_style_radius(gps_fix_indicator, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(gps_fix_indicator, 0, 0);
    lv_obj_remove_flag(gps_fix_indicator, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(gps_fix_indicator, LV_OBJ_FLAG_CLICKABLE);

    gps_fix_label = gps_page_create_label(page->root, "SEARCHING",
                                          &lv_font_montserrat_16,
                                          UI_COLOR_WARNING);
    lv_obj_set_size(gps_fix_label, 120, 24);
    lv_obj_set_pos(gps_fix_label, GPS_SIDE_PADDING + 20, GPS_FIX_Y);

    gps_details_label = gps_page_create_label(
        page->root, "0 SAT  |  WAITING FOR UTC",
        &lv_font_montserrat_14, UI_COLOR_MUTED);
    lv_obj_set_size(gps_details_label, 300, 24);
    lv_label_set_long_mode(gps_details_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(gps_details_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(gps_details_label,
                   UI_SCREEN_WIDTH - GPS_SIDE_PADDING - 300,
                   GPS_FIX_Y + 1);

    gps_page_add_separator(page->root, GPS_SIDE_PADDING,
                           GPS_FIX_Y + 32, GPS_CONTENT_WIDTH, 1);
}

static lv_obj_t *gps_page_create_coordinate(ui_page_t *page,
                                            lv_coord_t x,
                                            const char *title)
{
    lv_obj_t *title_label;
    lv_obj_t *value_label;

    title_label = gps_page_create_label(page->root, title,
                                        &lv_font_montserrat_14,
                                        UI_COLOR_MUTED);
    lv_obj_set_size(title_label, 208, 20);
    lv_obj_set_pos(title_label, x, GPS_COORDINATE_Y);

    value_label = gps_page_create_label(page->root, "--",
                                        &lv_font_montserrat_20,
                                        UI_COLOR_TEXT);
    lv_obj_set_size(value_label, 208, 28);
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(value_label, x, GPS_COORDINATE_Y + 24);
    return value_label;
}

static void gps_page_create_coordinates(ui_page_t *page)
{
    gps_latitude_label = gps_page_create_coordinate(
        page, GPS_SIDE_PADDING, "LATITUDE");
    gps_longitude_label = gps_page_create_coordinate(
        page, 248, "LONGITUDE");
    gps_page_add_separator(page->root, GPS_SIDE_PADDING,
                           GPS_COORDINATE_Y + 60,
                           GPS_CONTENT_WIDTH, 1);
}

static void gps_page_create_compass(ui_page_t *page)
{
    lv_obj_t *compass;
    lv_obj_t *center;
    lv_obj_t *label;

    compass = lv_obj_create(page->root);
    lv_obj_set_size(compass, GPS_COMPASS_SIZE, GPS_COMPASS_SIZE);
    lv_obj_set_pos(compass, GPS_SIDE_PADDING, GPS_VISUAL_Y);
    lv_obj_set_style_bg_opa(compass, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(compass, 2, 0);
    lv_obj_set_style_border_color(compass, lv_color_hex(UI_COLOR_TRACK), 0);
    lv_obj_set_style_radius(compass, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(compass, 0, 0);
    lv_obj_remove_flag(compass, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(compass, LV_OBJ_FLAG_CLICKABLE);

    label = gps_page_create_label(compass, "N", &lv_font_montserrat_14,
                                  UI_COLOR_PRIMARY);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 7);
    label = gps_page_create_label(compass, "E", &lv_font_montserrat_14,
                                  UI_COLOR_MUTED);
    lv_obj_align(label, LV_ALIGN_RIGHT_MID, -8, 0);
    label = gps_page_create_label(compass, "S", &lv_font_montserrat_14,
                                  UI_COLOR_MUTED);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -7);
    label = gps_page_create_label(compass, "W", &lv_font_montserrat_14,
                                  UI_COLOR_MUTED);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 8, 0);

    gps_needle_tail = lv_line_create(compass);
    lv_line_set_points(gps_needle_tail, gps_needle_tail_points, 2);
    lv_obj_set_style_line_width(gps_needle_tail, 3, 0);
    lv_obj_set_style_line_color(gps_needle_tail,
                                lv_color_hex(UI_COLOR_MUTED), 0);
    lv_obj_set_style_line_rounded(gps_needle_tail, true, 0);

    gps_needle = lv_line_create(compass);
    lv_line_set_points(gps_needle, gps_needle_points, 2);
    lv_obj_set_style_line_width(gps_needle, 4, 0);
    lv_obj_set_style_line_color(gps_needle,
                                lv_color_hex(UI_COLOR_PRIMARY), 0);
    lv_obj_set_style_line_rounded(gps_needle, true, 0);

    center = lv_obj_create(compass);
    lv_obj_set_size(center, 10, 10);
    lv_obj_set_pos(center, 72, 72);
    lv_obj_set_style_bg_color(center, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_bg_opa(center, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(center, 0, 0);
    lv_obj_set_style_radius(center, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(center, 0, 0);
    lv_obj_remove_flag(center, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(center, LV_OBJ_FLAG_CLICKABLE);
}

static void gps_page_create_signal_bars(lv_obj_t *parent,
                                        lv_coord_t x,
                                        lv_coord_t y)
{
    static const uint8_t bar_heights[GPS_SIGNAL_BAR_COUNT] = {
        18U, 28U, 42U, 58U, 48U, 34U, 22U, 14U,
    };
    uint32_t index;

    for (index = 0U; index < GPS_SIGNAL_BAR_COUNT; index++)
    {
        gps_signal_bars[index] = lv_obj_create(parent);
        lv_obj_set_size(gps_signal_bars[index], 14, bar_heights[index]);
        lv_obj_set_pos(gps_signal_bars[index],
                       x + (lv_coord_t)index * 23,
                       y + 58 - bar_heights[index]);
        lv_obj_set_style_bg_color(gps_signal_bars[index],
                                  lv_color_hex(UI_COLOR_TRACK), 0);
        lv_obj_set_style_bg_opa(gps_signal_bars[index], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(gps_signal_bars[index], 0, 0);
        lv_obj_set_style_radius(gps_signal_bars[index], 2, 0);
        lv_obj_set_style_pad_all(gps_signal_bars[index], 0, 0);
        lv_obj_remove_flag(gps_signal_bars[index], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(gps_signal_bars[index], LV_OBJ_FLAG_CLICKABLE);
    }
}

static void gps_page_create_navigation_data(ui_page_t *page)
{
    lv_obj_t *label;

    label = gps_page_create_label(page->root, "COURSE",
                                  &lv_font_montserrat_14,
                                  UI_COLOR_MUTED);
    lv_obj_set_size(label, 170, 20);
    lv_obj_set_pos(label, 204, GPS_VISUAL_Y + 2);

    gps_course_label = gps_page_create_label(
        page->root, "--", &lv_font_montserrat_30, UI_COLOR_TEXT);
    lv_obj_set_size(gps_course_label, 252, 40);
    lv_label_set_long_mode(gps_course_label, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(gps_course_label, 204, GPS_VISUAL_Y + 24);

    label = gps_page_create_label(page->root, "SATELLITE SIGNAL",
                                  &lv_font_montserrat_14,
                                  UI_COLOR_MUTED);
    lv_obj_set_size(label, 170, 20);
    lv_obj_set_pos(label, 204, GPS_VISUAL_Y + 82);

    gps_signal_label = gps_page_create_label(
        page->root, "SEARCHING", &lv_font_montserrat_14,
        UI_COLOR_WARNING);
    lv_obj_set_size(gps_signal_label, 100, 20);
    lv_obj_set_style_text_align(gps_signal_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(gps_signal_label, 356, GPS_VISUAL_Y + 82);

    gps_page_create_signal_bars(page->root, 204, GPS_VISUAL_Y + 104);
}

static lv_obj_t *gps_page_create_metric(ui_page_t *page,
                                        lv_coord_t x,
                                        const char *title)
{
    lv_obj_t *title_label;
    lv_obj_t *value_label;

    title_label = gps_page_create_label(page->root, title,
                                        &lv_font_montserrat_14,
                                        UI_COLOR_MUTED);
    lv_obj_set_size(title_label, 128, 20);
    lv_obj_set_pos(title_label, x, GPS_METRICS_Y + 8);

    value_label = gps_page_create_label(page->root, "--",
                                        &lv_font_montserrat_20,
                                        UI_COLOR_TEXT);
    lv_obj_set_size(value_label, 128, 28);
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(value_label, x, GPS_METRICS_Y + 31);
    return value_label;
}

static void gps_page_create_metrics(ui_page_t *page)
{
    gps_page_add_separator(page->root, GPS_SIDE_PADDING,
                           GPS_METRICS_Y, GPS_CONTENT_WIDTH, 1);
    gps_page_add_separator(page->root, 168, GPS_METRICS_Y + 8, 1, 50);
    gps_page_add_separator(page->root, 320, GPS_METRICS_Y + 8, 1, 50);
    gps_altitude_label = gps_page_create_metric(
        page, GPS_SIDE_PADDING, "ALTITUDE");
    gps_speed_label = gps_page_create_metric(page, 184, "SPEED");
    gps_hdop_label = gps_page_create_metric(page, 336, "HDOP");
}

static void gps_page_set_fix_state(const char *text,
                                   uint32_t color)
{
    lv_label_set_text(gps_fix_label, text);
    lv_obj_set_style_text_color(gps_fix_label, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(gps_fix_indicator, lv_color_hex(color), 0);
}

static void gps_page_update_compass(double course, bool valid)
{
    uint32_t sector;
    const gps_direction_vector_t *vector;
    char course_value[24];
    char course_text[40];

    if (!valid)
    {
        lv_obj_add_flag(gps_needle, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(gps_needle_tail, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(gps_course_label, "--");
        return;
    }

    sector = gps_course_sector(course);
    vector = &gps_direction_vectors[sector];
    gps_needle_points[1].x = GPS_COMPASS_CENTER + vector->x;
    gps_needle_points[1].y = GPS_COMPASS_CENTER + vector->y;
    gps_needle_tail_points[1].x = GPS_COMPASS_CENTER - vector->x / 2;
    gps_needle_tail_points[1].y = GPS_COMPASS_CENTER - vector->y / 2;
    lv_line_set_points(gps_needle, gps_needle_points, 2);
    lv_line_set_points(gps_needle_tail, gps_needle_tail_points, 2);
    lv_obj_remove_flag(gps_needle, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(gps_needle_tail, LV_OBJ_FLAG_HIDDEN);

    gps_format_fixed(course_value, sizeof(course_value), course, 1U,
                     "\xC2\xB0");
    rt_snprintf(course_text, sizeof(course_text), "%s  %s",
                course_value, gps_cardinal_directions[sector]);
    lv_label_set_text(gps_course_label, course_text);
}

static void gps_page_update_signal(uint32_t satellites, bool fixed)
{
    uint32_t active_bars = (satellites + 1U) / 2U;
    uint32_t index;
    const char *signal_text;
    uint32_t signal_color;

    if (active_bars > GPS_SIGNAL_BAR_COUNT)
        active_bars = GPS_SIGNAL_BAR_COUNT;
    for (index = 0U; index < GPS_SIGNAL_BAR_COUNT; index++)
    {
        lv_obj_set_style_bg_color(
            gps_signal_bars[index],
            lv_color_hex(index < active_bars ? UI_COLOR_PRIMARY :
                                                 UI_COLOR_TRACK), 0);
    }

    if (!fixed)
    {
        signal_text = satellites > 0U ? "ACQUIRING" : "SEARCHING";
        signal_color = UI_COLOR_WARNING;
    }
    else if (satellites >= 8U)
    {
        signal_text = "STRONG";
        signal_color = UI_COLOR_SUCCESS;
    }
    else
    {
        signal_text = "WEAK";
        signal_color = UI_COLOR_WARNING;
    }
    lv_label_set_text(gps_signal_label, signal_text);
    lv_obj_set_style_text_color(gps_signal_label,
                                lv_color_hex(signal_color), 0);
}

static void gps_page_update(void)
{
    communication_service_status_t status;
    communication_gps_info_t info;
    uint32_t rx_char_count;
    rt_err_t rx_result;
    rt_err_t status_result;
    rt_err_t result;
    char text[64];

    memset(&status, 0, sizeof(status));
    memset(&info, 0, sizeof(info));
    rx_char_count = 0U;
    rx_result = communication_service_get_gps_rx_char_count(&rx_char_count);
    if (rx_result == RT_EOK)
    {
        lv_label_set_text_fmt(gps_rx_count_label, "RX %u chars",
                              (unsigned int)rx_char_count);
    }
    else
    {
        lv_label_set_text(gps_rx_count_label, "RX -- chars");
    }
    status_result = communication_service_get_status(&status);
    if (status_result == RT_EOK && status.uart_active &&
            status.active_device == COMM_DEVICE_GPS)
    {
        lv_label_set_text(gps_technology_label, "UART2 / GPS");
        lv_obj_set_style_text_color(gps_technology_label,
                                    lv_color_hex(UI_COLOR_SUCCESS), 0);
    }
    else
    {
        gps_page_log_diagnostics(rx_result, rx_char_count,
                                 status_result, &status,
                                 -RT_EBUSY, &info);
        lv_label_set_text(gps_technology_label, "UART2 / ERROR");
        lv_obj_set_style_text_color(gps_technology_label,
                                    lv_color_hex(UI_COLOR_ERROR), 0);
        gps_page_set_fix_state(status.esp_busy ? "UART BUSY" :
                                                 "GPS OFFLINE",
                               status.esp_busy ? UI_COLOR_WARNING :
                                                 UI_COLOR_ERROR);
        return;
    }

    result = communication_service_get_gps_info(&info);
    gps_page_log_diagnostics(rx_result, rx_char_count,
                             status_result, &status,
                             result, &info);
    if (result != RT_EOK)
    {
        gps_page_set_fix_state("GPS OFFLINE", UI_COLOR_ERROR);
        return;
    }

    gps_page_set_fix_state(info.valid ? "GPS FIX" : "SEARCHING",
                           info.valid ? UI_COLOR_SUCCESS :
                                        UI_COLOR_WARNING);

    if (info.time.year >= 2000 && info.time.month >= 1 &&
            info.time.month <= 12 && info.time.day >= 1 &&
            info.time.day <= 31)
    {
        rt_snprintf(text, sizeof(text),
                    "%u SAT  |  %04d-%02d-%02d %02d:%02d:%02d UTC",
                    (unsigned int)info.satellites,
                    info.time.year, info.time.month, info.time.day,
                    info.time.hour, info.time.minute, info.time.second);
    }
    else
    {
        rt_snprintf(text, sizeof(text), "%u SAT  |  WAITING FOR UTC",
                    (unsigned int)info.satellites);
    }
    lv_label_set_text(gps_details_label, text);
    gps_page_update_signal(info.satellites, info.valid);

    if (!info.valid)
    {
        lv_label_set_text(gps_latitude_label, "--");
        lv_label_set_text(gps_longitude_label, "--");
        lv_label_set_text(gps_altitude_label, "--");
        lv_label_set_text(gps_speed_label, "--");
        lv_label_set_text(gps_hdop_label, "--");
        gps_page_update_compass(0.0, false);
        return;
    }

    gps_format_coordinate(text, sizeof(text), info.latitude, true);
    lv_label_set_text(gps_latitude_label, text);
    gps_format_coordinate(text, sizeof(text), info.longitude, false);
    lv_label_set_text(gps_longitude_label, text);
    gps_format_fixed(text, sizeof(text), info.altitude, 1U, " m");
    lv_label_set_text(gps_altitude_label, text);
    gps_format_fixed(text, sizeof(text), info.speed, 1U, " km/h");
    lv_label_set_text(gps_speed_label, text);
    gps_format_fixed(text, sizeof(text), info.hdop, 2U, "");
    lv_label_set_text(gps_hdop_label, text);
    gps_page_update_compass(info.course, true);
}

static void gps_page_on_create(ui_page_t *page)
{
    if (page == RT_NULL || page->root == RT_NULL)
        return;

    gps_page_create_header(page);
    gps_page_create_fix_status(page);
    gps_page_create_coordinates(page);
    gps_page_create_compass(page);
    gps_page_create_navigation_data(page);
    gps_page_create_metrics(page);

    if (page->group != RT_NULL)
    {
        lv_group_set_wrap(page->group, true);
        lv_group_focus_obj(gps_back_button);
    }
}

static void gps_page_on_enter(ui_page_t *page)
{
    communication_service_status_t status;
    uint32_t rx_count = 0U;
    rt_err_t select_result;
    rt_err_t status_result;
    rt_err_t rx_result;

    (void)page;
    gps_diagnostic_initialized = false;
    gps_diagnostic_ticks = 0U;
    gps_diagnostic_last_rx_count = 0U;
    gps_diagnostic_last_fix = false;

    select_result = communication_service_select_device(COMM_DEVICE_GPS);
    memset(&status, 0, sizeof(status));
    status_result = communication_service_get_status(&status);
    rx_result = communication_service_get_gps_rx_char_count(&rx_count);
    LOG_I("GPS page enter: select=%d ready=%u errors=0x%02x "
          "status=%d uart=%u active=%s rx_result=%d rx=%u",
          select_result,
          communication_service_is_ready() ? 1U : 0U,
          (unsigned int)communication_service_get_init_errors(),
          status_result,
          status.uart_active ? 1U : 0U,
          status.active_device == COMM_DEVICE_GPS ? "GPS" : "ESP32C6",
          rx_result, (unsigned int)rx_count);
    gps_page_update();
}

static void gps_page_on_leave(ui_page_t *page)
{
    rt_err_t result;

    (void)page;
    result = communication_service_select_device(COMM_DEVICE_ESP32C6);
    LOG_I("GPS page leave: switch to ESP32C6 result=%d", result);
}

static void gps_page_on_destroy(ui_page_t *page)
{
    (void)page;
    gps_back_button = RT_NULL;
    gps_technology_label = RT_NULL;
    gps_rx_count_label = RT_NULL;
    gps_fix_indicator = RT_NULL;
    gps_fix_label = RT_NULL;
    gps_details_label = RT_NULL;
    gps_latitude_label = RT_NULL;
    gps_longitude_label = RT_NULL;
    gps_needle = RT_NULL;
    gps_needle_tail = RT_NULL;
    gps_course_label = RT_NULL;
    gps_signal_label = RT_NULL;
    gps_altitude_label = RT_NULL;
    gps_speed_label = RT_NULL;
    gps_hdop_label = RT_NULL;
    memset(gps_signal_bars, 0, sizeof(gps_signal_bars));
}

static void gps_page_on_timer(ui_page_t *page)
{
    (void)page;
    gps_page_update();
}

static void gps_page_on_gesture(ui_page_t *page, ui_gesture_t gesture)
{
    (void)page;
    if (gesture == UI_GESTURE_RIGHT)
        ui_navigation_switch_async(UI_PAGE_HOME);
}

static void gps_page_on_event(ui_page_t *page, const ui_event_t *event)
{
    (void)page;
    if (event == RT_NULL || event->type != UI_EVENT_KEY)
        return;
    if (event->data == LV_KEY_ESC || event->data == LV_KEY_HOME)
        ui_navigation_switch_async(UI_PAGE_HOME);
}

rt_err_t gps_page_register(void)
{
    const ui_page_t page = {
        .id = UI_PAGE_GPS,
        .app_id = UI_APP_NONE,
        .name = "GPS Page",
        .on_create = gps_page_on_create,
        .on_destroy = gps_page_on_destroy,
        .on_enter = gps_page_on_enter,
        .on_leave = gps_page_on_leave,
        .on_timer = gps_page_on_timer,
        .on_gesture = gps_page_on_gesture,
        .on_event = gps_page_on_event,
        .timer_interval_ms = GPS_UPDATE_INTERVAL_MS,
    };

    return ui_navigation_register_page(&page);
}
