#include "home_page.h"

#include <string.h>

#include "ui_config.h"
#include "ui_navigation.h"
#include "ui_status_bar.h"
#include "ui_theme.h"
#include "cell_transform.h"

#define HOME_FOOTER_HEIGHT 88
#define HOME_CONTENT_HEIGHT (UI_SCREEN_HEIGHT - UI_STATUS_BAR_HEIGHT - HOME_FOOTER_HEIGHT)
#define HOME_APP_ROW_HEIGHT 208
#define HOME_APP_INFO_MAX 3
#define HOME_CAROUSEL_ROW_COUNT 5U
#define HOME_CAROUSEL_CENTER_ROW (HOME_CAROUSEL_ROW_COUNT / 2U)
#define HOME_CAROUSEL_PROGRESS_SCALE 1024
#define HOME_CAROUSEL_KEY_ANIM_TIME_MS 220U
#define HOME_RAIL_X 160
#define HOME_RAIL_Y 0
#define HOME_RAIL_HEIGHT 208
#define HOME_HONEYCOMB_NAME_HEIGHT 28
#define HOME_HONEYCOMB_NAME_Y (UI_STATUS_BAR_HEIGHT + 2)
#define HOME_HONEYCOMB_VIEW_Y \
    (UI_STATUS_BAR_HEIGHT + HOME_HONEYCOMB_NAME_HEIGHT + 4)
#define HOME_HONEYCOMB_VIEW_HEIGHT \
    (UI_SCREEN_HEIGHT - HOME_HONEYCOMB_VIEW_Y - HOME_FOOTER_HEIGHT)
#define HOME_HONEYCOMB_OUTER_RADIUS (UI_SCREEN_WIDTH / 9)
#define HOME_HONEYCOMB_INNER_RADIUS (HOME_HONEYCOMB_OUTER_RADIUS * 8 / 9)
#define HOME_HONEYCOMB_ICON_GAP (HOME_HONEYCOMB_OUTER_RADIUS - HOME_HONEYCOMB_INNER_RADIUS)
#define HOME_HONEYCOMB_DRAG_LIMIT 8
#define HOME_HONEYCOMB_THROW_FACTOR 6
#define HOME_HONEYCOMB_SETTLE_TIME_MS 260
#define HOME_HONEYCOMB_TOUCH_FRAME_MS 30U
#define HOME_APP_MUSIC_INDEX 0U
#define HOME_APP_AUDIO_INDEX 1U
#define HOME_APP_LORA_INDEX  2U
#define HOME_APP_CODEX_INDEX 3U
#define HOME_APP_SENSORS_INDEX 4U
#if defined(FACTORY_HW_USING_KEYBOARD)
#define HOME_APP_GPS_INDEX 5U
#define HOME_APP_ESP32C6_INDEX 6U
#define HOME_APP_TIMER_INDEX 7U
#define HOME_APP_CALENDAR_INDEX 8U
#define HOME_APP_CUBE_INDEX 9U
#define HOME_APP_SNAKE_INDEX 10U
#define HOME_APP_TETRIS_INDEX 11U
#define HOME_APP_WEATHER_INDEX 12U
#define HOME_APP_INFRARED_INDEX 13U
#define HOME_APP_POWER_INDEX 14U
#define HOME_APP_STORAGE_INDEX 15U
#define HOME_APP_INPUT_INDEX 16U
#else
#define HOME_APP_TIMER_INDEX 5U
#define HOME_APP_CALENDAR_INDEX 6U
#define HOME_APP_CUBE_INDEX 7U
#define HOME_APP_SNAKE_INDEX 8U
#define HOME_APP_TETRIS_INDEX 9U
#define HOME_APP_WEATHER_INDEX 10U
#define HOME_APP_POWER_INDEX 11U
#define HOME_APP_STORAGE_INDEX 12U
#define HOME_APP_INPUT_INDEX 13U
#endif

LV_IMAGE_DECLARE(music_icon);
LV_IMAGE_DECLARE(record_icon);
LV_IMAGE_DECLARE(lora_icon);
LV_IMAGE_DECLARE(imu_icon);
LV_IMAGE_DECLARE(code_icon);
LV_IMAGE_DECLARE(gps_icon);
LV_IMAGE_DECLARE(wifi_icon);
LV_IMAGE_DECLARE(timer_icon);
LV_IMAGE_DECLARE(calendar_icon);
LV_IMAGE_DECLARE(cube_icon);
LV_IMAGE_DECLARE(snake_icon);
LV_IMAGE_DECLARE(tetris_icon);
LV_IMAGE_DECLARE(weather_icon);
LV_IMAGE_DECLARE(ir_remote_icon);
LV_IMAGE_DECLARE(battery_icon);
LV_IMAGE_DECLARE(file_icon);
LV_IMAGE_DECLARE(keyboard_icon);
LV_IMAGE_DECLARE(info_icon);

typedef struct
{
    const lv_image_dsc_t *icon;
    const char *name;
    const char *field[HOME_APP_INFO_MAX];
    const char *value[HOME_APP_INFO_MAX];
    uint8_t field_count;
} home_app_definition_t;

typedef struct
{
    lv_obj_t *row;
    lv_obj_t *icon;
    lv_obj_t *name;
    lv_obj_t *field[HOME_APP_INFO_MAX];
    lv_obj_t *value[HOME_APP_INFO_MAX];
} home_carousel_row_t;

static const home_app_definition_t home_apps[] = {
    {&music_icon, "MUSIC", {"Source", "Function", "Output"}, {"Bluetooth", "Music player", "Speaker"}, 3},
    {&record_icon, "AUDIO", {"Codec", "Mode", "Format"}, {"AUDCODEC", "Record / play", "WAV / MP3"}, 3},
    {&lora_icon, "LORA", {"Radio", "Band", "Interface"}, {"SX1262", "868 - 923 MHz", "SPI"}, 3},
    {&code_icon, "CODEX", {"Transport", "Agents", "Control"}, {"BLE HID", "6 task slots", "Codex Desktop"}, 3},
#if defined(FACTORY_HW_USING_KEYBOARD)
    {&imu_icon, "SENSORS", {"Motion", "Environment", "Interface"}, {"BHI260AP", "BME280", "I2C"}, 3},
    {&gps_icon, "GPS", {"Receiver", "Interface", "Port"}, {"L76K", "UART", "Shared UART2"}, 3},
    {&wifi_icon, "SLAVE", {"Module", "Interface", "Port"}, {"ESP32-C6", "AT command", "Shared UART2"}, 3},
#else
    {&imu_icon, "SENSORS", {"Motion", "Interface", ""}, {"BHI260AP", "I2C", ""}, 2},
#endif
    {&timer_icon, "TIMER", {"Mode", "Default", "Alert"}, {"Pomodoro", "25 minutes", "UI / vibration"}, 3},
    {&calendar_icon, "CALENDAR", {"Date", "Time zone", "Source"}, {"Current month", "UTC+8", "BT / Wi-Fi / GPS"}, 3},
    {&cube_icon, "3D CUBE", {"Renderer", "Control", "Frame rate"}, {"Software 3D", "Touch / auto", "30 FPS target"}, 3},
    {&snake_icon, "SNAKE", {"Board", "Control", "Mode"}, {"18 x 18", "Keys / joypad", "Endless arcade"}, 3},
    {&tetris_icon, "TETRIS", {"Board", "Pieces", "Mode"}, {"10 x 20", "7 tetrominoes", "Endless levels"}, 3},
    {&weather_icon, "WEATHER", {"Requires", "Location", "Forecast"}, {"Bluetooth PAN", "Current IP", "5 days"}, 3},
#if defined(FACTORY_HW_USING_KEYBOARD)
    {&ir_remote_icon, "IR REMOTE", {"Protocol", "Carrier", "Output"}, {"NEC", "38 kHz", "PA36 PWM"}, 3},
#endif
    {&battery_icon, "POWER", {"Charger", "Battery", "Interface"}, {"SGM41562B", "Li-ion", "I2C"}, 3},
    {&file_icon, "STORAGE", {"Media", "File system", "Interface"}, {"TF card", "FAT", "SPI"}, 3},
#if defined(FACTORY_HW_USING_KEYBOARD)
    {&keyboard_icon, "INPUT", {"Keyboard", "Expander", "Interface"}, {"TCA8418", "XL9555", "I2C"}, 3},
#else
    {&keyboard_icon, "INPUT", {"Joypad", "Input", "Lighting"}, {"Analog / keys", "ADS1115 / XL9555", "WS2812"}, 3},
#endif
    {&info_icon, "SET", {"MCU", "Display", "RTOS"}, {"SF32LB52X", "480 x 480", "RT-Thread"}, 3},
};

