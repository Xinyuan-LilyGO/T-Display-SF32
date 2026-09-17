#include "ui_status_bar.h"

#include <lvgl.h>

#include "ui_config.h"
#include "ui_theme.h"

#define UI_VOLUME_POPUP_TIMEOUT_MS 1200U

static lv_obj_t *status_bar;
static lv_obj_t *bluetooth_label;
static lv_obj_t *wifi_label;
static lv_obj_t *gps_label;
static lv_obj_t *time_label;
static lv_obj_t *battery_icon_label;
static lv_obj_t *battery_percent_label;
static lv_obj_t *volume_popup;
static lv_obj_t *volume_icon_label;
static lv_obj_t *volume_value_label;
static lv_obj_t *volume_bar;
static lv_timer_t *volume_hide_timer;
static lv_obj_t *pomodoro_popup;
static lv_obj_t *low_battery_popup;
static lv_obj_t *low_battery_voltage_label;
static lv_obj_t *low_battery_countdown_label;

static lv_obj_t *ui_status_bar_create_label(lv_obj_t *parent,
                                            const char *text,
                                            const lv_font_t *font)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_label_set_text(label, text);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(label, 0, 0);
    lv_obj_set_style_pad_all(label, 0, 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_letter_space(label, 0, 0);
    return label;
}

static rt_err_t ui_status_bar_set_connection(lv_obj_t *label,
                                             uintptr_t value)
{
    ui_status_state_t state;

    if (label == RT_NULL || value >= (uintptr_t)UI_STATUS_STATE_COUNT)
        return -RT_EINVAL;
    state = (ui_status_state_t)value;

    if (state == UI_STATUS_STATE_OFF)
    {
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
        return RT_EOK;
    }

    lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_color(
        label,
        lv_color_hex(state == UI_STATUS_STATE_ACTIVE
                         ? UI_COLOR_SUCCESS
                         : UI_COLOR_MUTED),
        0);
    return RT_EOK;
}

static rt_err_t ui_status_bar_set_battery(uintptr_t value)
{
    uint8_t percent = (uint8_t)(value & UI_EVENT_BATTERY_PERCENT_MASK);
    bool charging = (value & UI_EVENT_BATTERY_CHARGING) != 0U;
    const char *symbol;
    uint32_t color;

    if (battery_icon_label == RT_NULL || battery_percent_label == RT_NULL ||
            percent > 100U)
        return -RT_EINVAL;

    if (charging)
    {
        symbol = LV_SYMBOL_CHARGE;
        color = UI_COLOR_SUCCESS;
    }
    else if (percent >= 80U)
    {
        symbol = LV_SYMBOL_BATTERY_FULL;
        color = UI_COLOR_TEXT;
    }
    else if (percent >= 55U)
    {
        symbol = LV_SYMBOL_BATTERY_3;
        color = UI_COLOR_TEXT;
    }
    else if (percent >= 30U)
    {
        symbol = LV_SYMBOL_BATTERY_2;
        color = UI_COLOR_TEXT;
    }
    else if (percent >= 10U)
    {
        symbol = LV_SYMBOL_BATTERY_1;
        color = UI_COLOR_WARNING;
    }
    else
    {
        symbol = LV_SYMBOL_BATTERY_EMPTY;
        color = UI_COLOR_ERROR;
    }

    lv_label_set_text(battery_icon_label, symbol);
    lv_label_set_text_fmt(battery_percent_label, "%u%%",
                          (unsigned int)percent);
    lv_obj_set_style_text_color(battery_icon_label, lv_color_hex(color), 0);
    lv_obj_set_style_text_color(battery_percent_label,
                                lv_color_hex(color), 0);
    return RT_EOK;
}

static rt_err_t ui_status_bar_set_time(uintptr_t value)
{
    uint8_t hour = (uint8_t)(value >> UI_EVENT_TIME_HOUR_SHIFT);
    uint8_t minute = (uint8_t)(value & UI_EVENT_TIME_MINUTE_MASK);

    if (time_label == RT_NULL || hour > 23U || minute > 59U)
        return -RT_EINVAL;

    lv_label_set_text_fmt(time_label, "%02u:%02u",
                          (unsigned int)hour, (unsigned int)minute);
    return RT_EOK;
}

