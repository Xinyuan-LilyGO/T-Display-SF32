#include "settings_page.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <ulog.h>

#include "settings_service.h"
#include "ui_config.h"
#include "ui_navigation.h"
#include "ui_status_bar.h"
#include "ui_theme.h"

#define SETTINGS_HEADER_HEIGHT 60
#define SETTINGS_CONTENT_Y (UI_STATUS_BAR_HEIGHT + SETTINGS_HEADER_HEIGHT)
#define SETTINGS_CONTENT_HEIGHT (UI_SCREEN_HEIGHT - SETTINGS_CONTENT_Y)
#define SETTINGS_ROW_HEIGHT 60
#define SETTINGS_SIDE_PADDING 24
#define SETTINGS_CONTENT_WIDTH \
    (UI_SCREEN_WIDTH - (SETTINGS_SIDE_PADDING * 2))
#define SETTINGS_MOTOR_EFFECT_COUNT 4U
#define SETTINGS_BRIGHTNESS_MIN 10U
#define SETTINGS_WIFI_ROWS_PER_PAGE 6U
#define SETTINGS_WIFI_REFRESH_TICKS 10U
#define SETTINGS_WIFI_KEYBOARD_Y 20
#define SETTINGS_WIFI_KEYBOARD_HEIGHT 292

typedef enum
{
    SETTINGS_ITEM_WIFI = 0,
    SETTINGS_ITEM_BLUETOOTH,
    SETTINGS_ITEM_BRIGHTNESS,
    SETTINGS_ITEM_VOLUME,
    SETTINGS_ITEM_MENU_STYLE,
    SETTINGS_ITEM_POWER_OFF,
    SETTINGS_ITEM_RESTART,
    SETTINGS_ITEM_KEYBOARD_LED,
    SETTINGS_ITEM_MOTOR,
    SETTINGS_ITEM_RGB_LED,
    SETTINGS_ITEM_COUNT,
} settings_item_id_t;

typedef enum
{
    SETTINGS_KIND_SWITCH = 0,
    SETTINGS_KIND_SLIDER,
    SETTINGS_KIND_MENU_STYLE,
    SETTINGS_KIND_ACTION,
    SETTINGS_KIND_RGB,
    SETTINGS_KIND_WIFI,
} settings_kind_t;

typedef struct
{
    const char *name;
    const char *icon;
    uint32_t color;
    settings_kind_t kind;
} settings_item_definition_t;

typedef struct
{
    bool wifi_enabled;
    bool bluetooth_enabled;
    bool bluetooth_pan_enabled;
    uint8_t brightness;
    uint8_t volume;
    uint8_t menu_style;
    uint8_t keyboard_led_brightness;
    bool motor_enabled;
    uint8_t motor_amplitude;
    uint8_t motor_effect;
    bool rgb_enabled;
    uint8_t rgb_brightness;
    uint8_t rgb_color;
} settings_ui_state_t;

static const settings_item_definition_t settings_items[] = {
    [SETTINGS_ITEM_WIFI] = {
        "Wi-Fi", LV_SYMBOL_WIFI, UI_COLOR_PRIMARY, SETTINGS_KIND_WIFI},
    [SETTINGS_ITEM_BLUETOOTH] = {"Bluetooth", LV_SYMBOL_BLUETOOTH, UI_COLOR_DEFAULT_BLUE, SETTINGS_KIND_SWITCH},
    [SETTINGS_ITEM_BRIGHTNESS] = {"Brightness", LV_SYMBOL_EYE_OPEN, UI_COLOR_WARNING, SETTINGS_KIND_SLIDER},
    [SETTINGS_ITEM_VOLUME] = {"Volume", LV_SYMBOL_AUDIO, UI_COLOR_SECONDARY, SETTINGS_KIND_SLIDER},
    [SETTINGS_ITEM_MENU_STYLE] = {"Menu style", LV_SYMBOL_LIST, UI_COLOR_PRIMARY, SETTINGS_KIND_MENU_STYLE},
    [SETTINGS_ITEM_POWER_OFF] = {"Power off", LV_SYMBOL_POWER, UI_COLOR_ERROR, SETTINGS_KIND_ACTION},
    [SETTINGS_ITEM_RESTART] = {"Restart", LV_SYMBOL_REFRESH, UI_COLOR_WARNING, SETTINGS_KIND_ACTION},
    [SETTINGS_ITEM_KEYBOARD_LED] = {"Keyboard LED", LV_SYMBOL_KEYBOARD, UI_COLOR_PRIMARY, SETTINGS_KIND_SLIDER},
    [SETTINGS_ITEM_MOTOR] = {"Motor", LV_SYMBOL_LOOP, UI_COLOR_SECONDARY, SETTINGS_KIND_SWITCH},
    [SETTINGS_ITEM_RGB_LED] = {"RGB LED", LV_SYMBOL_SETTINGS, UI_COLOR_PRIMARY, SETTINGS_KIND_RGB},
};

static const uint32_t rgb_colors[] = {
    0x39FF88,
    0x43B6FF,
    0xF0CF61,
    0xFF3B5C,
    0xFFFFFF,
};

static const char *const rgb_color_names[] = {
    "Green",
    "Blue",
    "Yellow",
    "Red",
    "White",
};

static const char *const motor_effect_names[SETTINGS_MOTOR_EFFECT_COUNT] = {
    "Short pulse",
    "Double pulse",
    "Long pulse",
    "Heartbeat",
};

static const char *const motor_effect_descriptions[SETTINGS_MOTOR_EFFECT_COUNT] = {
    "One short, crisp vibration",
    "Two quick vibration pulses",
    "One sustained vibration",
    "A soft rhythmic vibration",
};

static settings_ui_state_t settings_state = {
    .wifi_enabled = false,
    .bluetooth_enabled = true,
    .bluetooth_pan_enabled = false,
    .brightness = 50U,
    .volume = 70U,
    .menu_style = 0U,
    .keyboard_led_brightness = 60U,
    .motor_enabled = true,
    .motor_amplitude = 70U,
    .motor_effect = 0U,
    .rgb_enabled = true,
    .rgb_brightness = 70U,
    .rgb_color = 0U,
};

static ui_page_t *settings_page_owner;
static lv_obj_t *settings_main_view;
static lv_obj_t *settings_main_back;
static lv_obj_t *settings_main_list;
static lv_obj_t *settings_rows[SETTINGS_ITEM_COUNT];
static lv_obj_t *settings_focus_rows[SETTINGS_ITEM_COUNT];
static uint32_t settings_focus_row_count;
static lv_obj_t *settings_value_labels[SETTINGS_ITEM_COUNT];
static lv_obj_t *settings_detail_view;
static lv_obj_t *settings_detail_back;
static lv_obj_t *settings_detail_body;
static lv_obj_t *settings_detail_value_label;
static lv_obj_t *settings_detail_switch_value_label;
static lv_obj_t *settings_pan_value_label;
static lv_obj_t *settings_pan_control;
static lv_obj_t *settings_active_slider;
static settings_item_id_t settings_active_slider_item;
static lv_obj_t *settings_menu_buttons[2];
static lv_obj_t *settings_menu_check_labels[2];
static lv_obj_t *settings_rgb_buttons[sizeof(rgb_colors) / sizeof(rgb_colors[0])];
static lv_obj_t *settings_motor_amplitude_label;
static lv_obj_t *settings_motor_amplitude_slider;
static lv_obj_t *settings_motor_effect_buttons[SETTINGS_MOTOR_EFFECT_COUNT];
static lv_obj_t *settings_motor_effect_checks[SETTINGS_MOTOR_EFFECT_COUNT];
static settings_item_id_t settings_active_item = SETTINGS_ITEM_BLUETOOTH;

static void settings_page_update_pan_status(void);

#if defined(FACTORY_HW_USING_KEYBOARD)
static communication_wifi_status_t settings_wifi_status;
static communication_wifi_network_t
    settings_wifi_networks[COMM_WIFI_MAX_NETWORKS];
static size_t settings_wifi_network_count;
static uint32_t settings_wifi_revision = UINT32_MAX;
static uint8_t settings_wifi_page;
static uint8_t settings_wifi_refresh_ticks;
static bool settings_wifi_auto_scan_pending;
static lv_obj_t *settings_wifi_switch;
static lv_obj_t *settings_wifi_status_label;
static lv_obj_t *settings_wifi_rssi_label;
static lv_obj_t *settings_wifi_scan_button;
static lv_obj_t *settings_wifi_scan_label;
static lv_obj_t *settings_wifi_network_buttons[SETTINGS_WIFI_ROWS_PER_PAGE];
static lv_obj_t *settings_wifi_network_names[SETTINGS_WIFI_ROWS_PER_PAGE];
static lv_obj_t *settings_wifi_network_values[SETTINGS_WIFI_ROWS_PER_PAGE];
static lv_obj_t *settings_wifi_previous_button;
static lv_obj_t *settings_wifi_next_button;
static lv_obj_t *settings_wifi_page_label;
static lv_obj_t *settings_wifi_password_view;
static lv_obj_t *settings_wifi_password_input;
static lv_obj_t *settings_wifi_password_ssid_label;
static lv_obj_t *settings_wifi_keyboard;
static char settings_wifi_selected_ssid[COMM_WIFI_SSID_MAX_LEN + 1U];
#endif

static void settings_page_show_main(ui_page_t *page);
static void settings_page_show_detail(ui_page_t *page,
                                      settings_item_id_t item_id);
#if defined(FACTORY_HW_USING_KEYBOARD)
static void settings_page_update_wifi(void);
static void settings_page_hide_wifi_password(void);
#endif