#define HOME_APP_COUNT \
    (sizeof(home_apps) / sizeof(home_apps[0]))
#define HOME_CAROUSEL_EDGE_PADDING \
    ((HOME_CONTENT_HEIGHT - HOME_APP_ROW_HEIGHT) / 2)

static lv_obj_t *home_app_list;
static home_carousel_row_t home_carousel_rows[HOME_CAROUSEL_ROW_COUNT];
static lv_obj_t *home_rail;
static lv_obj_t *home_rail_marker;
static lv_obj_t *home_index_label;
static bool home_scroll_repositioning;
static bool home_carousel_positioned;
static bool home_carousel_key_animating;
static int32_t home_carousel_key_start_y;
static int32_t home_carousel_key_end_y;
static int32_t home_carousel_focus_slot = -1;
static uint32_t home_carousel_current_index;
static lv_obj_t *home_honeycomb_view;
static lv_obj_t *home_honeycomb_icons[HOME_APP_COUNT];
static lv_obj_t *home_honeycomb_selection;
static lv_obj_t *home_honeycomb_name_label;
static lv_point_t home_honeycomb_pivots[HOME_APP_COUNT];
static lv_point_t home_honeycomb_offset;
static lv_point_t home_honeycomb_drag_sum;
static lv_point_t home_honeycomb_last_vector;
static lv_point_t home_honeycomb_settle_start;
static lv_point_t home_honeycomb_settle_end;
static lv_timer_t *home_honeycomb_touch_timer;
static uint32_t home_honeycomb_touch_last_update;
static int32_t home_honeycomb_selected = -1;
static int32_t home_honeycomb_key_selected = -1;
static bool home_honeycomb_positioned;
static bool home_honeycomb_drag_active;
static bool home_honeycomb_touch_update_pending;
static home_menu_style_t selected_menu_style = HOME_MENU_STYLE_CAROUSEL;
static uint32_t home_selected_app_index;

static lv_obj_t *home_page_carousel_item_at(int32_t slot);
static int32_t home_page_carousel_center_slot(void);
static void home_page_carousel_row_event(lv_event_t *event);

static bool home_page_open_app(uint32_t app_index)
{
    if (app_index >= HOME_APP_COUNT)
        return false;

    home_selected_app_index = app_index;
    if (app_index == HOME_APP_MUSIC_INDEX)
    {
        ui_navigation_switch_async(UI_PAGE_MUSIC);
        return true;
    }
    if (app_index == HOME_APP_AUDIO_INDEX)
    {
        ui_navigation_switch_async(UI_PAGE_RECORDER);
        return true;
    }
    if (app_index == HOME_APP_LORA_INDEX)
    {
        ui_navigation_switch_async(UI_PAGE_LORA);
        return true;
    }
    if (app_index == HOME_APP_CODEX_INDEX)
    {
        ui_navigation_switch_async(UI_PAGE_CODEX);
        return true;
    }
    if (app_index == HOME_APP_SENSORS_INDEX)
    {
        ui_navigation_switch_async(UI_PAGE_SENSORS);
        return true;
    }
#if defined(FACTORY_HW_USING_KEYBOARD)
    if (app_index == HOME_APP_GPS_INDEX)
    {
        ui_navigation_switch_async(UI_PAGE_GPS);
        return true;
    }
    if (app_index == HOME_APP_ESP32C6_INDEX)
    {
        ui_navigation_switch_async(UI_PAGE_ESP32C6);
        return true;
    }
#endif
    if (app_index == HOME_APP_TIMER_INDEX)
    {
        ui_navigation_switch_async(UI_PAGE_POMODORO);
        return true;
    }
    if (app_index == HOME_APP_CALENDAR_INDEX)
    {
        ui_navigation_switch_async(UI_PAGE_CALENDAR);
        return true;
    }
    if (app_index == HOME_APP_CUBE_INDEX)
    {
        ui_navigation_switch_async(UI_PAGE_CUBE);
        return true;
    }
    if (app_index == HOME_APP_SNAKE_INDEX)
    {
        ui_navigation_switch_async(UI_PAGE_SNAKE);
        return true;
    }
    if (app_index == HOME_APP_TETRIS_INDEX)
    {
        ui_navigation_switch_async(UI_PAGE_TETRIS);
        return true;
    }
    if (app_index == HOME_APP_WEATHER_INDEX)
    {
        ui_navigation_switch_async(UI_PAGE_WEATHER);
        return true;
    }
#if defined(FACTORY_HW_USING_KEYBOARD)
    if (app_index == HOME_APP_INFRARED_INDEX)
    {
        ui_navigation_switch_async(UI_PAGE_INFRARED);
        return true;
    }
#endif
    if (app_index == HOME_APP_POWER_INDEX)
    {
        ui_navigation_switch_async(UI_PAGE_CHARGE);
        return true;
    }
    if (app_index == HOME_APP_STORAGE_INDEX)
    {
        ui_navigation_switch_async(UI_PAGE_STORAGE);
        return true;
    }
    if (app_index == HOME_APP_INPUT_INDEX)
    {
        ui_navigation_switch_async(UI_PAGE_INPUT);
        return true;
    }
    if (app_index == HOME_APP_COUNT - 1U)
    {
        ui_navigation_switch_async(UI_PAGE_SETTINGS);
        return true;
    }
    return false;
}