static void ui_status_bar_hide_volume(lv_timer_t *timer)
{
    if (volume_popup != RT_NULL)
        lv_obj_add_flag(volume_popup, LV_OBJ_FLAG_HIDDEN);
    lv_timer_pause(timer);
}

static rt_err_t ui_status_bar_create_volume_popup(void)
{
    lv_obj_t *top_layer;

    if (volume_popup != RT_NULL)
        return RT_EOK;
    top_layer = lv_layer_top();
    if (top_layer == RT_NULL)
        return -RT_ERROR;

    volume_popup = lv_obj_create(top_layer);
    if (volume_popup == RT_NULL)
        return -RT_ENOMEM;
    lv_obj_set_size(volume_popup, 196, 76);
    lv_obj_align(volume_popup, LV_ALIGN_TOP_MID, 0,
                 UI_STATUS_BAR_HEIGHT + 8);
    lv_obj_set_style_bg_color(volume_popup,
                              lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(volume_popup, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(volume_popup,
                                  lv_color_hex(UI_COLOR_TRACK), 0);
    lv_obj_set_style_border_width(volume_popup, 1, 0);
    lv_obj_set_style_radius(volume_popup, 4, 0);
    lv_obj_set_style_pad_all(volume_popup, 0, 0);
    lv_obj_set_style_shadow_width(volume_popup, 0, 0);
    lv_obj_remove_flag(volume_popup, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(volume_popup, LV_OBJ_FLAG_CLICKABLE);

    volume_icon_label = ui_status_bar_create_label(
        volume_popup, LV_SYMBOL_VOLUME_MID, &lv_font_montserrat_24);
    lv_obj_set_pos(volume_icon_label, 16, 14);
    volume_value_label = ui_status_bar_create_label(
        volume_popup, "Volume --%", &lv_font_montserrat_16);
    lv_obj_set_size(volume_value_label, 132, 24);
    lv_obj_set_pos(volume_value_label, 52, 14);

    volume_bar = lv_bar_create(volume_popup);
    lv_obj_set_size(volume_bar, 164, 8);
    lv_obj_set_pos(volume_bar, 16, 52);
    lv_bar_set_range(volume_bar, 0, 100);
    lv_obj_set_style_bg_color(volume_bar,
                              lv_color_hex(UI_COLOR_TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_color(volume_bar,
                              lv_color_hex(UI_COLOR_PRIMARY),
                              LV_PART_INDICATOR);
    lv_obj_set_style_radius(volume_bar, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(volume_bar, 2, LV_PART_INDICATOR);

    volume_hide_timer = lv_timer_create(ui_status_bar_hide_volume,
                                        UI_VOLUME_POPUP_TIMEOUT_MS,
                                        RT_NULL);
    if (volume_hide_timer == RT_NULL)
    {
        lv_obj_del(volume_popup);
        volume_popup = RT_NULL;
        volume_icon_label = RT_NULL;
        volume_value_label = RT_NULL;
        volume_bar = RT_NULL;
        return -RT_ENOMEM;
    }
    lv_timer_pause(volume_hide_timer);
    return RT_EOK;
}

static rt_err_t ui_status_bar_set_volume(uintptr_t value)
{
    uint8_t percent = (uint8_t)value;
    rt_err_t result;

    if (percent > 100U)
        return -RT_EINVAL;
    result = ui_status_bar_create_volume_popup();
    if (result != RT_EOK)
        return result;

    lv_label_set_text(volume_icon_label,
                      percent == 0U ? LV_SYMBOL_MUTE :
                      (percent < 67U ? LV_SYMBOL_VOLUME_MID :
                                       LV_SYMBOL_VOLUME_MAX));
    lv_label_set_text_fmt(volume_value_label, "Volume %u%%",
                          (unsigned int)percent);
    lv_bar_set_value(volume_bar, percent, LV_ANIM_ON);
    lv_obj_remove_flag(volume_popup, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(volume_popup);
    lv_timer_reset(volume_hide_timer);
    lv_timer_resume(volume_hide_timer);
    return RT_EOK;
}

static void ui_status_bar_delete_pomodoro_popup(void)
{
    if (pomodoro_popup != RT_NULL)
        lv_obj_delete_async(pomodoro_popup);
    pomodoro_popup = RT_NULL;
}

static void ui_status_bar_dismiss_pomodoro(lv_event_t *event)
{
    (void)event;
    ui_status_bar_delete_pomodoro_popup();
}

static rt_err_t ui_status_bar_create_pomodoro_popup(void)
{
    lv_obj_t *top_layer;
    lv_obj_t *label;
    lv_obj_t *button;

    if (pomodoro_popup != RT_NULL)
        return RT_EOK;
    top_layer = lv_layer_top();
    if (top_layer == RT_NULL)
        return -RT_ERROR;

    pomodoro_popup = lv_obj_create(top_layer);
    if (pomodoro_popup == RT_NULL)
        return -RT_ENOMEM;
    lv_obj_set_size(pomodoro_popup, 340, 174);
    lv_obj_align(pomodoro_popup, LV_ALIGN_CENTER, 0, 8);
    lv_obj_set_style_bg_color(pomodoro_popup,
                              lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(pomodoro_popup, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(pomodoro_popup,
                                  lv_color_hex(UI_COLOR_PRIMARY), 0);
    lv_obj_set_style_border_width(pomodoro_popup, 2, 0);
    lv_obj_set_style_radius(pomodoro_popup, 4, 0);
    lv_obj_set_style_pad_all(pomodoro_popup, 0, 0);
    lv_obj_set_style_shadow_width(pomodoro_popup, 0, 0);
    lv_obj_remove_flag(pomodoro_popup, LV_OBJ_FLAG_SCROLLABLE);

    label = ui_status_bar_create_label(pomodoro_popup,
                                       LV_SYMBOL_OK "  FOCUS COMPLETE",
                                       &lv_font_montserrat_20);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_PRIMARY), 0);
    lv_obj_set_size(label, 300, 30);
    lv_obj_set_pos(label, 20, 20);

    label = ui_status_bar_create_label(
        pomodoro_popup, "Pomodoro finished. Time for a break.",
        &lv_font_montserrat_14);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_MUTED), 0);
    lv_obj_set_size(label, 300, 24);
    lv_obj_set_pos(label, 20, 60);

    button = lv_button_create(pomodoro_popup);
    lv_obj_set_size(button, 112, 48);
    lv_obj_set_pos(button, 208, 106);
    lv_obj_set_style_bg_color(button, lv_color_hex(UI_COLOR_PRIMARY), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_radius(button, 4, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_add_event_cb(button, ui_status_bar_dismiss_pomodoro,
                        LV_EVENT_CLICKED, RT_NULL);
    label = ui_status_bar_create_label(button, "DONE",
                                       &lv_font_montserrat_16);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_BACKGROUND), 0);
    lv_obj_center(label);

    return RT_EOK;
}

static rt_err_t ui_status_bar_show_pomodoro_complete(void)
{
    rt_err_t result = ui_status_bar_create_pomodoro_popup();

    if (result != RT_EOK)
        return result;
    lv_obj_remove_flag(pomodoro_popup, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(pomodoro_popup);
    return RT_EOK;
}

static void ui_status_bar_delete_low_battery_popup(void)
{
    if (low_battery_popup != RT_NULL)
        lv_obj_del(low_battery_popup);
    low_battery_popup = RT_NULL;
    low_battery_voltage_label = RT_NULL;
    low_battery_countdown_label = RT_NULL;
}

static rt_err_t ui_status_bar_create_low_battery_popup(void)
{
    lv_obj_t *top_layer;
    lv_obj_t *label;

    if (low_battery_popup != RT_NULL)
        return RT_EOK;
    top_layer = lv_layer_top();
    if (top_layer == RT_NULL)
        return -RT_ERROR;

    low_battery_popup = lv_obj_create(top_layer);
    if (low_battery_popup == RT_NULL)
        return -RT_ENOMEM;
    lv_obj_set_size(low_battery_popup, 360, 210);
    lv_obj_align(low_battery_popup, LV_ALIGN_CENTER, 0, 8);
    lv_obj_set_style_bg_color(low_battery_popup,
                              lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(low_battery_popup, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(low_battery_popup,
                                  lv_color_hex(UI_COLOR_ERROR), 0);
    lv_obj_set_style_border_width(low_battery_popup, 2, 0);
    lv_obj_set_style_radius(low_battery_popup, 4, 0);
    lv_obj_set_style_pad_all(low_battery_popup, 0, 0);
    lv_obj_set_style_shadow_width(low_battery_popup, 0, 0);
    lv_obj_remove_flag(low_battery_popup, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(low_battery_popup, LV_OBJ_FLAG_CLICKABLE);

    label = ui_status_bar_create_label(
        low_battery_popup, LV_SYMBOL_BATTERY_EMPTY "  LOW BATTERY",
        &lv_font_montserrat_24);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_ERROR), 0);
    lv_obj_set_size(label, 320, 34);
    lv_obj_set_pos(label, 20, 20);

    low_battery_voltage_label = ui_status_bar_create_label(
        low_battery_popup, "Battery ---- mV", &lv_font_montserrat_18);
    lv_obj_set_size(low_battery_voltage_label, 320, 28);
    lv_obj_set_pos(low_battery_voltage_label, 20, 70);

    low_battery_countdown_label = ui_status_bar_create_label(
        low_battery_popup, "Power off in -- s", &lv_font_montserrat_20);
    lv_obj_set_style_text_color(low_battery_countdown_label,
                                lv_color_hex(UI_COLOR_WARNING), 0);
    lv_obj_set_size(low_battery_countdown_label, 320, 30);
    lv_obj_set_pos(low_battery_countdown_label, 20, 108);

    label = ui_status_bar_create_label(
        low_battery_popup, "Connect USB power to cancel",
        &lv_font_montserrat_14);
    lv_obj_set_style_text_color(label, lv_color_hex(UI_COLOR_MUTED), 0);
    lv_obj_set_size(label, 320, 24);
    lv_obj_set_pos(label, 20, 160);
    return RT_EOK;
}

static rt_err_t ui_status_bar_set_low_battery(uintptr_t value)
{
    bool active = (value & UI_EVENT_LOW_BATTERY_ACTIVE) != 0U;
    uint16_t voltage_mv =
        (uint16_t)(value & UI_EVENT_LOW_BATTERY_VOLTAGE_MASK);
    uint8_t remaining_seconds = (uint8_t)(
        (value >> UI_EVENT_LOW_BATTERY_SECONDS_SHIFT) &
        UI_EVENT_LOW_BATTERY_SECONDS_MASK);
    rt_err_t result;

    if (!active)
    {
        ui_status_bar_delete_low_battery_popup();
        return RT_EOK;
    }

    result = ui_status_bar_create_low_battery_popup();
    if (result != RT_EOK)
        return result;
    lv_label_set_text_fmt(low_battery_voltage_label, "Battery %u mV",
                          (unsigned int)voltage_mv);
    lv_label_set_text_fmt(low_battery_countdown_label,
                          "Power off in %u s",
                          (unsigned int)remaining_seconds);
    lv_obj_move_foreground(low_battery_popup);
    return RT_EOK;
}

rt_err_t ui_status_bar_init(void)
{
    lv_obj_t *top_layer;
    lv_obj_t *right_group;

    if (status_bar != RT_NULL)
        return RT_EOK;

    top_layer = lv_layer_top();
    if (top_layer == RT_NULL)
        return -RT_ERROR;

    status_bar = lv_obj_create(top_layer);
    if (status_bar == RT_NULL)
        return -RT_ENOMEM;

    lv_obj_set_size(status_bar, UI_SCREEN_WIDTH, UI_STATUS_BAR_HEIGHT);
    lv_obj_align(status_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(status_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(status_bar, 0, 0);
    lv_obj_set_style_radius(status_bar, 0, 0);
    lv_obj_set_style_pad_all(status_bar, 0, 0);
    lv_obj_set_style_shadow_width(status_bar, 0, 0);
    lv_obj_remove_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(status_bar, LV_OBJ_FLAG_CLICKABLE);

    time_label = ui_status_bar_create_label(status_bar, "--:--",
                                            &lv_font_montserrat_20);
    lv_obj_align(time_label, LV_ALIGN_LEFT_MID, 30, -1);

    right_group = lv_obj_create(status_bar);
    lv_obj_set_size(right_group, 300, UI_STATUS_BAR_HEIGHT);
    lv_obj_align(right_group, LV_ALIGN_RIGHT_MID, -18, 0);
    lv_obj_set_style_bg_opa(right_group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_group, 0, 0);
    lv_obj_set_style_radius(right_group, 0, 0);
    lv_obj_set_style_pad_all(right_group, 0, 0);
    lv_obj_set_style_pad_column(right_group, 12, 0);
    lv_obj_remove_flag(right_group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(right_group, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(right_group, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right_group, LV_FLEX_ALIGN_END,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    bluetooth_label = ui_status_bar_create_label(right_group,
                                                 LV_SYMBOL_BLUETOOTH,
                                                 &lv_font_montserrat_20);
    lv_obj_add_flag(bluetooth_label, LV_OBJ_FLAG_HIDDEN);

    wifi_label = ui_status_bar_create_label(right_group, LV_SYMBOL_WIFI,
                                            &lv_font_montserrat_20);
    lv_obj_add_flag(wifi_label, LV_OBJ_FLAG_HIDDEN);

    gps_label = ui_status_bar_create_label(right_group, LV_SYMBOL_GPS,
                                           &lv_font_montserrat_20);
    lv_obj_add_flag(gps_label, LV_OBJ_FLAG_HIDDEN);

    battery_icon_label = ui_status_bar_create_label(right_group,
                                                    LV_SYMBOL_BATTERY_EMPTY,
                                                    &lv_font_montserrat_20);

    battery_percent_label = ui_status_bar_create_label(
        right_group, "--%", &lv_font_montserrat_16);

    lv_obj_move_foreground(status_bar);
    return RT_EOK;
}

bool ui_status_bar_is_ready(void)
{
    return status_bar != RT_NULL;
}

void ui_status_bar_set_visible(bool visible)
{
    if (status_bar == RT_NULL)
        return;

    if (visible)
    {
        lv_obj_remove_flag(status_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(status_bar);
    }
    else
    {
        lv_obj_add_flag(status_bar, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_status_bar_bring_to_front(void)
{
    if (status_bar != RT_NULL)
        lv_obj_move_foreground(status_bar);
}

rt_err_t ui_status_bar_handle_event(const ui_event_t *event)
{
    if (event == RT_NULL || status_bar == RT_NULL)
        return -RT_EINVAL;

    switch (event->type)
    {
    case UI_EVENT_BATTERY_CHANGED:
        return ui_status_bar_set_battery(event->data);
    case UI_EVENT_BLUETOOTH_CHANGED:
        return ui_status_bar_set_connection(bluetooth_label, event->data);
    case UI_EVENT_WIFI_CHANGED:
        return ui_status_bar_set_connection(wifi_label, event->data);
    case UI_EVENT_GPS_CHANGED:
        return ui_status_bar_set_connection(gps_label, event->data);
    case UI_EVENT_TIME_CHANGED:
        return ui_status_bar_set_time(event->data);
    case UI_EVENT_VOLUME_CHANGED:
        return ui_status_bar_set_volume(event->data);
    case UI_EVENT_POMODORO_COMPLETE:
        return ui_status_bar_show_pomodoro_complete();
    case UI_EVENT_LOW_BATTERY_CHANGED:
        return ui_status_bar_set_low_battery(event->data);
    default:
        return RT_EOK;
    }
}