static lv_obj_t *settings_page_create_label(lv_obj_t *parent,
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

static void settings_page_style_container(lv_obj_t *object)
{
    lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_radius(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_shadow_width(object, 0, 0);
}

static void settings_page_style_button(lv_obj_t *button,
                                       uint32_t normal_color)
{
    lv_obj_set_style_bg_color(button, lv_color_hex(normal_color), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_radius(button, 4, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    ui_theme_apply_focus_style(button);
    lv_obj_set_style_bg_color(button, lv_color_hex(UI_COLOR_TRACK),
                              LV_STATE_PRESSED);
}

static void settings_page_style_control(lv_obj_t *control)
{
    ui_theme_apply_focus_style(control);
}

static const char *settings_page_bool_text(bool enabled)
{
    return enabled ? "On" : "Off";
}

static bool settings_page_get_switch(settings_item_id_t item_id)
{
    switch (item_id)
    {
    case SETTINGS_ITEM_WIFI:
        return settings_state.wifi_enabled;
    case SETTINGS_ITEM_BLUETOOTH:
        return settings_state.bluetooth_enabled;
    case SETTINGS_ITEM_MOTOR:
        return settings_state.motor_enabled;
    case SETTINGS_ITEM_RGB_LED:
        return settings_state.rgb_enabled;
    default:
        return false;
    }
}

static void settings_page_set_switch(settings_item_id_t item_id,
                                     bool enabled)
{
    switch (item_id)
    {
    case SETTINGS_ITEM_WIFI:
        settings_state.wifi_enabled = enabled;
        break;
    case SETTINGS_ITEM_BLUETOOTH:
        settings_state.bluetooth_enabled = enabled;
        break;
    case SETTINGS_ITEM_MOTOR:
        settings_state.motor_enabled = enabled;
        break;
    case SETTINGS_ITEM_RGB_LED:
        settings_state.rgb_enabled = enabled;
        break;
    default:
        break;
    }
}

static uint8_t settings_page_get_slider(settings_item_id_t item_id)
{
    switch (item_id)
    {
    case SETTINGS_ITEM_BRIGHTNESS:
        return settings_state.brightness;
    case SETTINGS_ITEM_VOLUME:
        return settings_state.volume;
    case SETTINGS_ITEM_KEYBOARD_LED:
        return settings_state.keyboard_led_brightness;
    case SETTINGS_ITEM_RGB_LED:
        return settings_state.rgb_brightness;
    default:
        return 0U;
    }
}

static void settings_page_set_slider(settings_item_id_t item_id,
                                     uint8_t value)
{
    switch (item_id)
    {
    case SETTINGS_ITEM_BRIGHTNESS:
        settings_state.brightness = value;
        break;
    case SETTINGS_ITEM_VOLUME:
        settings_state.volume = value;
        break;
    case SETTINGS_ITEM_KEYBOARD_LED:
        settings_state.keyboard_led_brightness = value;
        break;
    case SETTINGS_ITEM_RGB_LED:
        settings_state.rgb_brightness = value;
        break;
    default:
        break;
    }
}

static void settings_page_load_state(void)
{
    bool enabled;
    uint8_t value;

    if (settings_service_get_bluetooth_enabled(&enabled) == RT_EOK)
        settings_state.bluetooth_enabled = enabled;
    if (settings_service_get_pan_enabled(&enabled) == RT_EOK)
        settings_state.bluetooth_pan_enabled = enabled;
    if (settings_service_get_brightness(&value) == RT_EOK)
        settings_state.brightness = value;
    if (settings_service_get_volume(&value) == RT_EOK)
        settings_state.volume = value;
    if (settings_service_get_menu_style(&value) == RT_EOK)
        settings_state.menu_style = value;
    if (settings_service_get_motor_enabled(&enabled) == RT_EOK)
        settings_state.motor_enabled = enabled;
    if (settings_service_get_motor_config(
            &settings_state.motor_amplitude,
            &settings_state.motor_effect) != RT_EOK)
    {
        settings_state.motor_amplitude = 70U;
        settings_state.motor_effect = 0U;
    }

#if defined(FACTORY_HW_USING_KEYBOARD)
    if (settings_service_get_wifi_status(&settings_wifi_status) == RT_EOK)
    {
        settings_state.wifi_enabled = settings_wifi_status.enabled;
    }
    else if (settings_service_get_wifi_enabled(&enabled) == RT_EOK)
    {
        settings_state.wifi_enabled = enabled;
    }
    if (settings_service_get_keyboard_led(&value) == RT_EOK)
        settings_state.keyboard_led_brightness = value;
#elif defined(FACTORY_HW_USING_JOYPAD)
    if (settings_service_get_rgb(&enabled, &value,
                                 &settings_state.rgb_color) == RT_EOK)
    {
        settings_state.rgb_enabled = enabled;
        settings_state.rgb_brightness = value;
    }
#endif
}

static rt_err_t settings_page_apply_switch(settings_item_id_t item_id,
                                           bool enabled)
{
    switch (item_id)
    {
    case SETTINGS_ITEM_WIFI:
        return settings_service_set_wifi_enabled(enabled);
    case SETTINGS_ITEM_BLUETOOTH:
        return settings_service_set_bluetooth_enabled(enabled);
    case SETTINGS_ITEM_MOTOR:
        return settings_service_set_motor_enabled(enabled);
    case SETTINGS_ITEM_RGB_LED:
        return settings_service_set_rgb(enabled,
                                        settings_state.rgb_brightness,
                                        settings_state.rgb_color);
    default:
        return -RT_EINVAL;
    }
}

static rt_err_t settings_page_apply_slider(settings_item_id_t item_id,
                                           uint8_t value)
{
    switch (item_id)
    {
    case SETTINGS_ITEM_BRIGHTNESS:
        return settings_service_set_brightness(value);
    case SETTINGS_ITEM_VOLUME:
        return settings_service_set_volume(value);
    case SETTINGS_ITEM_KEYBOARD_LED:
        return settings_service_set_keyboard_led(value);
    case SETTINGS_ITEM_RGB_LED:
        return settings_service_set_rgb(settings_state.rgb_enabled, value,
                                        settings_state.rgb_color);
    default:
        return -RT_EINVAL;
    }
}

static void settings_page_update_row_value(settings_item_id_t item_id)
{
    lv_obj_t *label;
    char value[32];

    if (item_id >= SETTINGS_ITEM_COUNT)
        return;
    label = settings_value_labels[item_id];
    if (label == RT_NULL)
        return;

    switch (settings_items[item_id].kind)
    {
    case SETTINGS_KIND_WIFI:
#if defined(FACTORY_HW_USING_KEYBOARD)
        if (settings_wifi_status.connected &&
                settings_wifi_status.ssid[0] != '\0')
            lv_label_set_text(label, settings_wifi_status.ssid);
        else
#endif
            lv_label_set_text(label, settings_page_bool_text(
                                         settings_state.wifi_enabled));
        break;
    case SETTINGS_KIND_SWITCH:
        lv_label_set_text(label, settings_page_bool_text(
                                     settings_page_get_switch(item_id)));
        break;
    case SETTINGS_KIND_SLIDER:
        rt_snprintf(value, sizeof(value), "%u%%",
                    (unsigned int)settings_page_get_slider(item_id));
        lv_label_set_text(label, value);
        break;
    case SETTINGS_KIND_MENU_STYLE:
        lv_label_set_text(label, settings_state.menu_style == 0U
                                     ? "Carousel"
                                     : "Honeycomb");
        break;
    case SETTINGS_KIND_RGB:
        rt_snprintf(value, sizeof(value), "%s / %s",
                    settings_page_bool_text(settings_state.rgb_enabled),
                    rgb_color_names[settings_state.rgb_color]);
        lv_label_set_text(label, value);
        break;
    case SETTINGS_KIND_ACTION:
    default:
        lv_label_set_text(label, "");
        break;
    }
}

static void settings_page_update_detail_value(settings_item_id_t item_id)
{
    char value[32];

    if (settings_items[item_id].kind == SETTINGS_KIND_SLIDER ||
        item_id == SETTINGS_ITEM_RGB_LED)
    {
        if (settings_detail_value_label != RT_NULL)
        {
            rt_snprintf(value, sizeof(value), "%u%%",
                        (unsigned int)settings_page_get_slider(item_id));
            lv_label_set_text(settings_detail_value_label, value);
        }
    }
    if (settings_items[item_id].kind == SETTINGS_KIND_SWITCH ||
        item_id == SETTINGS_ITEM_RGB_LED)
    {
        if (settings_detail_switch_value_label != RT_NULL)
        {
            lv_label_set_text(settings_detail_switch_value_label,
                              settings_page_bool_text(
                                  settings_page_get_switch(item_id)));
        }
    }
}

static void settings_page_back_clicked(lv_event_t *event)
{
    (void)event;
    if (settings_page_owner == RT_NULL)
        return;

#if defined(FACTORY_HW_USING_KEYBOARD)
    if (settings_wifi_password_view != RT_NULL &&
            !lv_obj_has_flag(settings_wifi_password_view,
                             LV_OBJ_FLAG_HIDDEN))
    {
        settings_page_hide_wifi_password();
        return;
    }
#endif
    if (settings_detail_view != RT_NULL)
        settings_page_show_main(settings_page_owner);
    else
        ui_navigation_switch_async(UI_PAGE_HOME);
}

static lv_obj_t *settings_page_create_header(lv_obj_t *parent,
                                             const char *title)
{
    lv_obj_t *back;
    lv_obj_t *back_icon;
    lv_obj_t *title_label;

    back = lv_button_create(parent);
    lv_obj_set_size(back, 48, 44);
    lv_obj_set_pos(back, 16, UI_STATUS_BAR_HEIGHT + 8);
    settings_page_style_button(back, UI_COLOR_BACKGROUND);
    lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(back, settings_page_back_clicked,
                        LV_EVENT_CLICKED, RT_NULL);

    back_icon = settings_page_create_label(back, LV_SYMBOL_LEFT,
                                           &lv_font_montserrat_24,
                                           UI_COLOR_TEXT);
    lv_obj_center(back_icon);

    title_label = settings_page_create_label(parent, title,
                                             &lv_font_montserrat_24,
                                             UI_COLOR_TEXT);
    lv_obj_set_size(title_label, 360, 34);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(title_label, 76, UI_STATUS_BAR_HEIGHT + 13);
    return back;
}

static lv_obj_t *settings_page_add_section(lv_obj_t *parent,
                                           const char *text)
{
    lv_obj_t *label = settings_page_create_label(parent, text,
                                                 &lv_font_montserrat_14,
                                                 UI_COLOR_MUTED);

    lv_obj_set_size(label, SETTINGS_CONTENT_WIDTH, 32);
    lv_obj_set_style_pad_left(label, 8, 0);
    lv_obj_set_style_pad_top(label, 10, 0);
    return label;
}

static void settings_page_row_clicked(lv_event_t *event)
{
    settings_item_id_t item_id =
        (settings_item_id_t)(uintptr_t)lv_event_get_user_data(event);

    if (settings_page_owner != RT_NULL && item_id < SETTINGS_ITEM_COUNT)
        settings_page_show_detail(settings_page_owner, item_id);
}

static lv_obj_t *settings_page_add_row(lv_obj_t *parent,
                                       settings_item_id_t item_id)
{
    const settings_item_definition_t *definition =
        &settings_items[item_id];
    lv_obj_t *row;
    lv_obj_t *icon;
    lv_obj_t *name;
    lv_obj_t *value;
    lv_obj_t *arrow;
    lv_obj_t *separator;

    row = lv_button_create(parent);
    lv_obj_set_size(row, SETTINGS_CONTENT_WIDTH, SETTINGS_ROW_HEIGHT);
    settings_page_style_button(row, UI_COLOR_BACKGROUND);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_add_flag(row, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_event_cb(row, settings_page_row_clicked,
                        LV_EVENT_CLICKED, (void *)(uintptr_t)item_id);

    icon = settings_page_create_label(row, definition->icon,
                                      &lv_font_montserrat_20,
                                      definition->color);
    lv_obj_set_size(icon, 30, 28);
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 8, 0);

    name = settings_page_create_label(row, definition->name,
                                      &lv_font_montserrat_18,
                                      UI_COLOR_TEXT);
    lv_obj_set_size(name, 190, 26);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 48, 0);

    value = settings_page_create_label(row, "",
                                       &lv_font_montserrat_16,
                                       UI_COLOR_MUTED);
    lv_obj_set_size(value, 154, 24);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
    lv_obj_align(value, LV_ALIGN_RIGHT_MID, -34, 0);

    arrow = settings_page_create_label(row, LV_SYMBOL_RIGHT,
                                       &lv_font_montserrat_16,
                                       UI_COLOR_MUTED);
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -8, 0);

    separator = lv_obj_create(row);
    lv_obj_set_size(separator, SETTINGS_CONTENT_WIDTH - 48, 1);
    lv_obj_align(separator, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(separator, lv_color_hex(UI_COLOR_TRACK), 0);
    lv_obj_set_style_bg_opa(separator, LV_OPA_60, 0);
    lv_obj_set_style_border_width(separator, 0, 0);
    lv_obj_set_style_pad_all(separator, 0, 0);
    lv_obj_remove_flag(separator, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(separator, LV_OBJ_FLAG_SCROLLABLE);

    settings_rows[item_id] = row;
    if (settings_focus_row_count < SETTINGS_ITEM_COUNT)
    {
        settings_focus_rows[settings_focus_row_count] = row;
        settings_focus_row_count++;
    }
    settings_value_labels[item_id] = value;
    settings_page_update_row_value(item_id);
    return row;
}

static void settings_page_add_main_group_objects(ui_page_t *page)
{
    uint32_t index;
    lv_obj_t *focus = RT_NULL;

    if (page == RT_NULL || page->group == RT_NULL)
        return;

    lv_group_remove_all_objs(page->group);
    if (settings_main_back != RT_NULL)
        lv_group_add_obj(page->group, settings_main_back);

    for (index = 0U; index < settings_focus_row_count; index++)
    {
        lv_obj_t *row = settings_focus_rows[index];

        if (row == RT_NULL)
            continue;
        lv_group_add_obj(page->group, row);
        if (row == settings_rows[settings_active_item])
            focus = row;
    }

    if (focus == RT_NULL && settings_focus_row_count > 0U)
        focus = settings_focus_rows[0];
    if (focus != RT_NULL)
        lv_group_focus_obj(focus);
}

static void settings_page_switch_changed(lv_event_t *event)
{
    settings_item_id_t item_id =
        (settings_item_id_t)(uintptr_t)lv_event_get_user_data(event);
    lv_obj_t *control = lv_event_get_target(event);
    bool old_value = settings_page_get_switch(item_id);
    bool new_value = lv_obj_has_state(control, LV_STATE_CHECKED);
    rt_err_t result;

    result = settings_page_apply_switch(item_id, new_value);
    if (result != RT_EOK)
    {
        if (old_value)
            lv_obj_add_state(control, LV_STATE_CHECKED);
        else
            lv_obj_clear_state(control, LV_STATE_CHECKED);
        LOG_E("Apply %s switch failed: %d",
              settings_items[item_id].name, result);
    }
    else
    {
        settings_page_set_switch(item_id, new_value);
#if defined(FACTORY_HW_USING_KEYBOARD)
        if (item_id == SETTINGS_ITEM_WIFI)
        {
            settings_wifi_status.enabled = new_value;
            settings_wifi_auto_scan_pending = new_value;
            if (!new_value)
            {
                settings_wifi_network_count = 0U;
                settings_wifi_page = 0U;
            }
            settings_page_update_wifi();
        }
#endif
        if (item_id == SETTINGS_ITEM_MOTOR && new_value)
        {
            (void)settings_service_preview_motor(
                settings_state.motor_amplitude,
                settings_state.motor_effect);
        }
        if (item_id == SETTINGS_ITEM_BLUETOOTH && !new_value)
        {
            settings_state.bluetooth_pan_enabled = false;
            if (settings_pan_control != RT_NULL)
                lv_obj_clear_state(settings_pan_control,
                                   LV_STATE_CHECKED);
            if (settings_pan_value_label != RT_NULL)
                lv_label_set_text(settings_pan_value_label, "Off");
        }
    }
    settings_page_update_row_value(item_id);
    settings_page_update_detail_value(item_id);
}

static void settings_page_commit_slider(settings_item_id_t item_id,
                                         lv_obj_t *slider,
                                         uint8_t new_value)
{
    uint8_t old_value;
    rt_err_t result;

    if (item_id == SETTINGS_ITEM_BRIGHTNESS &&
            new_value < SETTINGS_BRIGHTNESS_MIN)
    {
        new_value = SETTINGS_BRIGHTNESS_MIN;
        lv_slider_set_value(slider, new_value, LV_ANIM_OFF);
    }
    old_value = settings_page_get_slider(item_id);
    result = settings_page_apply_slider(item_id, new_value);

    if (result != RT_EOK)
    {
        lv_slider_set_value(slider, old_value, LV_ANIM_OFF);
        LOG_E("Apply %s level failed: %d",
              settings_items[item_id].name, result);
    }
    else
    {
        settings_page_set_slider(item_id, new_value);
    }
    settings_page_update_row_value(item_id);
    settings_page_update_detail_value(item_id);
}

static void settings_page_slider_changed(lv_event_t *event)
{
    settings_item_id_t item_id =
        (settings_item_id_t)(uintptr_t)lv_event_get_user_data(event);
    lv_obj_t *slider = lv_event_get_target(event);

    settings_page_commit_slider(item_id, slider,
                                (uint8_t)lv_slider_get_value(slider));
}

static void settings_page_slider_step_clicked(lv_event_t *event)
{
    bool increment =
        (bool)(uintptr_t)lv_event_get_user_data(event);
    int32_t minimum = settings_active_slider_item == SETTINGS_ITEM_BRIGHTNESS
                          ? SETTINGS_BRIGHTNESS_MIN
                          : 0;
    int32_t value;

    if (settings_active_slider == RT_NULL)
        return;
    value = lv_slider_get_value(settings_active_slider);
    value += increment ? 5 : -5;
    if (value < minimum)
        value = minimum;
    else if (value > 100)
        value = 100;
    lv_slider_set_value(settings_active_slider, value, LV_ANIM_ON);
    settings_page_commit_slider(settings_active_slider_item,
                                settings_active_slider,
                                (uint8_t)value);
}

static lv_obj_t *settings_page_add_switch(lv_obj_t *parent,
                                          settings_item_id_t item_id)
{
    lv_obj_t *panel;
    lv_obj_t *label;
    lv_obj_t *control;

    panel = lv_obj_create(parent);
    lv_obj_set_size(panel, SETTINGS_CONTENT_WIDTH, 96);
    lv_obj_set_pos(panel, SETTINGS_SIDE_PADDING, 28);
    lv_obj_set_style_bg_color(panel, lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, 4, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    label = settings_page_create_label(panel, "Status",
                                       &lv_font_montserrat_18,
                                       UI_COLOR_TEXT);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 20, -16);

    settings_detail_switch_value_label = settings_page_create_label(
        panel, "", &lv_font_montserrat_14, UI_COLOR_MUTED);
    lv_obj_align(settings_detail_switch_value_label,
                 LV_ALIGN_LEFT_MID, 20, 15);

    control = lv_switch_create(panel);
    lv_obj_set_size(control, 76, 38);
    lv_obj_align(control, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_set_style_bg_color(control, lv_color_hex(UI_COLOR_TRACK),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_color(control, lv_color_hex(UI_COLOR_PRIMARY),
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    settings_page_style_control(control);
    if (settings_page_get_switch(item_id))
        lv_obj_add_state(control, LV_STATE_CHECKED);
    lv_obj_add_event_cb(control, settings_page_switch_changed,
                        LV_EVENT_VALUE_CHANGED,
                        (void *)(uintptr_t)item_id);
    settings_page_update_detail_value(item_id);
    return control;
}

static void settings_page_pan_changed(lv_event_t *event)
{
    lv_obj_t *control = lv_event_get_target(event);
    bool old_value = settings_state.bluetooth_pan_enabled;
    bool new_value = lv_obj_has_state(control, LV_STATE_CHECKED);
    rt_err_t result;

    result = settings_service_set_pan_enabled(new_value);
    if (result == RT_EOK)
    {
        settings_state.bluetooth_pan_enabled = new_value;
    }
    else
    {
        if (old_value)
            lv_obj_add_state(control, LV_STATE_CHECKED);
        else
            lv_obj_clear_state(control, LV_STATE_CHECKED);
        LOG_E("Apply Bluetooth PAN switch failed: %d", result);
    }
    settings_page_update_pan_status();
}

static void settings_page_update_pan_status(void)
{
    char ipv4_address[16];
    char text[48];
    bool connected = false;
    uint32_t color = UI_COLOR_MUTED;

    if (settings_pan_value_label == RT_NULL)
        return;
    (void)settings_service_get_pan_enabled(
        &settings_state.bluetooth_pan_enabled);
    if (!settings_state.bluetooth_pan_enabled)
    {
        rt_snprintf(text, sizeof(text), "Off");
    }
    else if (settings_service_get_pan_info(
                 &connected, ipv4_address, sizeof(ipv4_address)) != RT_EOK ||
             !connected)
    {
        rt_snprintf(text, sizeof(text), "Connecting");
        color = UI_COLOR_WARNING;
    }
    else if (strcmp(ipv4_address, "--") == 0)
    {
        rt_snprintf(text, sizeof(text), "Connected / DHCP...");
        color = UI_COLOR_WARNING;
    }
    else
    {
        rt_snprintf(text, sizeof(text), "Connected / %s", ipv4_address);
        color = UI_COLOR_SUCCESS;
    }

    if (strcmp(lv_label_get_text(settings_pan_value_label), text) != 0)
        lv_label_set_text(settings_pan_value_label, text);
    lv_obj_set_style_text_color(settings_pan_value_label,
                                lv_color_hex(color), 0);
}

static lv_obj_t *settings_page_add_pan_switch(lv_obj_t *parent)
{
    lv_obj_t *panel;
    lv_obj_t *label;
    lv_obj_t *control;

    panel = lv_obj_create(parent);
    lv_obj_set_size(panel, SETTINGS_CONTENT_WIDTH, 96);
    lv_obj_set_pos(panel, SETTINGS_SIDE_PADDING, 140);
    lv_obj_set_style_bg_color(panel, lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, 4, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    label = settings_page_create_label(panel, "PAN",
                                       &lv_font_montserrat_18,
                                       UI_COLOR_TEXT);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 20, -16);

    settings_pan_value_label = settings_page_create_label(
        panel, "Off",
        &lv_font_montserrat_14, UI_COLOR_MUTED);
    lv_obj_set_size(settings_pan_value_label, 290, 22);
    lv_label_set_long_mode(settings_pan_value_label, LV_LABEL_LONG_DOT);
    lv_obj_align(settings_pan_value_label, LV_ALIGN_LEFT_MID, 20, 15);

    control = lv_switch_create(panel);
    lv_obj_set_size(control, 76, 38);
    lv_obj_align(control, LV_ALIGN_RIGHT_MID, -20, 0);
    lv_obj_set_style_bg_color(control, lv_color_hex(UI_COLOR_TRACK),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_color(control, lv_color_hex(UI_COLOR_PRIMARY),
                              LV_PART_INDICATOR | LV_STATE_CHECKED);
    settings_page_style_control(control);
    if (settings_state.bluetooth_pan_enabled)
        lv_obj_add_state(control, LV_STATE_CHECKED);
    lv_obj_add_event_cb(control, settings_page_pan_changed,
                        LV_EVENT_VALUE_CHANGED, RT_NULL);
    settings_pan_control = control;
    settings_page_update_pan_status();
    return control;
}

#if defined(FACTORY_HW_USING_KEYBOARD)
static uint8_t settings_page_wifi_page_count(void)
{
    uint8_t count = (uint8_t)((settings_wifi_network_count +
                               SETTINGS_WIFI_ROWS_PER_PAGE - 1U) /
                              SETTINGS_WIFI_ROWS_PER_PAGE);

    return count == 0U ? 1U : count;
}

static void settings_page_add_wifi_group_objects(void)
{
    uint32_t row;
    size_t first = (size_t)settings_wifi_page *
                   SETTINGS_WIFI_ROWS_PER_PAGE;
    lv_obj_t *focused;
    bool restore_focus = false;

    if (settings_page_owner == RT_NULL ||
            settings_page_owner->group == RT_NULL)
        return;
    focused = lv_group_get_focused(settings_page_owner->group);
    lv_group_remove_all_objs(settings_page_owner->group);
    if (settings_detail_back != RT_NULL)
    {
        lv_group_add_obj(settings_page_owner->group, settings_detail_back);
        restore_focus = focused == settings_detail_back;
    }
    if (settings_wifi_switch != RT_NULL)
    {
        lv_group_add_obj(settings_page_owner->group, settings_wifi_switch);
        restore_focus = restore_focus || focused == settings_wifi_switch;
    }
    if (settings_wifi_scan_button != RT_NULL)
    {
        lv_group_add_obj(settings_page_owner->group,
                         settings_wifi_scan_button);
        restore_focus = restore_focus ||
                        focused == settings_wifi_scan_button;
    }
    for (row = 0U; row < SETTINGS_WIFI_ROWS_PER_PAGE; row++)
    {
        if (first + row >= settings_wifi_network_count ||
                settings_wifi_network_buttons[row] == RT_NULL)
            continue;
        lv_group_add_obj(settings_page_owner->group,
                         settings_wifi_network_buttons[row]);
        restore_focus = restore_focus ||
                        focused == settings_wifi_network_buttons[row];
    }
    if (settings_wifi_previous_button != RT_NULL)
    {
        lv_group_add_obj(settings_page_owner->group,
                         settings_wifi_previous_button);
        restore_focus = restore_focus ||
                        focused == settings_wifi_previous_button;
    }
    if (settings_wifi_next_button != RT_NULL)
    {
        lv_group_add_obj(settings_page_owner->group,
                         settings_wifi_next_button);
        restore_focus = restore_focus ||
                        focused == settings_wifi_next_button;
    }
    if (restore_focus)
        lv_group_focus_obj(focused);
    else if (settings_wifi_scan_button != RT_NULL)
        lv_group_focus_obj(settings_wifi_scan_button);
}

static void settings_page_update_wifi_rows(void)
{
    uint32_t row;
    uint8_t page_count = settings_page_wifi_page_count();
    size_t first;

    if (settings_wifi_page >= page_count)
        settings_wifi_page = (uint8_t)(page_count - 1U);
    first = (size_t)settings_wifi_page * SETTINGS_WIFI_ROWS_PER_PAGE;
    for (row = 0U; row < SETTINGS_WIFI_ROWS_PER_PAGE; row++)
    {
        lv_obj_t *button = settings_wifi_network_buttons[row];
        size_t index = first + row;

        if (button == RT_NULL)
            continue;
        if (index >= settings_wifi_network_count)
        {
            lv_obj_add_flag(button, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(button, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(settings_wifi_network_names[row],
                          settings_wifi_networks[index].ssid);
        lv_label_set_text_fmt(
            settings_wifi_network_values[row], "%d dBm",
            (int)settings_wifi_networks[index].rssi);
        if (settings_wifi_status.connected &&
                strcmp(settings_wifi_status.ssid,
                       settings_wifi_networks[index].ssid) == 0)
            lv_obj_add_state(button, LV_STATE_CHECKED);
        else
            lv_obj_clear_state(button, LV_STATE_CHECKED);
    }
    if (settings_wifi_page_label != RT_NULL)
    {
        lv_label_set_text_fmt(settings_wifi_page_label, "%u / %u",
                              (unsigned int)settings_wifi_page + 1U,
                              (unsigned int)page_count);
    }
    if (settings_wifi_previous_button != RT_NULL)
    {
        if (settings_wifi_page == 0U)
            lv_obj_add_state(settings_wifi_previous_button,
                             LV_STATE_DISABLED);
        else
            lv_obj_clear_state(settings_wifi_previous_button,
                               LV_STATE_DISABLED);
    }
    if (settings_wifi_next_button != RT_NULL)
    {
        if (settings_wifi_page + 1U >= page_count)
            lv_obj_add_state(settings_wifi_next_button,
                             LV_STATE_DISABLED);
        else
            lv_obj_clear_state(settings_wifi_next_button,
                               LV_STATE_DISABLED);
    }
}

static void settings_page_update_wifi(void)
{
    communication_wifi_status_t status;
    size_t count = 0U;
    bool changed = false;

    if (settings_service_get_wifi_status(&status) != RT_EOK)
        return;
    if (status.revision != settings_wifi_revision)
    {
        changed = true;
        settings_wifi_status = status;
        settings_wifi_revision = status.revision;
        settings_state.wifi_enabled = status.enabled;
        if (settings_service_get_wifi_networks(
                settings_wifi_networks, COMM_WIFI_MAX_NETWORKS,
                &count) == RT_EOK)
            settings_wifi_network_count = count;
    }

    if (settings_wifi_switch != RT_NULL)
    {
        if (settings_wifi_status.enabled)
            lv_obj_add_state(settings_wifi_switch, LV_STATE_CHECKED);
        else
            lv_obj_clear_state(settings_wifi_switch, LV_STATE_CHECKED);
    }
    if (settings_wifi_status_label != RT_NULL)
    {
        if (settings_wifi_status.connecting)
            lv_label_set_text_fmt(settings_wifi_status_label,
                                  "Connecting: %s",
                                  settings_wifi_selected_ssid);
        else if (settings_wifi_status.connected)
            lv_label_set_text(settings_wifi_status_label,
                              settings_wifi_status.ssid);
        else
            lv_label_set_text(settings_wifi_status_label,
                              settings_wifi_status.enabled ?
                                  "Not connected" : "Wi-Fi off");
    }
    if (settings_wifi_rssi_label != RT_NULL)
    {
        if (settings_wifi_status.connected)
            lv_label_set_text_fmt(settings_wifi_rssi_label,
                                  "RSSI %d dBm",
                                  (int)settings_wifi_status.rssi);
        else
            lv_label_set_text(settings_wifi_rssi_label, "RSSI -- dBm");
    }
    if (settings_wifi_scan_label != RT_NULL)
    {
        if (settings_wifi_status.scanning)
            lv_label_set_text(settings_wifi_scan_label, "Scanning...");
        else
            lv_label_set_text_fmt(settings_wifi_scan_label,
                                  "%u networks",
                                  (unsigned int)settings_wifi_network_count);
    }
    settings_page_update_wifi_rows();
    if (changed && (settings_wifi_password_view == RT_NULL ||
            lv_obj_has_flag(settings_wifi_password_view,
                            LV_OBJ_FLAG_HIDDEN)))
        settings_page_add_wifi_group_objects();
    settings_page_update_row_value(SETTINGS_ITEM_WIFI);
}

static void settings_page_wifi_scan_clicked(lv_event_t *event)
{
    rt_err_t result;

    (void)event;
    result = settings_service_scan_wifi();
    if (result == RT_EOK)
    {
        settings_wifi_auto_scan_pending = false;
        settings_wifi_page = 0U;
        if (settings_wifi_scan_label != RT_NULL)
            lv_label_set_text(settings_wifi_scan_label, "Scanning...");
    }
    else if (settings_wifi_scan_label != RT_NULL)
    {
        lv_label_set_text(settings_wifi_scan_label,
                          result == -RT_EBUSY ? "Wi-Fi busy" :
                                                "Scan failed");
    }
}

static void settings_page_wifi_page_clicked(lv_event_t *event)
{
    bool next = (bool)(uintptr_t)lv_event_get_user_data(event);
    uint8_t page_count = settings_page_wifi_page_count();

    if (next && settings_wifi_page + 1U < page_count)
        settings_wifi_page++;
    else if (!next && settings_wifi_page > 0U)
        settings_wifi_page--;
    settings_page_update_wifi_rows();
    settings_page_add_wifi_group_objects();
}

static void settings_page_wifi_keyboard_event(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);

    if (code == LV_EVENT_CANCEL)
    {
        settings_page_hide_wifi_password();
    }
    else if (code == LV_EVENT_READY)
    {
        const char *password = lv_textarea_get_text(
            settings_wifi_password_input);
        rt_err_t result = settings_service_connect_wifi(
            settings_wifi_selected_ssid, password);

        settings_page_hide_wifi_password();
        if (settings_wifi_scan_label != RT_NULL)
            lv_label_set_text(settings_wifi_scan_label,
                              result == RT_EOK ? "Connecting..." :
                              (result == -RT_EBUSY ? "Wi-Fi busy" :
                                                    "Connect failed"));
    }
}

static void settings_page_create_wifi_password(void)
{
    lv_obj_t *caption;

    if (settings_detail_view == RT_NULL ||
            settings_wifi_password_view != RT_NULL)
        return;
    settings_wifi_password_view = lv_obj_create(settings_detail_view);
    lv_obj_set_size(settings_wifi_password_view, UI_SCREEN_WIDTH,
                    UI_SCREEN_HEIGHT);
    lv_obj_set_pos(settings_wifi_password_view, 0, 0);
    lv_obj_set_style_bg_color(settings_wifi_password_view,
                              lv_color_hex(UI_COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(settings_wifi_password_view, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(settings_wifi_password_view, 0, 0);
    lv_obj_set_style_radius(settings_wifi_password_view, 0, 0);
    lv_obj_set_style_pad_all(settings_wifi_password_view, 0, 0);
    lv_obj_remove_flag(settings_wifi_password_view, LV_OBJ_FLAG_SCROLLABLE);

    caption = settings_page_create_label(settings_wifi_password_view,
                                         "WI-FI PASSWORD",
                                         &lv_font_montserrat_14,
                                         UI_COLOR_MUTED);
    lv_obj_set_pos(caption, SETTINGS_SIDE_PADDING, 330);
    settings_wifi_password_ssid_label = settings_page_create_label(
        settings_wifi_password_view, "", &lv_font_montserrat_20,
        UI_COLOR_TEXT);
    lv_obj_set_size(settings_wifi_password_ssid_label,
                    SETTINGS_CONTENT_WIDTH, 30);
    lv_label_set_long_mode(settings_wifi_password_ssid_label,
                           LV_LABEL_LONG_DOT);
    lv_obj_set_pos(settings_wifi_password_ssid_label,
                   SETTINGS_SIDE_PADDING, 354);

    settings_wifi_password_input = lv_textarea_create(
        settings_wifi_password_view);
    lv_obj_set_size(settings_wifi_password_input,
                    UI_SCREEN_WIDTH - 32, 42);
    lv_obj_set_pos(settings_wifi_password_input,
                   16, 78);
    lv_textarea_set_one_line(settings_wifi_password_input, true);
    lv_textarea_set_password_mode(settings_wifi_password_input, true);
    lv_textarea_set_max_length(settings_wifi_password_input, 64U);
    lv_textarea_set_placeholder_text(settings_wifi_password_input,
                                     "Password");
    lv_obj_set_style_bg_color(settings_wifi_password_input,
                              lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(settings_wifi_password_input,
                            LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(settings_wifi_password_input,
                                  lv_color_hex(UI_COLOR_TRACK), 0);
    lv_obj_set_style_border_width(settings_wifi_password_input, 1, 0);
    lv_obj_set_style_radius(settings_wifi_password_input, 4, 0);
    lv_obj_set_style_text_color(settings_wifi_password_input,
                                lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(settings_wifi_password_input,
                               &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_opa(settings_wifi_password_input,
                              LV_OPA_COVER, 0);
    lv_obj_set_style_text_letter_space(settings_wifi_password_input,
                                       0, 0);
    lv_obj_set_style_pad_top(settings_wifi_password_input, 8, 0);
    lv_obj_set_style_pad_bottom(settings_wifi_password_input, 8, 0);
    lv_obj_set_scrollbar_mode(settings_wifi_password_input,
                              LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(settings_wifi_password_input, LV_DIR_HOR);
    lv_obj_remove_flag(settings_wifi_password_input,
                       LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_remove_flag(settings_wifi_password_input,
                       LV_OBJ_FLAG_SCROLL_MOMENTUM);
    ui_theme_apply_focus_style(settings_wifi_password_input);

    settings_wifi_keyboard = lv_keyboard_create(
        settings_wifi_password_view);
    lv_obj_set_size(settings_wifi_keyboard, UI_SCREEN_WIDTH,
                    SETTINGS_WIFI_KEYBOARD_HEIGHT);
    lv_obj_set_pos(settings_wifi_keyboard, 0,
                   SETTINGS_WIFI_KEYBOARD_Y);
    lv_obj_set_style_bg_color(settings_wifi_keyboard,
                              lv_color_hex(UI_COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(settings_wifi_keyboard, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(settings_wifi_keyboard, 4, 0);
    lv_obj_set_style_pad_row(settings_wifi_keyboard, 4, 0);
    lv_obj_set_style_pad_column(settings_wifi_keyboard, 3, 0);
    lv_obj_set_style_bg_color(settings_wifi_keyboard,
                              lv_color_hex(UI_COLOR_PANEL), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(settings_wifi_keyboard, LV_OPA_COVER,
                            LV_PART_ITEMS);
    lv_obj_set_style_border_color(settings_wifi_keyboard,
                                  lv_color_hex(UI_COLOR_TRACK),
                                  LV_PART_ITEMS);
    lv_obj_set_style_border_width(settings_wifi_keyboard, 1,
                                  LV_PART_ITEMS);
    lv_obj_set_style_bg_color(settings_wifi_keyboard,
                              lv_color_hex(UI_COLOR_PRIMARY),
                              LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_radius(settings_wifi_keyboard, 3, LV_PART_ITEMS);
    lv_obj_set_style_text_color(settings_wifi_keyboard,
                                lv_color_hex(UI_COLOR_TEXT),
                                LV_PART_ITEMS);
    lv_obj_set_style_text_font(settings_wifi_keyboard,
                               &lv_font_montserrat_16, LV_PART_ITEMS);
    lv_obj_set_style_text_color(settings_wifi_keyboard,
                                lv_color_hex(UI_COLOR_BACKGROUND),
                                LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(settings_wifi_keyboard,
                              lv_color_hex(UI_COLOR_SECONDARY),
                              LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(settings_wifi_keyboard,
                                lv_color_hex(UI_COLOR_BACKGROUND),
                                LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_keyboard_set_mode(settings_wifi_keyboard,
                         LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(settings_wifi_keyboard,
                             settings_wifi_password_input);
    lv_obj_add_event_cb(settings_wifi_keyboard,
                        settings_page_wifi_keyboard_event,
                        LV_EVENT_ALL, RT_NULL);
    lv_obj_add_flag(settings_wifi_password_view, LV_OBJ_FLAG_HIDDEN);
}

static void settings_page_show_wifi_password(const char *ssid)
{
    settings_page_create_wifi_password();
    if (settings_wifi_password_view == RT_NULL)
        return;
    rt_strncpy(settings_wifi_selected_ssid, ssid,
               sizeof(settings_wifi_selected_ssid));
    lv_label_set_text(settings_wifi_password_ssid_label,
                      settings_wifi_selected_ssid);
    lv_textarea_set_text(settings_wifi_password_input, "");
    if (settings_detail_body != RT_NULL)
        lv_obj_add_flag(settings_detail_body, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(settings_wifi_password_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(settings_wifi_password_view);
    lv_obj_move_foreground(settings_wifi_keyboard);
    lv_obj_move_foreground(settings_wifi_password_input);
    if (settings_page_owner != RT_NULL &&
            settings_page_owner->group != RT_NULL)
    {
        lv_group_remove_all_objs(settings_page_owner->group);
        lv_group_add_obj(settings_page_owner->group,
                         settings_wifi_keyboard);
        lv_group_focus_obj(settings_wifi_keyboard);
        lv_group_set_editing(settings_page_owner->group, true);
    }
}

static void settings_page_hide_wifi_password(void)
{
    if (settings_wifi_password_view == RT_NULL ||
            lv_obj_has_flag(settings_wifi_password_view,
                            LV_OBJ_FLAG_HIDDEN))
        return;
    lv_obj_add_flag(settings_wifi_password_view, LV_OBJ_FLAG_HIDDEN);
    if (settings_detail_body != RT_NULL)
        lv_obj_remove_flag(settings_detail_body, LV_OBJ_FLAG_HIDDEN);
    if (settings_page_owner != RT_NULL &&
            settings_page_owner->group != RT_NULL)
        lv_group_set_editing(settings_page_owner->group, false);
    settings_page_add_wifi_group_objects();
    if (settings_wifi_scan_button != RT_NULL)
        lv_group_focus_obj(settings_wifi_scan_button);
}

static void settings_page_wifi_network_clicked(lv_event_t *event)
{
    uint32_t row = (uint32_t)(uintptr_t)lv_event_get_user_data(event);
    size_t index = (size_t)settings_wifi_page *
                   SETTINGS_WIFI_ROWS_PER_PAGE + row;
    rt_err_t result;

    if (row >= SETTINGS_WIFI_ROWS_PER_PAGE ||
            index >= settings_wifi_network_count)
        return;
    if (settings_wifi_networks[index].encryption != 0U)
    {
        settings_page_show_wifi_password(
            settings_wifi_networks[index].ssid);
        return;
    }
    rt_strncpy(settings_wifi_selected_ssid,
               settings_wifi_networks[index].ssid,
               sizeof(settings_wifi_selected_ssid));
    result = settings_service_connect_wifi(settings_wifi_selected_ssid, "");
    if (settings_wifi_scan_label != RT_NULL)
        lv_label_set_text(settings_wifi_scan_label,
                          result == RT_EOK ? "Connecting..." :
                          (result == -RT_EBUSY ? "Wi-Fi busy" :
                                                "Connect failed"));
}

static lv_obj_t *settings_page_add_wifi(lv_obj_t *parent,
                                        ui_page_t *page)
{
    lv_obj_t *panel;
    lv_obj_t *label;
    uint32_t row;

    panel = lv_obj_create(parent);
    lv_obj_set_size(panel, SETTINGS_CONTENT_WIDTH, 84);
    lv_obj_set_pos(panel, SETTINGS_SIDE_PADDING, 16);
    lv_obj_set_style_bg_color(panel, lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, 4, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    settings_wifi_status_label = settings_page_create_label(
        panel, "Not connected", &lv_font_montserrat_18, UI_COLOR_TEXT);
    lv_obj_set_size(settings_wifi_status_label, 286, 26);
    lv_label_set_long_mode(settings_wifi_status_label, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(settings_wifi_status_label, 18, 18);
    settings_wifi_rssi_label = settings_page_create_label(
        panel, "RSSI -- dBm", &lv_font_montserrat_14, UI_COLOR_MUTED);
    lv_obj_set_pos(settings_wifi_rssi_label, 18, 49);
    settings_wifi_switch = lv_switch_create(panel);
    lv_obj_set_size(settings_wifi_switch, 76, 38);
    lv_obj_align(settings_wifi_switch, LV_ALIGN_RIGHT_MID, -18, 0);
    settings_page_style_control(settings_wifi_switch);
    if (settings_state.wifi_enabled)
        lv_obj_add_state(settings_wifi_switch, LV_STATE_CHECKED);
    lv_obj_add_event_cb(settings_wifi_switch,
                        settings_page_switch_changed,
                        LV_EVENT_VALUE_CHANGED,
                        (void *)(uintptr_t)SETTINGS_ITEM_WIFI);

    settings_wifi_scan_button = lv_button_create(parent);
    lv_obj_set_size(settings_wifi_scan_button, 132, 46);
    lv_obj_set_pos(settings_wifi_scan_button, SETTINGS_SIDE_PADDING, 112);
    settings_page_style_button(settings_wifi_scan_button, UI_COLOR_PRIMARY);
    lv_obj_add_event_cb(settings_wifi_scan_button,
                        settings_page_wifi_scan_clicked,
                        LV_EVENT_CLICKED, RT_NULL);
    label = settings_page_create_label(settings_wifi_scan_button,
                                       LV_SYMBOL_REFRESH "  Scan",
                                       &lv_font_montserrat_16,
                                       UI_COLOR_BACKGROUND);
    lv_obj_center(label);
    settings_wifi_scan_label = settings_page_create_label(
        parent, "0 networks", &lv_font_montserrat_14, UI_COLOR_MUTED);
    lv_obj_set_pos(settings_wifi_scan_label, 174, 126);

    for (row = 0U; row < SETTINGS_WIFI_ROWS_PER_PAGE; row++)
    {
        lv_obj_t *button = lv_button_create(parent);

        lv_obj_set_size(button, SETTINGS_CONTENT_WIDTH, 54);
        lv_obj_set_pos(button, SETTINGS_SIDE_PADDING,
                       172 + (int32_t)row * 58);
        settings_page_style_button(button, UI_COLOR_PANEL);
        lv_obj_set_style_bg_color(button,
                                  lv_color_hex(UI_COLOR_PRIMARY),
                                  LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(button, LV_OPA_30,
                                LV_STATE_CHECKED);
        lv_obj_add_flag(button, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        lv_obj_add_event_cb(button,
                            settings_page_wifi_network_clicked,
                            LV_EVENT_CLICKED,
                            (void *)(uintptr_t)row);
        settings_wifi_network_names[row] = settings_page_create_label(
            button, "", &lv_font_montserrat_16, UI_COLOR_TEXT);
        lv_obj_set_size(settings_wifi_network_names[row], 255, 24);
        lv_label_set_long_mode(settings_wifi_network_names[row],
                               LV_LABEL_LONG_DOT);
        lv_obj_align(settings_wifi_network_names[row],
                     LV_ALIGN_LEFT_MID, 14, 0);
        settings_wifi_network_values[row] = settings_page_create_label(
            button, "", &lv_font_montserrat_14, UI_COLOR_MUTED);
        lv_obj_set_size(settings_wifi_network_values[row], 140, 22);
        lv_obj_set_style_text_align(settings_wifi_network_values[row],
                                    LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(settings_wifi_network_values[row],
                     LV_ALIGN_RIGHT_MID, -14, 0);
        settings_wifi_network_buttons[row] = button;
        lv_obj_add_flag(button, LV_OBJ_FLAG_HIDDEN);
    }

    settings_wifi_previous_button = lv_button_create(parent);
    lv_obj_set_size(settings_wifi_previous_button, 64, 42);
    lv_obj_set_pos(settings_wifi_previous_button,
                   SETTINGS_SIDE_PADDING, 532);
    settings_page_style_button(settings_wifi_previous_button,
                               UI_COLOR_PANEL);
    lv_obj_add_event_cb(settings_wifi_previous_button,
                        settings_page_wifi_page_clicked,
                        LV_EVENT_CLICKED, (void *)(uintptr_t)false);
    label = settings_page_create_label(settings_wifi_previous_button,
                                       LV_SYMBOL_LEFT,
                                       &lv_font_montserrat_18,
                                       UI_COLOR_TEXT);
    lv_obj_center(label);

    settings_wifi_next_button = lv_button_create(parent);
    lv_obj_set_size(settings_wifi_next_button, 64, 42);
    lv_obj_set_pos(settings_wifi_next_button,
                   UI_SCREEN_WIDTH - SETTINGS_SIDE_PADDING - 64, 532);
    settings_page_style_button(settings_wifi_next_button,
                               UI_COLOR_PANEL);
    lv_obj_add_event_cb(settings_wifi_next_button,
                        settings_page_wifi_page_clicked,
                        LV_EVENT_CLICKED, (void *)(uintptr_t)true);
    label = settings_page_create_label(settings_wifi_next_button,
                                       LV_SYMBOL_RIGHT,
                                       &lv_font_montserrat_18,
                                       UI_COLOR_TEXT);
    lv_obj_center(label);
    settings_wifi_page_label = settings_page_create_label(
        parent, "1 / 1", &lv_font_montserrat_14, UI_COLOR_MUTED);
    lv_obj_set_size(settings_wifi_page_label, 120, 24);
    lv_obj_set_style_text_align(settings_wifi_page_label,
                                LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(settings_wifi_page_label,
                   (UI_SCREEN_WIDTH - 120) / 2, 542);

    (void)page;
    settings_page_update_wifi();
    settings_page_add_wifi_group_objects();
    return settings_wifi_scan_button;
}
#endif

static lv_obj_t *settings_page_add_slider(lv_obj_t *parent,
                                          settings_item_id_t item_id,
                                          int32_t y,
                                          ui_page_t *page)
{
    lv_obj_t *caption;
    lv_obj_t *slider;
    lv_obj_t *decrement;
    lv_obj_t *decrement_label;
    lv_obj_t *increment;
    lv_obj_t *increment_label;

    caption = settings_page_create_label(parent, "Level",
                                         &lv_font_montserrat_16,
                                         UI_COLOR_MUTED);
    lv_obj_set_pos(caption, SETTINGS_SIDE_PADDING, y);

    settings_detail_value_label = settings_page_create_label(
        parent, "", &lv_font_montserrat_34, UI_COLOR_TEXT);
    lv_obj_set_size(settings_detail_value_label, 180, 48);
    lv_obj_set_style_text_align(settings_detail_value_label,
                                LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(settings_detail_value_label,
                   UI_SCREEN_WIDTH - SETTINGS_SIDE_PADDING - 180,
                   y - 12);

    slider = lv_slider_create(parent);
    lv_obj_set_size(slider, SETTINGS_CONTENT_WIDTH, 18);
    lv_obj_set_pos(slider, SETTINGS_SIDE_PADDING, y + 64);
    if(item_id == SETTINGS_ITEM_BRIGHTNESS)
        lv_slider_set_range(slider, SETTINGS_BRIGHTNESS_MIN, 100);
    else
        lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, settings_page_get_slider(item_id),
                        LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_hex(UI_COLOR_TRACK),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(UI_COLOR_PRIMARY),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(UI_COLOR_TEXT),
                              LV_PART_KNOB);
    settings_page_style_control(slider);
    lv_obj_add_event_cb(slider, settings_page_slider_changed,
                        LV_EVENT_VALUE_CHANGED,
                        (void *)(uintptr_t)item_id);

    decrement = lv_button_create(parent);
    lv_obj_set_size(decrement, 72, 44);
    lv_obj_set_pos(decrement, SETTINGS_SIDE_PADDING, y + 106);
    settings_page_style_button(decrement, UI_COLOR_PANEL);
    lv_obj_add_event_cb(decrement, settings_page_slider_step_clicked,
                        LV_EVENT_CLICKED, (void *)(uintptr_t)false);
    decrement_label = settings_page_create_label(
        decrement, LV_SYMBOL_MINUS, &lv_font_montserrat_20,
        UI_COLOR_TEXT);
    lv_obj_center(decrement_label);

    increment = lv_button_create(parent);
    lv_obj_set_size(increment, 72, 44);
    lv_obj_set_pos(increment,
                   UI_SCREEN_WIDTH - SETTINGS_SIDE_PADDING - 72,
                   y + 106);
    settings_page_style_button(increment, UI_COLOR_PANEL);
    lv_obj_add_event_cb(increment, settings_page_slider_step_clicked,
                        LV_EVENT_CLICKED, (void *)(uintptr_t)true);
    increment_label = settings_page_create_label(
        increment, LV_SYMBOL_PLUS, &lv_font_montserrat_20,
        UI_COLOR_TEXT);
    lv_obj_center(increment_label);

    settings_active_slider = slider;
    settings_active_slider_item = item_id;
    if (page->group != RT_NULL)
    {
        lv_group_add_obj(page->group, slider);
        lv_group_add_obj(page->group, decrement);
        lv_group_add_obj(page->group, increment);
    }
    settings_page_update_detail_value(item_id);
    return slider;
}

static void settings_page_update_motor_amplitude(void)
{
    char value[16];

    if (settings_motor_amplitude_label == RT_NULL)
        return;
    rt_snprintf(value, sizeof(value), "%u%%",
                (unsigned int)settings_state.motor_amplitude);
    lv_label_set_text(settings_motor_amplitude_label, value);
}

static void settings_page_motor_amplitude_changed(lv_event_t *event)
{
    lv_obj_t *slider = lv_event_get_target(event);

    settings_state.motor_amplitude =
        (uint8_t)lv_slider_get_value(slider);
    settings_page_update_motor_amplitude();
}

static void settings_page_motor_amplitude_released(lv_event_t *event)
{
    (void)event;
    (void)settings_service_preview_motor(settings_state.motor_amplitude,
                                         settings_state.motor_effect);
}

static void settings_page_motor_amplitude_step_clicked(lv_event_t *event)
{
    bool increment = (bool)(uintptr_t)lv_event_get_user_data(event);
    int32_t value;

    if (settings_motor_amplitude_slider == RT_NULL)
        return;
    value = lv_slider_get_value(settings_motor_amplitude_slider);
    value += increment ? 5 : -5;
    if (value < 0)
        value = 0;
    else if (value > 100)
        value = 100;
    lv_slider_set_value(settings_motor_amplitude_slider, value,
                        LV_ANIM_ON);
    settings_state.motor_amplitude = (uint8_t)value;
    settings_page_update_motor_amplitude();
    (void)settings_service_preview_motor(settings_state.motor_amplitude,
                                         settings_state.motor_effect);
}

static void settings_page_motor_effect_clicked(lv_event_t *event)
{
    uint32_t selected =
        (uint32_t)(uintptr_t)lv_event_get_user_data(event);
    uint32_t index;

    if (selected >= SETTINGS_MOTOR_EFFECT_COUNT)
        return;
    settings_state.motor_effect = (uint8_t)selected;
    for (index = 0U; index < SETTINGS_MOTOR_EFFECT_COUNT; index++)
    {
        if (settings_motor_effect_checks[index] == RT_NULL)
            continue;
        if (index == selected)
            lv_obj_remove_flag(settings_motor_effect_checks[index],
                               LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(settings_motor_effect_checks[index],
                            LV_OBJ_FLAG_HIDDEN);
    }
    (void)settings_service_preview_motor(settings_state.motor_amplitude,
                                         settings_state.motor_effect);
}

static lv_obj_t *settings_page_add_motor(lv_obj_t *parent,
                                         ui_page_t *page)
{
    lv_obj_t *control;
    lv_obj_t *caption;
    lv_obj_t *decrement;
    lv_obj_t *increment;
    uint32_t index;

    control = settings_page_add_switch(parent, SETTINGS_ITEM_MOTOR);
    lv_obj_set_y(lv_obj_get_parent(control), 12);
    if (page->group != RT_NULL)
        lv_group_add_obj(page->group, control);

    caption = settings_page_create_label(parent, "Vibration amplitude",
                                         &lv_font_montserrat_16,
                                         UI_COLOR_MUTED);
    lv_obj_set_pos(caption, SETTINGS_SIDE_PADDING, 124);

    settings_motor_amplitude_label = settings_page_create_label(
        parent, "", &lv_font_montserrat_24, UI_COLOR_TEXT);
    lv_obj_set_size(settings_motor_amplitude_label, 100, 34);
    lv_obj_set_style_text_align(settings_motor_amplitude_label,
                                LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(settings_motor_amplitude_label,
                   UI_SCREEN_WIDTH - SETTINGS_SIDE_PADDING - 100, 116);

    settings_motor_amplitude_slider = lv_slider_create(parent);
    lv_obj_set_size(settings_motor_amplitude_slider,
                    SETTINGS_CONTENT_WIDTH, 18);
    lv_obj_set_pos(settings_motor_amplitude_slider,
                   SETTINGS_SIDE_PADDING, 164);
    lv_slider_set_range(settings_motor_amplitude_slider, 0, 100);
    lv_slider_set_value(settings_motor_amplitude_slider,
                        settings_state.motor_amplitude, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(settings_motor_amplitude_slider,
                              lv_color_hex(UI_COLOR_TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_color(settings_motor_amplitude_slider,
                              lv_color_hex(UI_COLOR_PRIMARY),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(settings_motor_amplitude_slider,
                              lv_color_hex(UI_COLOR_TEXT), LV_PART_KNOB);
    settings_page_style_control(settings_motor_amplitude_slider);
    lv_obj_add_flag(settings_motor_amplitude_slider,
                    LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_event_cb(settings_motor_amplitude_slider,
                        settings_page_motor_amplitude_changed,
                        LV_EVENT_VALUE_CHANGED, RT_NULL);
    lv_obj_add_event_cb(settings_motor_amplitude_slider,
                        settings_page_motor_amplitude_released,
                        LV_EVENT_RELEASED, RT_NULL);

    decrement = lv_button_create(parent);
    lv_obj_set_size(decrement, 72, 42);
    lv_obj_set_pos(decrement, SETTINGS_SIDE_PADDING, 198);
    settings_page_style_button(decrement, UI_COLOR_PANEL);
    lv_obj_add_flag(decrement, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_event_cb(decrement,
                        settings_page_motor_amplitude_step_clicked,
                        LV_EVENT_CLICKED, (void *)(uintptr_t)false);
    lv_obj_center(settings_page_create_label(
        decrement, LV_SYMBOL_MINUS, &lv_font_montserrat_20,
        UI_COLOR_TEXT));

    increment = lv_button_create(parent);
    lv_obj_set_size(increment, 72, 42);
    lv_obj_set_pos(increment,
                   UI_SCREEN_WIDTH - SETTINGS_SIDE_PADDING - 72, 198);
    settings_page_style_button(increment, UI_COLOR_PANEL);
    lv_obj_add_flag(increment, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_event_cb(increment,
                        settings_page_motor_amplitude_step_clicked,
                        LV_EVENT_CLICKED, (void *)(uintptr_t)true);
    lv_obj_center(settings_page_create_label(
        increment, LV_SYMBOL_PLUS, &lv_font_montserrat_20,
        UI_COLOR_TEXT));

    caption = settings_page_create_label(parent, "Vibration effect",
                                         &lv_font_montserrat_16,
                                         UI_COLOR_MUTED);
    lv_obj_set_pos(caption, SETTINGS_SIDE_PADDING, 264);

    if (page->group != RT_NULL)
    {
        lv_group_add_obj(page->group, settings_motor_amplitude_slider);
        lv_group_add_obj(page->group, decrement);
        lv_group_add_obj(page->group, increment);
    }

    for (index = 0U; index < SETTINGS_MOTOR_EFFECT_COUNT; index++)
    {
        lv_obj_t *button = lv_button_create(parent);
        lv_obj_t *name;
        lv_obj_t *description;
        lv_obj_t *check;

        lv_obj_set_size(button, SETTINGS_CONTENT_WIDTH, 64);
        lv_obj_set_pos(button, SETTINGS_SIDE_PADDING,
                       294 + (int32_t)index * 72);
        settings_page_style_button(button, UI_COLOR_PANEL);
        lv_obj_add_flag(button, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        lv_obj_add_event_cb(button, settings_page_motor_effect_clicked,
                            LV_EVENT_CLICKED,
                            (void *)(uintptr_t)index);

        name = settings_page_create_label(button,
                                          motor_effect_names[index],
                                          &lv_font_montserrat_18,
                                          UI_COLOR_TEXT);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 16, -12);
        description = settings_page_create_label(
            button, motor_effect_descriptions[index],
            &lv_font_montserrat_14, UI_COLOR_MUTED);
        lv_obj_align(description, LV_ALIGN_LEFT_MID, 16, 14);
        check = settings_page_create_label(button, LV_SYMBOL_OK,
                                           &lv_font_montserrat_18,
                                           UI_COLOR_PRIMARY);
        lv_obj_align(check, LV_ALIGN_RIGHT_MID, -16, 0);
        if (index != settings_state.motor_effect)
            lv_obj_add_flag(check, LV_OBJ_FLAG_HIDDEN);

        settings_motor_effect_buttons[index] = button;
        settings_motor_effect_checks[index] = check;
        if (page->group != RT_NULL)
            lv_group_add_obj(page->group, button);
    }
    settings_page_update_motor_amplitude();
    return control;
}

static void settings_page_menu_clicked(lv_event_t *event)
{
    uint32_t selected =
        (uint32_t)(uintptr_t)lv_event_get_user_data(event);
    uint32_t index;
    rt_err_t result;

    if (selected >= 2U)
        return;

    result = settings_service_set_menu_style((uint8_t)selected);
    if (result == RT_EOK)
        settings_state.menu_style = (uint8_t)selected;
    else
        LOG_E("Apply menu style failed: %d", result);

    for (index = 0U; index < 2U; index++)
    {
        if (settings_menu_buttons[index] == RT_NULL)
            continue;
        if (index == settings_state.menu_style)
        {
            lv_obj_add_state(settings_menu_buttons[index],
                             LV_STATE_CHECKED);
            if (settings_menu_check_labels[index] != RT_NULL)
                lv_obj_remove_flag(settings_menu_check_labels[index],
                                   LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_clear_state(settings_menu_buttons[index],
                               LV_STATE_CHECKED);
            if (settings_menu_check_labels[index] != RT_NULL)
                lv_obj_add_flag(settings_menu_check_labels[index],
                                LV_OBJ_FLAG_HIDDEN);
        }
    }
    settings_page_update_row_value(SETTINGS_ITEM_MENU_STYLE);
}

static lv_obj_t *settings_page_add_menu_choices(lv_obj_t *parent,
                                                ui_page_t *page)
{
    static const char *const names[] = {"Carousel", "Honeycomb"};
    lv_obj_t *first = RT_NULL;
    uint32_t index;

    for (index = 0U; index < 2U; index++)
    {
        lv_obj_t *button = lv_button_create(parent);
        lv_obj_t *name;
        lv_obj_t *check;

        lv_obj_set_size(button, SETTINGS_CONTENT_WIDTH, 68);
        lv_obj_set_pos(button, SETTINGS_SIDE_PADDING,
                       28 + (int32_t)index * 78);
        settings_page_style_button(button, UI_COLOR_PANEL);
        lv_obj_set_style_bg_color(button,
                                  lv_color_hex(UI_COLOR_PRIMARY),
                                  LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(button, LV_OPA_30,
                                LV_STATE_CHECKED);
        lv_obj_add_flag(button, LV_OBJ_FLAG_CHECKABLE);
        lv_obj_add_event_cb(button, settings_page_menu_clicked,
                            LV_EVENT_CLICKED,
                            (void *)(uintptr_t)index);
        if (index == settings_state.menu_style)
            lv_obj_add_state(button, LV_STATE_CHECKED);

        name = settings_page_create_label(button, names[index],
                                          &lv_font_montserrat_18,
                                          UI_COLOR_TEXT);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 18, 0);
        check = settings_page_create_label(button, LV_SYMBOL_OK,
                                           &lv_font_montserrat_18,
                                           UI_COLOR_PRIMARY);
        lv_obj_align(check, LV_ALIGN_RIGHT_MID, -18, 0);
        if (index != settings_state.menu_style)
            lv_obj_add_flag(check, LV_OBJ_FLAG_HIDDEN);

        settings_menu_buttons[index] = button;
        settings_menu_check_labels[index] = check;
        if (page->group != RT_NULL)
            lv_group_add_obj(page->group, button);
        if (first == RT_NULL)
            first = button;
    }
    return first;
}

static void settings_page_action_clicked(lv_event_t *event)
{
    lv_obj_t *status = lv_event_get_user_data(event);
    settings_power_action_t action;
    rt_err_t result;

    switch (settings_active_item)
    {
    case SETTINGS_ITEM_POWER_OFF:
        action = SETTINGS_POWER_OFF;
        break;
    case SETTINGS_ITEM_RESTART:
        action = SETTINGS_POWER_RESTART;
        break;
    default:
        return;
    }

    result = settings_service_request_power_action(action);

    if (status != RT_NULL)
    {
        lv_label_set_text(status, result == RT_EOK
                                      ? "Scheduled"
                                      : "Action failed");
        lv_obj_set_style_text_color(status,
                                    lv_color_hex(result == RT_EOK
                                                     ? UI_COLOR_PRIMARY
                                                     : UI_COLOR_ERROR),
                                    0);
    }
    if (result != RT_EOK)
        LOG_E("Schedule %s failed: %d",
              settings_items[settings_active_item].name, result);
}

static lv_obj_t *settings_page_add_action(lv_obj_t *parent,
                                          settings_item_id_t item_id,
                                          ui_page_t *page)
{
    const char *prompt;
    const char *action_text;
    lv_obj_t *prompt_label;
    lv_obj_t *status;
    lv_obj_t *confirm;
    lv_obj_t *confirm_label;
    lv_obj_t *cancel;
    lv_obj_t *cancel_label;

    if (item_id == SETTINGS_ITEM_POWER_OFF)
    {
        prompt = "Power off this device?";
        action_text = "Power off";
    }
    else if (item_id == SETTINGS_ITEM_RESTART)
    {
        prompt = "Restart this device?";
        action_text = "Restart";
    }
    else
        return RT_NULL;

    prompt_label = settings_page_create_label(parent, prompt,
                                              &lv_font_montserrat_22,
                                              UI_COLOR_TEXT);
    lv_obj_set_size(prompt_label, SETTINGS_CONTENT_WIDTH, 34);
    lv_obj_set_pos(prompt_label, SETTINGS_SIDE_PADDING, 38);

    status = settings_page_create_label(parent, "Awaiting confirmation",
                                        &lv_font_montserrat_16,
                                        UI_COLOR_MUTED);
    lv_obj_set_pos(status, SETTINGS_SIDE_PADDING, 82);

    confirm = lv_button_create(parent);
    lv_obj_set_size(confirm, 204, 54);
    lv_obj_set_pos(confirm, SETTINGS_SIDE_PADDING, 142);
    settings_page_style_button(confirm, UI_COLOR_ERROR);
    lv_obj_add_event_cb(confirm, settings_page_action_clicked,
                        LV_EVENT_CLICKED, status);
    confirm_label = settings_page_create_label(
        confirm, action_text,
        &lv_font_montserrat_18, UI_COLOR_TEXT);
    lv_obj_center(confirm_label);

    cancel = lv_button_create(parent);
    lv_obj_set_size(cancel, 204, 54);
    lv_obj_set_pos(cancel, SETTINGS_SIDE_PADDING + 228, 142);
    settings_page_style_button(cancel, UI_COLOR_PANEL);
    lv_obj_add_event_cb(cancel, settings_page_back_clicked,
                        LV_EVENT_CLICKED, RT_NULL);
    cancel_label = settings_page_create_label(cancel, "Cancel",
                                              &lv_font_montserrat_18,
                                              UI_COLOR_TEXT);
    lv_obj_center(cancel_label);

    if (page->group != RT_NULL)
    {
        lv_group_add_obj(page->group, confirm);
        lv_group_add_obj(page->group, cancel);
    }
    return confirm;
}

static void settings_page_rgb_color_clicked(lv_event_t *event)
{
    uint32_t selected =
        (uint32_t)(uintptr_t)lv_event_get_user_data(event);
    uint32_t index;

    if (selected >= sizeof(rgb_colors) / sizeof(rgb_colors[0]))
        return;
    rt_err_t result;

    result = settings_service_set_rgb(settings_state.rgb_enabled,
                                      settings_state.rgb_brightness,
                                      (uint8_t)selected);
    if (result == RT_EOK)
        settings_state.rgb_color = (uint8_t)selected;
    else
        LOG_E("Apply RGB color failed: %d", result);

    for (index = 0U; index < sizeof(rgb_colors) / sizeof(rgb_colors[0]);
         index++)
    {
        if (settings_rgb_buttons[index] == RT_NULL)
            continue;
        lv_obj_set_style_border_width(settings_rgb_buttons[index],
                                      index == settings_state.rgb_color
                                          ? 3
                                          : 0,
                                      0);
    }
    settings_page_update_row_value(SETTINGS_ITEM_RGB_LED);
}

static lv_obj_t *settings_page_add_rgb(lv_obj_t *parent, ui_page_t *page)
{
    lv_obj_t *control;
    lv_obj_t *slider;
    lv_obj_t *color_title;
    uint32_t index;

    control = settings_page_add_switch(parent, SETTINGS_ITEM_RGB_LED);
    lv_obj_set_y(lv_obj_get_parent(control), 16);
    if (page->group != RT_NULL)
        lv_group_add_obj(page->group, control);

    slider = settings_page_add_slider(parent, SETTINGS_ITEM_RGB_LED,
                                      132, page);

    color_title = settings_page_create_label(parent, "Color",
                                             &lv_font_montserrat_16,
                                             UI_COLOR_MUTED);
    lv_obj_set_pos(color_title, SETTINGS_SIDE_PADDING, 298);

    for (index = 0U; index < sizeof(rgb_colors) / sizeof(rgb_colors[0]);
         index++)
    {
        lv_obj_t *button = lv_button_create(parent);

        lv_obj_set_size(button, 52, 52);
        lv_obj_set_pos(button,
                       SETTINGS_SIDE_PADDING + (int32_t)index * 72,
                       330);
        settings_page_style_button(button, rgb_colors[index]);
        lv_obj_set_style_border_color(button,
                                      lv_color_hex(UI_COLOR_TEXT), 0);
        lv_obj_set_style_border_width(button,
                                      index == settings_state.rgb_color
                                          ? 3
                                          : 0,
                                      0);
        lv_obj_add_event_cb(button, settings_page_rgb_color_clicked,
                            LV_EVENT_CLICKED,
                            (void *)(uintptr_t)index);
        settings_rgb_buttons[index] = button;
        if (page->group != RT_NULL)
            lv_group_add_obj(page->group, button);
    }
    return control;
}

static void settings_page_show_detail(ui_page_t *page,
                                      settings_item_id_t item_id)
{
    lv_obj_t *back;
    lv_obj_t *body;
    lv_obj_t *focus = RT_NULL;

    if (page == RT_NULL || page->root == RT_NULL ||
        item_id >= SETTINGS_ITEM_COUNT)
        return;

    settings_active_item = item_id;
    if (page->group != RT_NULL)
        lv_group_remove_all_objs(page->group);
    if (settings_detail_view != RT_NULL)
        lv_obj_del(settings_detail_view);

    memset(settings_menu_buttons, 0, sizeof(settings_menu_buttons));
    memset(settings_menu_check_labels, 0,
           sizeof(settings_menu_check_labels));
    memset(settings_rgb_buttons, 0, sizeof(settings_rgb_buttons));
    memset(settings_motor_effect_buttons, 0,
           sizeof(settings_motor_effect_buttons));
    memset(settings_motor_effect_checks, 0,
           sizeof(settings_motor_effect_checks));
    settings_detail_value_label = RT_NULL;
    settings_detail_switch_value_label = RT_NULL;
    settings_pan_value_label = RT_NULL;
    settings_pan_control = RT_NULL;
    settings_active_slider = RT_NULL;
    settings_motor_amplitude_label = RT_NULL;
    settings_motor_amplitude_slider = RT_NULL;
#if defined(FACTORY_HW_USING_KEYBOARD)
    settings_wifi_switch = RT_NULL;
    settings_wifi_status_label = RT_NULL;
    settings_wifi_rssi_label = RT_NULL;
    settings_wifi_scan_button = RT_NULL;
    settings_wifi_scan_label = RT_NULL;
    settings_wifi_previous_button = RT_NULL;
    settings_wifi_next_button = RT_NULL;
    settings_wifi_page_label = RT_NULL;
    settings_wifi_password_view = RT_NULL;
    settings_wifi_password_input = RT_NULL;
    settings_wifi_password_ssid_label = RT_NULL;
    settings_wifi_keyboard = RT_NULL;
    memset(settings_wifi_network_buttons, 0,
           sizeof(settings_wifi_network_buttons));
    memset(settings_wifi_network_names, 0,
           sizeof(settings_wifi_network_names));
    memset(settings_wifi_network_values, 0,
           sizeof(settings_wifi_network_values));
#endif
    lv_obj_add_flag(settings_main_view, LV_OBJ_FLAG_HIDDEN);

    settings_detail_view = lv_obj_create(page->root);
    lv_obj_set_size(settings_detail_view, UI_SCREEN_WIDTH,
                    UI_SCREEN_HEIGHT);
    settings_page_style_container(settings_detail_view);
    lv_obj_remove_flag(settings_detail_view, LV_OBJ_FLAG_SCROLLABLE);

    back = settings_page_create_header(settings_detail_view,
                                       settings_items[item_id].name);
    settings_detail_back = back;
    if (page->group != RT_NULL)
        lv_group_add_obj(page->group, back);

    body = lv_obj_create(settings_detail_view);
    settings_detail_body = body;
    lv_obj_set_size(body, UI_SCREEN_WIDTH, SETTINGS_CONTENT_HEIGHT);
    lv_obj_set_pos(body, 0, SETTINGS_CONTENT_Y);
    settings_page_style_container(body);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_AUTO);

    switch (settings_items[item_id].kind)
    {
    case SETTINGS_KIND_WIFI:
#if defined(FACTORY_HW_USING_KEYBOARD)
        settings_wifi_page = 0U;
        settings_wifi_auto_scan_pending = true;
        settings_wifi_refresh_ticks = 0U;
        (void)settings_service_refresh_wifi_status();
        focus = settings_page_add_wifi(body, page);
#endif
        break;
    case SETTINGS_KIND_SWITCH:
        if (item_id == SETTINGS_ITEM_MOTOR)
        {
            focus = settings_page_add_motor(body, page);
        }
        else
        {
            focus = settings_page_add_switch(body, item_id);
            if (page->group != RT_NULL)
                lv_group_add_obj(page->group, focus);
            if (item_id == SETTINGS_ITEM_BLUETOOTH)
            {
                lv_obj_t *pan_switch = settings_page_add_pan_switch(body);

                if (page->group != RT_NULL)
                    lv_group_add_obj(page->group, pan_switch);
            }
        }
        break;
    case SETTINGS_KIND_SLIDER:
        focus = settings_page_add_slider(body, item_id, 54, page);
        break;
    case SETTINGS_KIND_MENU_STYLE:
        focus = settings_page_add_menu_choices(body, page);
        break;
    case SETTINGS_KIND_ACTION:
        focus = settings_page_add_action(body, item_id, page);
        break;
    case SETTINGS_KIND_RGB:
        focus = settings_page_add_rgb(body, page);
        break;
    default:
        break;
    }

    if (focus != RT_NULL)
        lv_group_focus_obj(focus);
}

static void settings_page_show_main(ui_page_t *page)
{
    if (page == RT_NULL || settings_main_view == RT_NULL)
        return;

    if (page->group != RT_NULL)
        lv_group_remove_all_objs(page->group);
    if (settings_detail_view != RT_NULL)
    {
        lv_obj_del(settings_detail_view);
        settings_detail_view = RT_NULL;
    }
    settings_detail_back = RT_NULL;
    settings_detail_body = RT_NULL;
    settings_detail_value_label = RT_NULL;
    settings_detail_switch_value_label = RT_NULL;
    settings_pan_value_label = RT_NULL;
    settings_pan_control = RT_NULL;
    settings_active_slider = RT_NULL;
    settings_motor_amplitude_label = RT_NULL;
    settings_motor_amplitude_slider = RT_NULL;
#if defined(FACTORY_HW_USING_KEYBOARD)
    settings_wifi_switch = RT_NULL;
    settings_wifi_status_label = RT_NULL;
    settings_wifi_rssi_label = RT_NULL;
    settings_wifi_scan_button = RT_NULL;
    settings_wifi_scan_label = RT_NULL;
    settings_wifi_previous_button = RT_NULL;
    settings_wifi_next_button = RT_NULL;
    settings_wifi_page_label = RT_NULL;
    settings_wifi_password_view = RT_NULL;
    settings_wifi_password_input = RT_NULL;
    settings_wifi_password_ssid_label = RT_NULL;
    settings_wifi_keyboard = RT_NULL;
    memset(settings_wifi_network_buttons, 0,
           sizeof(settings_wifi_network_buttons));
    memset(settings_wifi_network_names, 0,
           sizeof(settings_wifi_network_names));
    memset(settings_wifi_network_values, 0,
           sizeof(settings_wifi_network_values));
#endif
    memset(settings_menu_buttons, 0, sizeof(settings_menu_buttons));
    memset(settings_menu_check_labels, 0,
           sizeof(settings_menu_check_labels));
    memset(settings_rgb_buttons, 0, sizeof(settings_rgb_buttons));
    memset(settings_motor_effect_buttons, 0,
           sizeof(settings_motor_effect_buttons));
    memset(settings_motor_effect_checks, 0,
           sizeof(settings_motor_effect_checks));
    lv_obj_remove_flag(settings_main_view, LV_OBJ_FLAG_HIDDEN);
    settings_page_add_main_group_objects(page);
}

static void settings_page_on_create(ui_page_t *page)
{
    uint32_t index;

    if (page == RT_NULL || page->root == RT_NULL)
        return;

    settings_page_owner = page;
    settings_active_item = SETTINGS_ITEM_BLUETOOTH;
    settings_page_load_state();
    memset(settings_rows, 0, sizeof(settings_rows));
    memset(settings_focus_rows, 0, sizeof(settings_focus_rows));
    settings_focus_row_count = 0U;
    memset(settings_value_labels, 0, sizeof(settings_value_labels));
    if (page->group != RT_NULL)
        lv_group_set_wrap(page->group, true);

    settings_main_view = lv_obj_create(page->root);
    lv_obj_set_size(settings_main_view, UI_SCREEN_WIDTH,
                    UI_SCREEN_HEIGHT);
    settings_page_style_container(settings_main_view);
    lv_obj_remove_flag(settings_main_view, LV_OBJ_FLAG_SCROLLABLE);

    settings_main_back = settings_page_create_header(settings_main_view,
                                                     "SETTINGS");

    settings_main_list = lv_obj_create(settings_main_view);
    lv_obj_set_size(settings_main_list, UI_SCREEN_WIDTH,
                    SETTINGS_CONTENT_HEIGHT);
    lv_obj_set_pos(settings_main_list, 0, SETTINGS_CONTENT_Y);
    settings_page_style_container(settings_main_list);
    lv_obj_set_style_pad_left(settings_main_list,
                              SETTINGS_SIDE_PADDING, 0);
    lv_obj_set_style_pad_right(settings_main_list,
                               SETTINGS_SIDE_PADDING, 0);
    lv_obj_set_style_pad_bottom(settings_main_list, 24, 0);
    lv_obj_set_style_pad_row(settings_main_list, 0, 0);
    lv_obj_set_scroll_dir(settings_main_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(settings_main_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(settings_main_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(settings_main_list, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    settings_page_add_section(settings_main_list, "SYSTEM");
    for (index = SETTINGS_ITEM_BLUETOOTH;
         index <= SETTINGS_ITEM_RESTART; index++)
    {
        settings_page_add_row(settings_main_list,
                              (settings_item_id_t)index);
    }

#if defined(FACTORY_HW_USING_KEYBOARD)
    settings_page_add_section(settings_main_list, "KEYBOARD");
    settings_page_add_row(settings_main_list, SETTINGS_ITEM_WIFI);
    settings_page_add_row(settings_main_list, SETTINGS_ITEM_MOTOR);
    settings_page_add_row(settings_main_list, SETTINGS_ITEM_KEYBOARD_LED);
#elif defined(FACTORY_HW_USING_JOYPAD)
    settings_page_add_section(settings_main_list, "JOYPAD");
    settings_page_add_row(settings_main_list, SETTINGS_ITEM_MOTOR);
    settings_page_add_row(settings_main_list, SETTINGS_ITEM_RGB_LED);
#endif

    settings_page_show_main(page);
}

static void settings_page_on_enter(ui_page_t *page)
{
    uint32_t index;

    settings_page_load_state();
    for (index = 0U; index < SETTINGS_ITEM_COUNT; index++)
        settings_page_update_row_value((settings_item_id_t)index);
    settings_page_show_main(page);
}

static void settings_page_on_leave(ui_page_t *page)
{
    settings_page_show_main(page);
}

static void settings_page_on_destroy(ui_page_t *page)
{
    (void)page;
    settings_page_owner = RT_NULL;
    settings_main_view = RT_NULL;
    settings_main_back = RT_NULL;
    settings_main_list = RT_NULL;
    settings_detail_view = RT_NULL;
    settings_detail_back = RT_NULL;
    settings_detail_body = RT_NULL;
    settings_detail_value_label = RT_NULL;
    settings_detail_switch_value_label = RT_NULL;
    settings_pan_value_label = RT_NULL;
    settings_pan_control = RT_NULL;
    settings_active_slider = RT_NULL;
    settings_motor_amplitude_label = RT_NULL;
    settings_motor_amplitude_slider = RT_NULL;
    memset(settings_rows, 0, sizeof(settings_rows));
    memset(settings_focus_rows, 0, sizeof(settings_focus_rows));
    settings_focus_row_count = 0U;
    memset(settings_value_labels, 0, sizeof(settings_value_labels));
    memset(settings_menu_buttons, 0, sizeof(settings_menu_buttons));
    memset(settings_menu_check_labels, 0,
           sizeof(settings_menu_check_labels));
    memset(settings_rgb_buttons, 0, sizeof(settings_rgb_buttons));
    memset(settings_motor_effect_buttons, 0,
           sizeof(settings_motor_effect_buttons));
    memset(settings_motor_effect_checks, 0,
           sizeof(settings_motor_effect_checks));
#if defined(FACTORY_HW_USING_KEYBOARD)
    settings_wifi_switch = RT_NULL;
    settings_wifi_status_label = RT_NULL;
    settings_wifi_rssi_label = RT_NULL;
    settings_wifi_scan_button = RT_NULL;
    settings_wifi_scan_label = RT_NULL;
    settings_wifi_previous_button = RT_NULL;
    settings_wifi_next_button = RT_NULL;
    settings_wifi_page_label = RT_NULL;
    settings_wifi_password_view = RT_NULL;
    settings_wifi_password_input = RT_NULL;
    settings_wifi_password_ssid_label = RT_NULL;
    settings_wifi_keyboard = RT_NULL;
    memset(settings_wifi_network_buttons, 0,
           sizeof(settings_wifi_network_buttons));
    memset(settings_wifi_network_names, 0,
           sizeof(settings_wifi_network_names));
    memset(settings_wifi_network_values, 0,
           sizeof(settings_wifi_network_values));
#endif
}

static void settings_page_on_timer(ui_page_t *page)
{
    (void)page;
    if (settings_detail_view != RT_NULL &&
            settings_active_item == SETTINGS_ITEM_BLUETOOTH)
    {
        settings_page_update_pan_status();
    }
#if defined(FACTORY_HW_USING_KEYBOARD)
    if (settings_detail_view == RT_NULL ||
            settings_active_item != SETTINGS_ITEM_WIFI)
        return;

    settings_page_update_wifi();
    if (settings_wifi_auto_scan_pending &&
            settings_wifi_status.enabled &&
            !settings_wifi_status.scanning &&
            !settings_wifi_status.connecting &&
            settings_wifi_network_count == 0U)
    {
        rt_err_t result = settings_service_scan_wifi();

        if (result == RT_EOK)
            settings_wifi_auto_scan_pending = false;
    }
    settings_wifi_refresh_ticks++;
    if (settings_wifi_refresh_ticks >= SETTINGS_WIFI_REFRESH_TICKS)
    {
        settings_wifi_refresh_ticks = 0U;
        if (settings_wifi_status.enabled &&
                !settings_wifi_status.scanning &&
                !settings_wifi_status.connecting &&
                !settings_wifi_auto_scan_pending)
            (void)settings_service_refresh_wifi_status();
    }
#endif
}

static void settings_page_on_gesture(ui_page_t *page,
                                     ui_gesture_t gesture)
{
    if (gesture != UI_GESTURE_RIGHT)
        return;

#if defined(FACTORY_HW_USING_KEYBOARD)
    if (settings_wifi_password_view != RT_NULL &&
            !lv_obj_has_flag(settings_wifi_password_view,
                             LV_OBJ_FLAG_HIDDEN))
    {
        settings_page_hide_wifi_password();
        return;
    }
#endif
    if (settings_detail_view != RT_NULL)
        settings_page_show_main(page);
    else
        ui_navigation_switch_async(UI_PAGE_HOME);
}

static void settings_page_on_event(ui_page_t *page,
                                   const ui_event_t *event)
{
    if (page == RT_NULL || event == RT_NULL)
        return;

    if (event->type == UI_EVENT_VOLUME_CHANGED && event->data <= 100U)
    {
        settings_state.volume = (uint8_t)event->data;
        settings_page_update_row_value(SETTINGS_ITEM_VOLUME);
        if (settings_active_item == SETTINGS_ITEM_VOLUME)
        {
            if (settings_active_slider != RT_NULL &&
                    settings_active_slider_item == SETTINGS_ITEM_VOLUME)
            {
                lv_slider_set_value(settings_active_slider,
                                    settings_state.volume, LV_ANIM_ON);
            }
            settings_page_update_detail_value(SETTINGS_ITEM_VOLUME);
        }
        return;
    }
    if (event->type != UI_EVENT_KEY)
        return;

    if (event->data == LV_KEY_ESC)
    {
#if defined(FACTORY_HW_USING_KEYBOARD)
        if (settings_wifi_password_view != RT_NULL &&
                !lv_obj_has_flag(settings_wifi_password_view,
                                 LV_OBJ_FLAG_HIDDEN))
        {
            settings_page_hide_wifi_password();
            return;
        }
#endif
        if (settings_detail_view != RT_NULL)
            settings_page_show_main(page);
        else
            ui_navigation_switch_async(UI_PAGE_HOME);
    }
    else if (event->data == LV_KEY_HOME)
    {
        ui_navigation_switch_async(UI_PAGE_HOME);
    }
}

rt_err_t settings_page_register(void)
{
    const ui_page_t page = {
        .id = UI_PAGE_SETTINGS,
        .app_id = UI_APP_NONE,
        .name = "Settings Page",
        .on_create = settings_page_on_create,
        .on_enter = settings_page_on_enter,
        .on_leave = settings_page_on_leave,
        .on_destroy = settings_page_on_destroy,
        .on_timer = settings_page_on_timer,
        .timer_interval_ms = 500U,
        .on_gesture = settings_page_on_gesture,
        .on_event = settings_page_on_event,
    };

    return ui_navigation_register_page(&page);
}