static lv_obj_t *home_page_create_label(lv_obj_t *parent,
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

static void home_page_style_container(lv_obj_t *object)
{
    lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_radius(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_shadow_width(object, 0, 0);
}

static lv_obj_t *home_page_add_app(lv_obj_t *parent,
                                   home_carousel_row_t *view,
                                   uint32_t slot)
{
    lv_obj_t *row;
    lv_obj_t *icon_area;
    lv_obj_t *separator;
    uint8_t field_index;

    row = lv_obj_create(parent);
    lv_obj_set_size(row, UI_SCREEN_WIDTH, HOME_APP_ROW_HEIGHT);
    home_page_style_container(row);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_SNAPPABLE);
    ui_theme_disable_focus_style(row);
    lv_obj_add_event_cb(row, home_page_carousel_row_event,
                        LV_EVENT_SHORT_CLICKED,
                        (void *)(uintptr_t)slot);

    icon_area = lv_obj_create(row);
    lv_obj_set_size(icon_area, 128, 128);
    lv_obj_set_pos(icon_area, 16, 20);
    home_page_style_container(icon_area);
    lv_obj_remove_flag(icon_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(icon_area, LV_OBJ_FLAG_CLICKABLE);

    view->icon = lv_image_create(icon_area);
    lv_obj_center(view->icon);

    view->name = home_page_create_label(row, "",
                                        &lv_font_montserrat_18,
                                        UI_COLOR_TEXT);
    lv_obj_set_size(view->name, 128, 26);
    lv_obj_set_style_text_align(view->name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(view->name, 16, 154);

    for (field_index = 0; field_index < HOME_APP_INFO_MAX; field_index++)
    {
        lv_coord_t y = 60 + (lv_coord_t)field_index * 38;

        view->field[field_index] = home_page_create_label(
            row, "", &lv_font_montserrat_14, UI_COLOR_MUTED);
        lv_obj_set_size(view->field[field_index], 102, 22);
        lv_obj_set_pos(view->field[field_index], 204, y);

        view->value[field_index] = home_page_create_label(
            row, "", &lv_font_montserrat_16, UI_COLOR_TEXT);
        lv_obj_set_size(view->value[field_index], 158, 24);
        lv_label_set_long_mode(view->value[field_index], LV_LABEL_LONG_DOT);
        lv_obj_set_pos(view->value[field_index], 312, y - 1);
    }

    separator = lv_obj_create(row);
    lv_obj_set_size(separator, 272, 1);
    lv_obj_set_pos(separator, 184, HOME_APP_ROW_HEIGHT - 1);
    lv_obj_set_style_bg_color(separator, lv_color_hex(UI_COLOR_TRACK), 0);
    lv_obj_set_style_bg_opa(separator, LV_OPA_60, 0);
    lv_obj_set_style_border_width(separator, 0, 0);
    lv_obj_set_style_pad_all(separator, 0, 0);
    lv_obj_remove_flag(separator, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(separator, LV_OBJ_FLAG_CLICKABLE);
    view->row = row;
    return row;
}

static void home_page_create_carousel_rail(lv_obj_t *parent)
{
    home_rail = lv_obj_create(parent);
    lv_obj_set_size(home_rail, 8, HOME_RAIL_HEIGHT);
    lv_obj_set_pos(home_rail, HOME_RAIL_X, HOME_RAIL_Y);
    lv_obj_set_style_bg_color(home_rail,
                              lv_color_hex(UI_COLOR_PRIMARY), 0);
    lv_obj_set_style_bg_opa(home_rail, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(home_rail, 0, 0);
    lv_obj_set_style_radius(home_rail, 0, 0);
    lv_obj_set_style_pad_all(home_rail, 0, 0);
    lv_obj_remove_flag(home_rail, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(home_rail, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(home_rail, LV_OBJ_FLAG_FLOATING);

    home_rail_marker = lv_obj_create(parent);
    lv_obj_set_size(home_rail_marker, 12, 0);
    lv_obj_set_pos(home_rail_marker, HOME_RAIL_X - 2, HOME_RAIL_Y);
    lv_obj_set_style_bg_color(home_rail_marker,
                              lv_color_hex(UI_COLOR_SECONDARY), 0);
    lv_obj_set_style_bg_opa(home_rail_marker, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(home_rail_marker, 0, 0);
    lv_obj_set_style_radius(home_rail_marker, 0, 0);
    lv_obj_set_style_pad_all(home_rail_marker, 0, 0);
    lv_obj_remove_flag(home_rail_marker, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(home_rail_marker, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(home_rail_marker, LV_OBJ_FLAG_FLOATING);

    home_index_label = home_page_create_label(parent, "",
                                              &lv_font_montserrat_16,
                                              UI_COLOR_MUTED);
    lv_label_set_text_fmt(home_index_label, "01 / %02u",
                          (unsigned int)HOME_APP_COUNT);
    lv_obj_set_size(home_index_label, 120, 24);
    lv_obj_set_pos(home_index_label,
                   (UI_SCREEN_WIDTH - 120) / 2,
                   HOME_CONTENT_HEIGHT - 28 -
                       HOME_CAROUSEL_EDGE_PADDING);
    lv_obj_set_style_text_align(home_index_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_remove_flag(home_index_label, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(home_index_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(home_index_label, LV_OBJ_FLAG_FLOATING);
    lv_obj_move_foreground(home_index_label);
}

static void home_page_add_footer(lv_obj_t *parent)
{
    lv_obj_t *brand;
    lv_obj_t *product;
    lv_obj_t *footer = lv_obj_create(parent);

    lv_obj_set_size(footer, UI_SCREEN_WIDTH, HOME_FOOTER_HEIGHT);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, 0);
    home_page_style_container(footer);
    lv_obj_remove_flag(footer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(footer, LV_OBJ_FLAG_CLICKABLE);

    brand = home_page_create_label(footer, "LILYGO",
                                   &lv_font_montserrat_30,
                                   UI_COLOR_TEXT);
    lv_obj_set_size(brand, 432, 38);
    lv_obj_set_style_text_align(brand, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(brand, 24, 8);

    product = home_page_create_label(footer, "T-SF32 DISPLAY",
                                     &lv_font_montserrat_14,
                                     UI_COLOR_MUTED);
    lv_obj_set_size(product, 432, 22);
    lv_obj_set_style_text_align(product, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(product, 24, 50);
}

static uint32_t home_page_carousel_wrap_index(int32_t index)
{
    int32_t count = (int32_t)HOME_APP_COUNT;

    index %= count;
    if (index < 0)
        index += count;
    return (uint32_t)index;
}

static void home_page_carousel_row_event(lv_event_t *event)
{
    uint32_t slot =
        (uint32_t)(uintptr_t)lv_event_get_user_data(event);
    uint32_t app_index;

    if (slot >= HOME_CAROUSEL_ROW_COUNT)
        return;
    app_index = home_page_carousel_wrap_index(
        (int32_t)home_carousel_current_index +
        (int32_t)slot - (int32_t)HOME_CAROUSEL_CENTER_ROW);
    home_page_open_app(app_index);
}

static void home_page_carousel_bind_row(home_carousel_row_t *view,
                                        uint32_t app_index)
{
    const home_app_definition_t *app = &home_apps[app_index];
    uint8_t field_index;

    lv_image_set_src(view->icon, app->icon);
    lv_label_set_text_static(view->name, app->name);
    for (field_index = 0; field_index < HOME_APP_INFO_MAX; field_index++)
    {
        if (field_index < app->field_count)
        {
            lv_label_set_text_static(view->field[field_index],
                                     app->field[field_index]);
            lv_label_set_text(view->value[field_index],
                              app->value[field_index]);
            lv_obj_remove_flag(view->field[field_index], LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(view->value[field_index], LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(view->field[field_index], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(view->value[field_index], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void home_page_carousel_bind_rows(void)
{
    uint32_t slot;

    for (slot = 0U; slot < HOME_CAROUSEL_ROW_COUNT; slot++)
    {
        int32_t relative = (int32_t)slot -
                           (int32_t)HOME_CAROUSEL_CENTER_ROW;
        uint32_t app_index = home_page_carousel_wrap_index(
            (int32_t)home_carousel_current_index + relative);

        home_page_carousel_bind_row(&home_carousel_rows[slot], app_index);
    }
}

static void home_page_update_carousel(lv_obj_t *list)
{
    lv_area_t list_area;
    int32_t list_center_y;
    int32_t radius;
    uint32_t child_count;
    uint32_t index;

    lv_obj_get_coords(list, &list_area);
    list_center_y = list_area.y1 + lv_area_get_height(&list_area) / 2;
    radius = lv_area_get_height(&list_area) / 2;
    child_count = lv_obj_get_child_count(list);

    for (index = 0; index < child_count; index++)
    {
        lv_obj_t *child = lv_obj_get_child(list, (int32_t)index);
        lv_area_t child_area;
        int32_t child_center_y;
        int32_t signed_distance;
        int32_t distance;
        int32_t offset;
        int32_t translate_x;
        lv_opa_t opacity;

        if (lv_obj_has_flag(child, LV_OBJ_FLAG_FLOATING))
            continue;

        lv_obj_get_coords(child, &child_area);
        child_center_y = child_area.y1 +
                         lv_area_get_height(&child_area) / 2;
        signed_distance = child_center_y - list_center_y;
        distance = LV_ABS(signed_distance);

        if (distance >= radius)
        {
            offset = radius;
            opacity = LV_OPA_TRANSP;
        }
        else
        {
            uint32_t offset_square =
                (uint32_t)(radius * radius - distance * distance);
            lv_sqrt_res_t square_root;

            lv_sqrt(offset_square, &square_root, 0x8000);
            offset = radius - square_root.i;
            opacity = (lv_opa_t)lv_map(distance, 0, radius,
                                       LV_OPA_COVER, LV_OPA_TRANSP);
        }

        /* Both sides follow the right arc: the current item leaves toward
         * the upper right and the next item enters from the lower right. */
        translate_x = offset;
        lv_obj_set_style_translate_x(child, translate_x, 0);
        lv_obj_set_style_opa(child, opacity, 0);
    }

    if (home_carousel_positioned)
    {
        int32_t center_slot = home_page_carousel_center_slot();
        uint32_t current_index = home_carousel_current_index;
        int32_t marker_height = 0;

        if (center_slot >= 0)
        {
            current_index = home_page_carousel_wrap_index(
                (int32_t)home_carousel_current_index + center_slot -
                (int32_t)HOME_CAROUSEL_CENTER_ROW);
        }
        if (home_index_label != RT_NULL)
        {
            lv_label_set_text_fmt(home_index_label, "%02u / %02u",
                                  (unsigned int)(current_index + 1U),
                                  (unsigned int)HOME_APP_COUNT);
        }

        if (HOME_APP_COUNT > 1U)
        {
            lv_obj_t *middle = home_page_carousel_item_at(
                (int32_t)HOME_CAROUSEL_CENTER_ROW);
            lv_obj_t *next = home_page_carousel_item_at(
                (int32_t)HOME_CAROUSEL_CENTER_ROW + 1);
            lv_area_t middle_area;
            lv_area_t next_area;
            int32_t middle_center_y;
            int32_t next_center_y;
            int32_t spacing;
            int32_t position;
            int32_t cycle = (int32_t)HOME_APP_COUNT *
                            HOME_CAROUSEL_PROGRESS_SCALE;
            int32_t last = ((int32_t)HOME_APP_COUNT - 1) *
                           HOME_CAROUSEL_PROGRESS_SCALE;

            lv_obj_get_coords(middle, &middle_area);
            lv_obj_get_coords(next, &next_area);
            middle_center_y = middle_area.y1 +
                              lv_area_get_height(&middle_area) / 2;
            next_center_y = next_area.y1 +
                            lv_area_get_height(&next_area) / 2;
            spacing = next_center_y - middle_center_y;
            position = (int32_t)home_carousel_current_index *
                       HOME_CAROUSEL_PROGRESS_SCALE;
            if (spacing != 0)
            {
                position += (list_center_y - middle_center_y) *
                            HOME_CAROUSEL_PROGRESS_SCALE / spacing;
            }
            while (position < 0)
                position += cycle;
            while (position >= cycle)
                position -= cycle;

            if (position <= last)
                marker_height = HOME_RAIL_HEIGHT * position / last;
            else
                marker_height = HOME_RAIL_HEIGHT * (cycle - position) /
                                HOME_CAROUSEL_PROGRESS_SCALE;
        }
        else
        {
            marker_height = HOME_RAIL_HEIGHT;
        }
        if (home_rail_marker != RT_NULL)
        {
            lv_obj_set_y(home_rail_marker, HOME_RAIL_Y);
            lv_obj_set_height(home_rail_marker, marker_height);
        }
    }
}

static lv_obj_t *home_page_carousel_item_at(int32_t slot)
{
    if (slot < 0 || slot >= (int32_t)HOME_CAROUSEL_ROW_COUNT)
        return RT_NULL;
    return home_carousel_rows[slot].row;
}

static int32_t home_page_carousel_center_slot(void)
{
    lv_area_t list_area;
    int32_t list_center_y;
    int32_t nearest_slot = -1;
    int32_t nearest_distance = INT32_MAX;
    int32_t slot;

    if (home_app_list == RT_NULL)
        return -1;

    lv_obj_get_coords(home_app_list, &list_area);
    list_center_y = list_area.y1 + lv_area_get_height(&list_area) / 2;
    for (slot = 0; slot < (int32_t)HOME_CAROUSEL_ROW_COUNT; slot++)
    {
        lv_obj_t *item = home_page_carousel_item_at(slot);
        lv_area_t item_area;
        int32_t distance;

        if (item == RT_NULL)
            continue;
        lv_obj_get_coords(item, &item_area);
        distance = LV_ABS(item_area.y1 +
                          lv_area_get_height(&item_area) / 2 -
                          list_center_y);
        if (distance < nearest_distance)
        {
            nearest_distance = distance;
            nearest_slot = slot;
        }
    }
    return nearest_slot;
}

static void home_page_carousel_focus_slot(int32_t slot)
{
    lv_obj_t *item = home_page_carousel_item_at(slot);

    if (item == RT_NULL)
        return;

    lv_group_focus_obj(item);
    home_carousel_focus_slot = slot;
}

static void home_page_carousel_recenter(void)
{
    lv_obj_t *center = home_page_carousel_item_at(
        (int32_t)HOME_CAROUSEL_CENTER_ROW);
    bool was_repositioning = home_scroll_repositioning;

    if (center == RT_NULL)
        return;

    home_scroll_repositioning = true;
    lv_obj_update_layout(home_app_list);
    home_page_carousel_focus_slot((int32_t)HOME_CAROUSEL_CENTER_ROW);
    lv_obj_scroll_to_view_recursive(center, LV_ANIM_OFF);
    home_scroll_repositioning = was_repositioning;
}

static void home_page_carousel_finish_scroll(void)
{
    int32_t center_slot = home_page_carousel_center_slot();

    if (center_slot >= 0)
    {
        int32_t delta = center_slot - (int32_t)HOME_CAROUSEL_CENTER_ROW;

        home_carousel_current_index = home_page_carousel_wrap_index(
            (int32_t)home_carousel_current_index + delta);
        home_selected_app_index = home_carousel_current_index;
    }

    home_scroll_repositioning = true;
    home_page_carousel_bind_rows();
    home_page_carousel_recenter();
    home_scroll_repositioning = false;
    home_page_update_carousel(home_app_list);
}

static void home_page_carousel_key_anim_progress(void *object,
                                                 int32_t progress)
{
    int32_t scroll_y;

    if (object == RT_NULL || object != home_app_list)
        return;

    scroll_y = home_carousel_key_start_y +
               (home_carousel_key_end_y - home_carousel_key_start_y) *
                   progress / 256;
    home_scroll_repositioning = true;
    lv_obj_scroll_to_y(object, scroll_y, LV_ANIM_OFF);
    home_scroll_repositioning = false;
    home_page_update_carousel(object);
}

static void home_page_carousel_key_anim_completed(lv_anim_t *animation)
{
    (void)animation;
    home_carousel_key_animating = false;
    if (home_app_list != RT_NULL && home_carousel_positioned)
        home_page_carousel_finish_scroll();
}

static bool home_page_carousel_start_key_animation(int32_t step)
{
    lv_obj_t *center;
    lv_obj_t *target;
    lv_area_t center_area;
    lv_area_t target_area;
    int32_t center_y;
    int32_t target_y;
    lv_anim_t animation;

    center = home_page_carousel_item_at(
        (int32_t)HOME_CAROUSEL_CENTER_ROW);
    target = home_page_carousel_item_at(
        (int32_t)HOME_CAROUSEL_CENTER_ROW + step);
    if (center == RT_NULL || target == RT_NULL)
        return false;

    lv_obj_update_layout(home_app_list);
    lv_obj_get_coords(center, &center_area);
    lv_obj_get_coords(target, &target_area);
    center_y = center_area.y1 + lv_area_get_height(&center_area) / 2;
    target_y = target_area.y1 + lv_area_get_height(&target_area) / 2;
    if (center_y == target_y)
        return false;

    home_carousel_key_start_y = lv_obj_get_scroll_y(home_app_list);
    home_carousel_key_end_y = home_carousel_key_start_y +
                              target_y - center_y;
    home_carousel_key_animating = true;

    lv_anim_init(&animation);
    lv_anim_set_var(&animation, home_app_list);
    lv_anim_set_values(&animation, 0, 256);
    lv_anim_set_duration(&animation, HOME_CAROUSEL_KEY_ANIM_TIME_MS);
    lv_anim_set_exec_cb(&animation, home_page_carousel_key_anim_progress);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&animation,
                             home_page_carousel_key_anim_completed);
    lv_anim_start(&animation);
    return true;
}

static void home_page_scroll_event(lv_event_t *event)
{
    lv_obj_t *list;
    lv_event_code_t code;

    code = lv_event_get_code(event);
    if (code != LV_EVENT_SCROLL_BEGIN &&
        code != LV_EVENT_SCROLL && code != LV_EVENT_SCROLL_END)
        return;
    if (!home_carousel_positioned)
        return;

    list = lv_event_get_target(event);
    if (home_scroll_repositioning)
        return;

    if (code == LV_EVENT_SCROLL_BEGIN)
    {
        if (home_carousel_key_animating &&
            lv_event_get_param(event) == RT_NULL)
        {
            lv_anim_delete(home_app_list,
                           home_page_carousel_key_anim_progress);
            home_carousel_key_animating = false;
        }
        return;
    }

    if (code == LV_EVENT_SCROLL_END)
    {
        home_page_carousel_finish_scroll();
        return;
    }
    home_page_update_carousel(list);
}

static void home_page_carousel_key(uint32_t key)
{
    int32_t step;

    if (home_app_list == RT_NULL || !home_carousel_positioned)
    {
        return;
    }

    if (key == LV_KEY_ENTER)
    {
        if (!home_carousel_key_animating)
            home_page_open_app(home_carousel_current_index);
        return;
    }

    if (key == LV_KEY_DOWN || key == LV_KEY_RIGHT || key == LV_KEY_NEXT)
        step = 1;
    else if (key == LV_KEY_UP || key == LV_KEY_LEFT || key == LV_KEY_PREV)
        step = -1;
    else
        return;

    if (!home_carousel_key_animating)
        (void)home_page_carousel_start_key_animation(step);
}

static void home_page_carousel_create(ui_page_t *page)
{
    uint32_t slot;

    if (page == RT_NULL || page->root == RT_NULL)
        return;

    lv_obj_set_scrollbar_mode(page->root, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(page->root, LV_DIR_NONE);
    home_carousel_current_index = home_selected_app_index;

    // background = lv_image_create(page->root);
    // lv_image_set_src(background, &dark_bg2);
    // lv_obj_set_size(background, UI_SCREEN_WIDTH, UI_SCREEN_HEIGHT);
    // lv_obj_set_pos(background, 0, 0);
    // lv_obj_remove_flag(background, LV_OBJ_FLAG_CLICKABLE);
    // lv_obj_move_background(background);

    home_app_list = lv_obj_create(page->root);
    lv_obj_set_size(home_app_list, UI_SCREEN_WIDTH, HOME_CONTENT_HEIGHT);
    lv_obj_set_pos(home_app_list, 0, UI_STATUS_BAR_HEIGHT);
    home_page_style_container(home_app_list);
    lv_obj_set_style_pad_top(home_app_list,
                             HOME_CAROUSEL_EDGE_PADDING, 0);
    lv_obj_set_style_pad_bottom(home_app_list,
                                HOME_CAROUSEL_EDGE_PADDING, 0);
    lv_obj_set_scroll_dir(home_app_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(home_app_list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_snap_y(home_app_list, LV_SCROLL_SNAP_NONE);
    lv_obj_add_flag(home_app_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(home_app_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_remove_flag(home_app_list, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_set_flex_flow(home_app_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(home_app_list, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(home_app_list, home_page_scroll_event,
                        LV_EVENT_SCROLL, RT_NULL);
    lv_obj_add_event_cb(home_app_list, home_page_scroll_event,
                        LV_EVENT_SCROLL_BEGIN, RT_NULL);
    lv_obj_add_event_cb(home_app_list, home_page_scroll_event,
                        LV_EVENT_SCROLL_END, RT_NULL);

    for (slot = 0U; slot < HOME_CAROUSEL_ROW_COUNT; slot++)
    {
        lv_obj_t *app = home_page_add_app(home_app_list,
                                          &home_carousel_rows[slot],
                                          slot);

        if (page->group != RT_NULL)
            lv_group_add_obj(page->group, app);
    }
    home_page_carousel_bind_rows();

    home_page_create_carousel_rail(home_app_list);

    /* The footer is fixed on the page and does not participate in carousel
     * transforms or cyclic repositioning. */
    home_page_add_footer(page->root);
}

static void home_page_carousel_enter(ui_page_t *page)
{
    (void)page;
    if (home_app_list != RT_NULL)
    {
        lv_obj_update_layout(home_app_list);
        if (!home_carousel_positioned)
        {
            home_carousel_positioned = true;
            home_page_carousel_recenter();
        }
        home_page_update_carousel(home_app_list);
    }
}

static void home_page_carousel_destroy(ui_page_t *page)
{
    (void)page;
    if (home_app_list != RT_NULL)
    {
        lv_anim_delete(home_app_list,
                       home_page_carousel_key_anim_progress);
    }
    home_app_list = RT_NULL;
    home_rail = RT_NULL;
    home_rail_marker = RT_NULL;
    home_index_label = RT_NULL;
    home_scroll_repositioning = false;
    home_carousel_positioned = false;
    home_carousel_key_animating = false;
    home_carousel_focus_slot = -1;
    home_carousel_current_index = 0U;
    memset(home_carousel_rows, 0, sizeof(home_carousel_rows));
}

static void home_page_honeycomb_get_center(uint32_t index,
                                           int32_t *center_x,
                                           int32_t *center_y)
{
    int32_t column = 0;
    int32_t row = 0;

    if (index != 0U)
    {
        uint32_t total = 0U;
        uint32_t ring = 0U;
        uint32_t ring_icons = 1U;
        uint32_t position;

        while (total + ring_icons - 1U < index)
        {
            total += ring_icons;
            ring++;
            ring_icons = ring * 6U;
        }

        column = -(int32_t)ring;
        for (position = 0U; position < ring_icons; position++)
        {
            if (total + position == index)
                break;

            switch (position / ring)
            {
            case 0:
                column++;
                row--;
                break;
            case 1:
                column++;
                break;
            case 2:
                row++;
                break;
            case 3:
                column--;
                row++;
                break;
            case 4:
                column--;
                break;
            default:
                row--;
                break;
            }
        }
    }

    *center_x = UI_SCREEN_WIDTH / 2 +
                (column - row) * HOME_HONEYCOMB_OUTER_RADIUS;
    *center_y = HOME_HONEYCOMB_VIEW_HEIGHT / 2 +
                (column + row) * HOME_HONEYCOMB_OUTER_RADIUS * 443 / 256;
}

static bool home_page_honeycomb_limit_square(float *center_x,
                                             float *center_y,
                                             float *radius)
{
    const float left = 8.0f;
    const float top = 6.0f;
    const float right = (float)UI_SCREEN_WIDTH - 9.0f;
    const float bottom = (float)HOME_HONEYCOMB_VIEW_HEIGHT - 7.0f;
    float x1 = LV_MAX(left, *center_x - *radius);
    float y1 = LV_MAX(top, *center_y - *radius);
    float x2 = LV_MIN(right, *center_x + *radius);
    float y2 = LV_MIN(bottom, *center_y + *radius);
    float width;
    float height;

    if (x1 > x2 || y1 > y2)
    {
        *radius = 0.0f;
        return false;
    }

    width = x2 - x1 + 1.0f;
    height = y2 - y1 + 1.0f;
    *radius = LV_MIN(width, height) / 2.0f;
    *center_x = x1 + width / 2.0f;
    *center_y = y1 + height / 2.0f;
    return *radius > 0.0f;
}

static void home_page_honeycomb_limit_round(float *center_x,
                                            float *center_y,
                                            float *radius,
                                            float pivot_radius)
{
    const float limit_radius = (float)UI_SCREEN_WIDTH / 2.0f;

    if (pivot_radius + *radius <= limit_radius)
        return;
    if (pivot_radius - *radius >= limit_radius || pivot_radius <= 0.0f)
    {
        *radius = 0.0f;
        return;
    }

    {
        float old_x = *center_x - (float)UI_SCREEN_WIDTH / 2.0f;
        float old_y = *center_y -
                      (float)HOME_HONEYCOMB_VIEW_HEIGHT / 2.0f;
        float new_pivot_radius;

        *radius = (limit_radius - (pivot_radius - *radius)) / 2.0f;
        new_pivot_radius = limit_radius - *radius;
        *center_x += old_x * new_pivot_radius / pivot_radius - old_x;
        *center_y += old_y * new_pivot_radius / pivot_radius - old_y;
    }
}

static void home_page_honeycomb_update(void)
{
    uint32_t nearest_index = 0U;
    float nearest_distance = (float)UI_SCREEN_WIDTH;
    float nearest_x = 0.0f;
    float nearest_y = 0.0f;
    float nearest_radius = 0.0f;
    bool found_nearest = false;
    uint32_t index;

    if (home_honeycomb_view == RT_NULL)
        return;

    for (index = 0U; index < HOME_APP_COUNT; index++)
    {
        lv_obj_t *icon = home_honeycomb_icons[index];
        float center_x;
        float center_y;
        float radius = (float)HOME_HONEYCOMB_OUTER_RADIUS;
        float pivot_radius = 0.0f;

        if (icon == RT_NULL)
            continue;

        center_x = (float)(home_honeycomb_pivots[index].x +
                           home_honeycomb_offset.x);
        center_y = (float)(home_honeycomb_pivots[index].y +
                           home_honeycomb_offset.y);

        if (get_icon_transform_param(
                center_x, center_y, radius,
                &center_x, &center_y, &radius, &pivot_radius,
                (float)UI_SCREEN_WIDTH,
                (float)HOME_HONEYCOMB_VIEW_HEIGHT))
        {
            home_page_honeycomb_limit_round(&center_x, &center_y,
                                            &radius, pivot_radius);
            home_page_honeycomb_limit_square(&center_x, &center_y,
                                             &radius);
        }
        else
        {
            radius = 0.0f;
        }

        if (radius > (float)HOME_HONEYCOMB_ICON_GAP)
            radius -= (float)HOME_HONEYCOMB_ICON_GAP;
        else
            radius = 0.0f;

        if (radius > 0.0f)
        {
            int32_t image_width = home_apps[index].icon->header.w;
            int32_t image_height = home_apps[index].icon->header.h;
            uint32_t scale = (uint32_t)(radius * 2.0f * 256.0f /
                                        (float)image_width);

            lv_obj_remove_flag(icon, LV_OBJ_FLAG_HIDDEN);
            lv_image_set_scale(icon, scale);
            lv_obj_move_to(icon,
                           (int32_t)center_x - image_width / 2,
                           (int32_t)center_y - image_height / 2);

            if (!found_nearest || pivot_radius < nearest_distance)
            {
                nearest_distance = pivot_radius;
                nearest_index = index;
                nearest_x = center_x;
                nearest_y = center_y;
                nearest_radius = radius;
                found_nearest = true;
            }
        }
        else
        {
            lv_obj_add_flag(icon, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (found_nearest && home_honeycomb_selection != RT_NULL)
    {
        int32_t ring_size = (int32_t)(nearest_radius * 2.0f) + 8;
        bool selection_changed =
            home_honeycomb_selected != (int32_t)nearest_index;

        home_honeycomb_selected = (int32_t)nearest_index;
        if (home_honeycomb_name_label != RT_NULL)
        {
            if (selection_changed)
            {
                lv_label_set_text_static(home_honeycomb_name_label,
                                         home_apps[nearest_index].name);
                lv_obj_move_foreground(home_honeycomb_name_label);
            }
            lv_obj_remove_flag(home_honeycomb_name_label,
                               LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_set_size(home_honeycomb_selection, ring_size, ring_size);
        /* The ring changes size while dragging. Keep its position in the
         * style so a layout refresh cannot move it briefly back to (0, 0). */
        lv_obj_set_pos(home_honeycomb_selection,
                       (int32_t)nearest_x - ring_size / 2,
                       (int32_t)nearest_y - ring_size / 2);
        lv_obj_remove_flag(home_honeycomb_selection, LV_OBJ_FLAG_HIDDEN);
        if (selection_changed)
            lv_obj_move_foreground(home_honeycomb_selection);
    }
    else if (home_honeycomb_selection != RT_NULL)
    {
        lv_obj_add_flag(home_honeycomb_selection, LV_OBJ_FLAG_HIDDEN);
        if (home_honeycomb_name_label != RT_NULL)
            lv_obj_add_flag(home_honeycomb_name_label,
                            LV_OBJ_FLAG_HIDDEN);
    }
}

static void home_page_honeycomb_render_touch_frame(void)
{
    if (home_honeycomb_view == RT_NULL)
        return;

    /* Moving every icon produces many overlapping invalid areas. Register the
     * complete view first so LVGL can merge the child updates into one area. */
    lv_obj_invalidate(home_honeycomb_view);
    home_page_honeycomb_update();
    home_honeycomb_touch_last_update = lv_tick_get();
}

static void home_page_honeycomb_touch_timer_cb(lv_timer_t *timer)
{
    if (!home_honeycomb_touch_update_pending ||
        home_honeycomb_view == RT_NULL)
    {
        lv_timer_pause(timer);
        return;
    }

    home_honeycomb_touch_update_pending = false;
    home_page_honeycomb_render_touch_frame();
    lv_timer_pause(timer);
}

static void home_page_honeycomb_schedule_touch_frame(void)
{
    uint32_t elapsed = lv_tick_elaps(home_honeycomb_touch_last_update);

    if (elapsed >= HOME_HONEYCOMB_TOUCH_FRAME_MS ||
        home_honeycomb_touch_timer == RT_NULL)
    {
        home_honeycomb_touch_update_pending = false;
        home_page_honeycomb_render_touch_frame();
        return;
    }

    home_honeycomb_touch_update_pending = true;
    lv_timer_set_period(home_honeycomb_touch_timer,
                        HOME_HONEYCOMB_TOUCH_FRAME_MS - elapsed);
    lv_timer_reset(home_honeycomb_touch_timer);
    lv_timer_resume(home_honeycomb_touch_timer);
}

static uint32_t home_page_honeycomb_nearest_for_offset(
    const lv_point_t *offset)
{
    const int32_t center_x = UI_SCREEN_WIDTH / 2;
    const int32_t center_y = HOME_HONEYCOMB_VIEW_HEIGHT / 2;
    uint32_t nearest_index = 0U;
    uint32_t nearest_distance = UINT32_MAX;
    uint32_t index;

    for (index = 0U; index < HOME_APP_COUNT; index++)
    {
        int32_t delta_x = home_honeycomb_pivots[index].x +
                          offset->x - center_x;
        int32_t delta_y = home_honeycomb_pivots[index].y +
                          offset->y - center_y;
        uint32_t distance = (uint32_t)(delta_x * delta_x +
                                       delta_y * delta_y);

        if (distance < nearest_distance)
        {
            nearest_distance = distance;
            nearest_index = index;
        }
    }
    return nearest_index;
}

static void home_page_honeycomb_settle_progress(void *object,
                                                int32_t progress)
{
    (void)object;
    home_honeycomb_offset.x =
        home_honeycomb_settle_start.x +
        (home_honeycomb_settle_end.x -
         home_honeycomb_settle_start.x) *
            progress / 256;
    home_honeycomb_offset.y =
        home_honeycomb_settle_start.y +
        (home_honeycomb_settle_end.y -
         home_honeycomb_settle_start.y) *
            progress / 256;
    home_page_honeycomb_update();
}

static bool home_page_honeycomb_center_app(uint32_t index,
                                           lv_anim_enable_t animation)
{
    lv_anim_t settle_animation;

    if (home_honeycomb_view == RT_NULL || index >= HOME_APP_COUNT)
        return false;

    home_selected_app_index = index;
    home_honeycomb_key_selected = (int32_t)index;

    home_honeycomb_settle_start = home_honeycomb_offset;
    home_honeycomb_settle_end.x =
        UI_SCREEN_WIDTH / 2 - home_honeycomb_pivots[index].x;
    home_honeycomb_settle_end.y =
        HOME_HONEYCOMB_VIEW_HEIGHT / 2 -
        home_honeycomb_pivots[index].y;

    if (home_honeycomb_settle_start.x == home_honeycomb_settle_end.x &&
        home_honeycomb_settle_start.y == home_honeycomb_settle_end.y)
    {
        return false;
    }

    lv_anim_delete(home_honeycomb_view,
                   home_page_honeycomb_settle_progress);
    if (animation == LV_ANIM_OFF)
    {
        home_honeycomb_offset = home_honeycomb_settle_end;
        home_page_honeycomb_update();
        return true;
    }

    lv_anim_init(&settle_animation);
    lv_anim_set_var(&settle_animation, home_honeycomb_view);
    lv_anim_set_values(&settle_animation, 0, 256);
    lv_anim_set_duration(&settle_animation,
                         HOME_HONEYCOMB_SETTLE_TIME_MS);
    lv_anim_set_exec_cb(&settle_animation,
                        home_page_honeycomb_settle_progress);
    lv_anim_set_path_cb(&settle_animation, lv_anim_path_ease_out);
    lv_anim_start(&settle_animation);
    return true;
}

static void home_page_honeycomb_key(uint32_t key)
{
    uint32_t current_index;

    if (home_honeycomb_view == RT_NULL || HOME_APP_COUNT == 0U)
        return;

    current_index = home_honeycomb_key_selected >= 0
                        ? (uint32_t)home_honeycomb_key_selected
                        : (home_honeycomb_selected >= 0
                               ? (uint32_t)home_honeycomb_selected
                               : 0U);

    if (key == LV_KEY_ENTER)
    {
        if (home_page_open_app(current_index))
            return;
        home_page_honeycomb_center_app(current_index, LV_ANIM_ON);
        return;
    }
    if (key == LV_KEY_PREV || key == LV_KEY_NEXT ||
        key == LV_KEY_UP || key == LV_KEY_DOWN ||
        key == LV_KEY_LEFT || key == LV_KEY_RIGHT)
    {
        uint32_t target_index;

        if (key == LV_KEY_NEXT || key == LV_KEY_DOWN ||
            key == LV_KEY_RIGHT)
            target_index = (current_index + 1U) % HOME_APP_COUNT;
        else
            target_index = (current_index + HOME_APP_COUNT - 1U) %
                           HOME_APP_COUNT;

        home_page_honeycomb_center_app(target_index, LV_ANIM_ON);
        return;
    }
}

static void home_page_honeycomb_icon_event(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_SHORT_CLICKED &&
        !home_honeycomb_drag_active)
    {
        uint32_t index =
            (uint32_t)(uintptr_t)lv_event_get_user_data(event);

        if (home_page_open_app(index))
            return;
        home_page_honeycomb_center_app(index, LV_ANIM_ON);
    }
}

static void home_page_honeycomb_view_event(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);

    if (code == LV_EVENT_PRESSED)
    {
        lv_anim_delete(home_honeycomb_view,
                       home_page_honeycomb_settle_progress);
        home_honeycomb_drag_sum.x = 0;
        home_honeycomb_drag_sum.y = 0;
        home_honeycomb_last_vector.x = 0;
        home_honeycomb_last_vector.y = 0;
        home_honeycomb_drag_active = false;
        home_honeycomb_key_selected = -1;
        home_honeycomb_touch_update_pending = false;
        home_honeycomb_touch_last_update =
            lv_tick_get() - HOME_HONEYCOMB_TOUCH_FRAME_MS;
        if (home_honeycomb_touch_timer != RT_NULL)
            lv_timer_pause(home_honeycomb_touch_timer);
    }
    else if (code == LV_EVENT_PRESSING)
    {
        lv_indev_t *input = lv_event_get_indev(event);
        lv_point_t vector;

        if (input == RT_NULL)
            input = lv_indev_active();
        if (input == RT_NULL)
            return;

        lv_indev_get_vect(input, &vector);
        if (!home_honeycomb_drag_active)
        {
            home_honeycomb_drag_sum.x += vector.x;
            home_honeycomb_drag_sum.y += vector.y;
            if (LV_ABS(home_honeycomb_drag_sum.x) >
                    HOME_HONEYCOMB_DRAG_LIMIT ||
                LV_ABS(home_honeycomb_drag_sum.y) >
                    HOME_HONEYCOMB_DRAG_LIMIT)
            {
                home_honeycomb_drag_active = true;
                home_honeycomb_offset.x += home_honeycomb_drag_sum.x;
                home_honeycomb_offset.y += home_honeycomb_drag_sum.y;
                home_honeycomb_drag_sum.x = 0;
                home_honeycomb_drag_sum.y = 0;
                home_honeycomb_last_vector = vector;
                home_page_honeycomb_schedule_touch_frame();
            }
        }
        else if (vector.x != 0 || vector.y != 0)
        {
            home_honeycomb_offset.x += vector.x;
            home_honeycomb_offset.y += vector.y;
            home_honeycomb_last_vector = vector;
            home_page_honeycomb_schedule_touch_frame();
        }
    }
    else if (code == LV_EVENT_RELEASED ||
             code == LV_EVENT_PRESS_LOST)
    {
        if (home_honeycomb_drag_active)
        {
            lv_point_t projected_offset = home_honeycomb_offset;
            uint32_t nearest_index;

            if (home_honeycomb_touch_update_pending)
            {
                home_honeycomb_touch_update_pending = false;
                if (home_honeycomb_touch_timer != RT_NULL)
                    lv_timer_pause(home_honeycomb_touch_timer);
                home_page_honeycomb_render_touch_frame();
            }

            projected_offset.x += home_honeycomb_last_vector.x *
                                  HOME_HONEYCOMB_THROW_FACTOR;
            projected_offset.y += home_honeycomb_last_vector.y *
                                  HOME_HONEYCOMB_THROW_FACTOR;
            nearest_index = home_page_honeycomb_nearest_for_offset(
                &projected_offset);
            home_page_honeycomb_center_app(nearest_index, LV_ANIM_ON);
        }

        home_honeycomb_drag_sum.x = 0;
        home_honeycomb_drag_sum.y = 0;
        home_honeycomb_drag_active = false;
    }
}

static void home_page_honeycomb_create(ui_page_t *page)
{
    uint32_t index;

    if (page == RT_NULL || page->root == RT_NULL)
        return;

    lv_obj_set_scrollbar_mode(page->root, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(page->root, LV_DIR_NONE);

    home_honeycomb_view = lv_obj_create(page->root);
    lv_obj_set_size(home_honeycomb_view, UI_SCREEN_WIDTH,
                    HOME_HONEYCOMB_VIEW_HEIGHT);
    lv_obj_set_pos(home_honeycomb_view, 0, HOME_HONEYCOMB_VIEW_Y);
    home_page_style_container(home_honeycomb_view);
    lv_obj_set_scrollbar_mode(home_honeycomb_view, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(home_honeycomb_view, LV_DIR_NONE);
    lv_obj_remove_flag(home_honeycomb_view, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(home_honeycomb_view, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(home_honeycomb_view,
                        home_page_honeycomb_view_event,
                        LV_EVENT_PRESSED, RT_NULL);
    lv_obj_add_event_cb(home_honeycomb_view,
                        home_page_honeycomb_view_event,
                        LV_EVENT_PRESSING, RT_NULL);
    lv_obj_add_event_cb(home_honeycomb_view,
                        home_page_honeycomb_view_event,
                        LV_EVENT_RELEASED, RT_NULL);
    lv_obj_add_event_cb(home_honeycomb_view,
                        home_page_honeycomb_view_event,
                        LV_EVENT_PRESS_LOST, RT_NULL);

    for (index = 0U; index < HOME_APP_COUNT; index++)
    {
        lv_obj_t *image;
        int32_t center_x;
        int32_t center_y;

        home_page_honeycomb_get_center(index, &center_x, &center_y);
        home_honeycomb_pivots[index].x = center_x;
        home_honeycomb_pivots[index].y = center_y;

        image = lv_image_create(home_honeycomb_view);
        lv_image_set_src(image, home_apps[index].icon);
        lv_image_set_pivot(image,
                           home_apps[index].icon->header.w / 2,
                           home_apps[index].icon->header.h / 2);
        lv_obj_add_flag(image, LV_OBJ_FLAG_CLICKABLE |
                                   LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_remove_flag(image, LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_add_event_cb(image, home_page_honeycomb_icon_event,
                            LV_EVENT_SHORT_CLICKED,
                            (void *)(uintptr_t)index);
        home_honeycomb_icons[index] = image;
    }

    home_honeycomb_touch_timer = lv_timer_create(
        home_page_honeycomb_touch_timer_cb,
        HOME_HONEYCOMB_TOUCH_FRAME_MS, RT_NULL);
    if (home_honeycomb_touch_timer != RT_NULL)
        lv_timer_pause(home_honeycomb_touch_timer);

    home_honeycomb_selection = lv_obj_create(home_honeycomb_view);
    home_page_style_container(home_honeycomb_selection);
    lv_obj_set_style_border_width(home_honeycomb_selection, 3, 0);
    lv_obj_set_style_border_color(home_honeycomb_selection,
                                  lv_color_hex(UI_COLOR_SECONDARY), 0);
    lv_obj_set_style_border_opa(home_honeycomb_selection,
                                LV_OPA_50, 0);
    lv_obj_set_style_radius(home_honeycomb_selection,
                            LV_RADIUS_CIRCLE, 0);
    lv_obj_remove_flag(home_honeycomb_selection, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(home_honeycomb_selection, LV_OBJ_FLAG_SCROLLABLE);

    home_honeycomb_name_label = home_page_create_label(
        page->root, "", &lv_font_montserrat_18,
        UI_COLOR_TEXT);
    lv_obj_set_size(home_honeycomb_name_label, UI_SCREEN_WIDTH - 120,
                    HOME_HONEYCOMB_NAME_HEIGHT);
    lv_obj_set_pos(home_honeycomb_name_label, 60,
                   HOME_HONEYCOMB_NAME_Y);
    lv_obj_set_style_text_align(home_honeycomb_name_label,
                                LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(home_honeycomb_name_label,
                           LV_LABEL_LONG_DOT);
    lv_obj_remove_flag(home_honeycomb_name_label,
                       LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(home_honeycomb_name_label, LV_OBJ_FLAG_HIDDEN);

    home_page_add_footer(page->root);
}

static void home_page_honeycomb_enter(ui_page_t *page)
{
    (void)page;
    if (home_honeycomb_view == RT_NULL)
        return;

    if (!home_honeycomb_positioned)
    {
        home_honeycomb_offset.x = 0;
        home_honeycomb_offset.y = 0;
        home_honeycomb_drag_sum.x = 0;
        home_honeycomb_drag_sum.y = 0;
        home_honeycomb_last_vector.x = 0;
        home_honeycomb_last_vector.y = 0;
        home_honeycomb_positioned = true;
        home_page_honeycomb_center_app(home_selected_app_index,
                                       LV_ANIM_OFF);
        home_page_honeycomb_update();
    }
    else
    {
        home_page_honeycomb_update();
    }
}

static void home_page_honeycomb_destroy(ui_page_t *page)
{
    uint32_t index;

    (void)page;
    for (index = 0U; index < HOME_APP_COUNT; index++)
    {
        home_honeycomb_icons[index] = RT_NULL;
    }
    if (home_honeycomb_view != RT_NULL)
    {
        lv_anim_delete(home_honeycomb_view,
                       home_page_honeycomb_settle_progress);
    }
    if (home_honeycomb_touch_timer != RT_NULL)
    {
        lv_timer_delete(home_honeycomb_touch_timer);
        home_honeycomb_touch_timer = RT_NULL;
    }
    home_honeycomb_view = RT_NULL;
    home_honeycomb_selection = RT_NULL;
    home_honeycomb_name_label = RT_NULL;
    home_honeycomb_selected = -1;
    home_honeycomb_key_selected = -1;
    home_honeycomb_positioned = false;
    home_honeycomb_drag_active = false;
    home_honeycomb_touch_update_pending = false;
    home_honeycomb_touch_last_update = 0U;
}

typedef struct
{
    ui_page_lifecycle_cb_t create;
    ui_page_lifecycle_cb_t enter;
    ui_page_lifecycle_cb_t destroy;
} home_menu_style_ops_t;

static const home_menu_style_ops_t menu_style_ops[HOME_MENU_STYLE_COUNT] = {
    [HOME_MENU_STYLE_CAROUSEL] = {
        .create = home_page_carousel_create,
        .enter = home_page_carousel_enter,
        .destroy = home_page_carousel_destroy,
    },
    [HOME_MENU_STYLE_HONEYCOMB] = {
        .create = home_page_honeycomb_create,
        .enter = home_page_honeycomb_enter,
        .destroy = home_page_honeycomb_destroy,
    },
};

static void home_page_on_create(ui_page_t *page)
{
    const home_menu_style_ops_t *ops = &menu_style_ops[selected_menu_style];

    if (ops->create != RT_NULL)
        ops->create(page);
}

static void home_page_on_enter(ui_page_t *page)
{
    const home_menu_style_ops_t *ops = &menu_style_ops[selected_menu_style];

    if (ops->enter != RT_NULL)
        ops->enter(page);
}

static void home_page_on_destroy(ui_page_t *page)
{
    const home_menu_style_ops_t *ops = &menu_style_ops[selected_menu_style];

    if (ops->destroy != RT_NULL)
        ops->destroy(page);
}

static void home_page_on_event(ui_page_t *page, const ui_event_t *event)
{
    (void)page;
    if (event == RT_NULL || event->type != UI_EVENT_KEY)
        return;

    if (selected_menu_style == HOME_MENU_STYLE_CAROUSEL)
        home_page_carousel_key((uint32_t)event->data);
    else if (selected_menu_style == HOME_MENU_STYLE_HONEYCOMB)
        home_page_honeycomb_key((uint32_t)event->data);
}

rt_err_t home_page_register(void)
{
    const ui_page_t page = {
        .id = UI_PAGE_HOME,
        .app_id = UI_APP_NONE,
        .name = "Home Page",
        .on_create = home_page_on_create,
        .on_enter = home_page_on_enter,
        .on_destroy = home_page_on_destroy,
        .on_event = home_page_on_event,
    };

    return ui_navigation_register_page(&page);
}

rt_err_t home_page_set_menu_style(home_menu_style_t style)
{
    ui_page_t *page;

    if (style >= HOME_MENU_STYLE_COUNT)
        return -RT_EINVAL;
    if (style == selected_menu_style)
        return RT_EOK;

    page = ui_navigation_get_page(UI_PAGE_HOME);
    if (page != RT_NULL && page->is_created)
    {
        if (ui_navigation_get_current_page_id() == UI_PAGE_HOME)
            return -RT_EBUSY;
        ui_navigation_destroy_page(UI_PAGE_HOME);
    }

    selected_menu_style = style;
    return RT_EOK;
}

home_menu_style_t home_page_get_menu_style(void)
{
    return selected_menu_style;
}
