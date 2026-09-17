#include "infrared_page.h"

#include <stdbool.h>
#include <stdint.h>

#include <lvgl.h>

#include "infrared_service.h"
#include "ui_config.h"
#include "ui_navigation.h"
#include "ui_status_bar.h"
#include "ui_theme.h"

#define INFRARED_HEADER_Y       (UI_STATUS_BAR_HEIGHT + 4)
#define INFRARED_KEY_COUNT      16U
#define INFRARED_KEY_COLUMNS     4U
#define INFRARED_UPDATE_MS     100U
#define INFRARED_SEND_INTERVAL_MS 1000U

typedef enum
{
    INFRARED_FIELD_ADDRESS = 0,
    INFRARED_FIELD_COMMAND,
} infrared_field_t;

static const char infrared_hex_keys[INFRARED_KEY_COUNT] = {
    '0', '1', '2', '3',
    '4', '5', '6', '7',
    '8', '9', 'A', 'B',
    'C', 'D', 'E', 'F',
};

static lv_obj_t *infrared_address_value;
static lv_obj_t *infrared_command_value;
static lv_obj_t *infrared_status_label;
static lv_obj_t *infrared_send_button;
static lv_obj_t *infrared_send_icon;
static lv_obj_t *infrared_send_label;
static lv_obj_t *infrared_field_buttons[2];
static lv_obj_t *infrared_field_labels[2];
static uint8_t infrared_address;
static uint8_t infrared_command;
static uint8_t infrared_nibble_count;
static infrared_field_t infrared_active_field = INFRARED_FIELD_ADDRESS;
static infrared_service_state_t infrared_last_state =
    INFRARED_SERVICE_STATE_UNAVAILABLE;
static bool infrared_auto_sending;
static uint32_t infrared_send_elapsed_ms;

static lv_obj_t *infrared_page_create_label(lv_obj_t *parent,
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

static void infrared_page_style_button(lv_obj_t *button,
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

static void infrared_page_set_status(const char *text, uint32_t color)
{
    if (infrared_status_label == RT_NULL)
        return;
    lv_label_set_text(infrared_status_label, text);
    lv_obj_set_style_text_color(infrared_status_label,
                                lv_color_hex(color), 0);
}

static void infrared_page_update_values(void)
{
    if (infrared_address_value != RT_NULL)
        lv_label_set_text_fmt(infrared_address_value, "0x%02X",
                              infrared_address);
    if (infrared_command_value != RT_NULL)
        lv_label_set_text_fmt(infrared_command_value, "0x%02X",
                              infrared_command);
}

static void infrared_page_update_field_style(void)
{
    uint32_t index;

    for (index = 0U; index < 2U; index++)
    {
        bool selected = index == (uint32_t)infrared_active_field;

        if (infrared_field_buttons[index] == RT_NULL)
            continue;
        lv_obj_set_style_bg_color(
            infrared_field_buttons[index],
            lv_color_hex(selected ? UI_COLOR_PRIMARY : UI_COLOR_PANEL), 0);
        lv_obj_set_style_text_color(
            infrared_field_labels[index],
            lv_color_hex(selected ? UI_COLOR_BACKGROUND : UI_COLOR_TEXT), 0);
    }
}

static void infrared_page_select_field(infrared_field_t field)
{
    infrared_active_field = field;
    infrared_nibble_count = 0U;
    infrared_page_update_field_style();
}

static void infrared_page_enter_digit(uint8_t digit)
{
    uint8_t *value = infrared_active_field == INFRARED_FIELD_ADDRESS
                         ? &infrared_address
                         : &infrared_command;

    if (infrared_nibble_count == 0U)
    {
        *value = digit;
        infrared_nibble_count = 1U;
    }
    else
    {
        *value = (uint8_t)((*value << 4) | digit);
        infrared_nibble_count = 0U;
        if (infrared_active_field == INFRARED_FIELD_ADDRESS)
            infrared_active_field = INFRARED_FIELD_COMMAND;
    }
    infrared_page_update_values();
    infrared_page_update_field_style();
}

static rt_err_t infrared_page_send(void)
{
    rt_err_t result = infrared_service_send_nec(
        infrared_address, infrared_command);

    if (result == RT_EOK)
        infrared_page_set_status("QUEUED", UI_COLOR_WARNING);
    else if (result == -RT_EFULL)
        infrared_page_set_status("BUSY", UI_COLOR_WARNING);
    else
        infrared_page_set_status("SEND ERROR", UI_COLOR_ERROR);
    return result;
}

static void infrared_page_update_send_button(void)
{
    uint32_t background = infrared_auto_sending ?
                          UI_COLOR_WARM : UI_COLOR_PRIMARY;
    uint32_t foreground = infrared_auto_sending ?
                          UI_COLOR_TEXT : UI_COLOR_BACKGROUND;

    if (infrared_send_button != RT_NULL)
    {
        lv_obj_set_style_bg_color(infrared_send_button,
                                  lv_color_hex(background), 0);
    }
    if (infrared_send_icon != RT_NULL)
    {
        lv_label_set_text(infrared_send_icon,
                          infrared_auto_sending ?
                          LV_SYMBOL_STOP : LV_SYMBOL_UPLOAD);
        lv_obj_set_style_text_color(infrared_send_icon,
                                    lv_color_hex(foreground), 0);
    }
    if (infrared_send_label != RT_NULL)
    {
        lv_label_set_text(infrared_send_label,
                          infrared_auto_sending ? "STOP" : "SEND");
        lv_obj_set_style_text_color(infrared_send_label,
                                    lv_color_hex(foreground), 0);
    }
}

static void infrared_page_stop_auto_send(bool show_status)
{
    infrared_auto_sending = false;
    infrared_send_elapsed_ms = 0U;
    infrared_page_update_send_button();
    if (show_status)
        infrared_page_set_status("STOPPED", UI_COLOR_MUTED);
}

static void infrared_page_back_clicked(lv_event_t *event)
{
    (void)event;
    ui_navigation_switch_async(UI_PAGE_HOME);
}

static void infrared_page_field_clicked(lv_event_t *event)
{
    infrared_page_select_field(
        (infrared_field_t)(uintptr_t)lv_event_get_user_data(event));
}

static void infrared_page_hex_clicked(lv_event_t *event)
{
    uint32_t index = (uint32_t)(uintptr_t)lv_event_get_user_data(event);

    if (index < INFRARED_KEY_COUNT)
        infrared_page_enter_digit((uint8_t)index);
}

static void infrared_page_send_event(lv_event_t *event)
{
    rt_err_t result;

    (void)event;
    if (infrared_auto_sending)
    {
        infrared_page_stop_auto_send(true);
        return;
    }

    result = infrared_page_send();
    if (result == RT_EOK)
    {
        infrared_auto_sending = true;
        infrared_send_elapsed_ms = 0U;
        infrared_page_update_send_button();
        infrared_page_set_status("AUTO 1 S", UI_COLOR_WARNING);
    }
}

static lv_obj_t *infrared_page_create_back_button(ui_page_t *page)
{
    lv_obj_t *button = lv_button_create(page->root);
    lv_obj_t *icon;

    lv_obj_set_size(button, 48, 44);
    lv_obj_set_pos(button, 12, INFRARED_HEADER_Y);
    infrared_page_style_button(button, UI_COLOR_BACKGROUND);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(button, infrared_page_back_clicked,
                        LV_EVENT_CLICKED, RT_NULL);
    icon = infrared_page_create_label(button, LV_SYMBOL_LEFT,
                                      &lv_font_montserrat_24,
                                      UI_COLOR_TEXT);
    lv_obj_center(icon);
    if (page->group != RT_NULL)
        lv_group_add_obj(page->group, button);
    return button;
}

static void infrared_page_create_header(ui_page_t *page)
{
    lv_obj_t *label;

    infrared_page_create_back_button(page);
    label = infrared_page_create_label(page->root, "IR REMOTE",
                                       &lv_font_montserrat_24,
                                       UI_COLOR_TEXT);
    lv_obj_set_pos(label, 72, INFRARED_HEADER_Y + 6);

    label = infrared_page_create_label(page->root, "NEC / 38 kHz",
                                       &lv_font_montserrat_14,
                                       UI_COLOR_SUCCESS);
    lv_obj_set_size(label, 160, 24);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(label, 302, INFRARED_HEADER_Y + 10);
}

static void infrared_page_create_summary(ui_page_t *page)
{
    lv_obj_t *panel = lv_obj_create(page->root);
    lv_obj_t *label;

    lv_obj_set_size(panel, UI_SCREEN_WIDTH - 32, 82);
    lv_obj_set_pos(panel, 16, 96);
    lv_obj_set_style_bg_color(panel, lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, 4, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    label = infrared_page_create_label(panel, "ADDRESS",
                                       &lv_font_montserrat_12,
                                       UI_COLOR_MUTED);
    lv_obj_set_pos(label, 20, 14);
    infrared_address_value = infrared_page_create_label(
        panel, "0x00", &lv_font_montserrat_24, UI_COLOR_TEXT);
    lv_obj_set_pos(infrared_address_value, 20, 36);

    label = infrared_page_create_label(panel, "COMMAND",
                                       &lv_font_montserrat_12,
                                       UI_COLOR_MUTED);
    lv_obj_set_pos(label, 170, 14);
    infrared_command_value = infrared_page_create_label(
        panel, "0x00", &lv_font_montserrat_24, UI_COLOR_TEXT);
    lv_obj_set_pos(infrared_command_value, 170, 36);

    infrared_status_label = infrared_page_create_label(
        panel, "READY", &lv_font_montserrat_14, UI_COLOR_SUCCESS);
    lv_obj_set_size(infrared_status_label, 116, 24);
    lv_obj_set_style_text_align(infrared_status_label,
                                LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(infrared_status_label, 312, 40);
}

static void infrared_page_create_field_buttons(ui_page_t *page)
{
    static const char *const names[2] = {"ADDRESS", "COMMAND"};
    uint32_t index;

    for (index = 0U; index < 2U; index++)
    {
        lv_obj_t *button = lv_button_create(page->root);

        infrared_field_buttons[index] = button;
        lv_obj_set_size(button, 220, 42);
        lv_obj_set_pos(button, 16 + (int32_t)index * 228, 190);
        infrared_page_style_button(button, UI_COLOR_PANEL);
        lv_obj_add_event_cb(button, infrared_page_field_clicked,
                            LV_EVENT_CLICKED, (void *)(uintptr_t)index);
        infrared_field_labels[index] = infrared_page_create_label(
            button, names[index], &lv_font_montserrat_14, UI_COLOR_TEXT);
        lv_obj_center(infrared_field_labels[index]);
        if (page->group != RT_NULL)
            lv_group_add_obj(page->group, button);
    }
    infrared_page_update_field_style();
}

static void infrared_page_create_hex_keys(ui_page_t *page)
{
    uint32_t index;

    for (index = 0U; index < INFRARED_KEY_COUNT; index++)
    {
        uint32_t row = index / INFRARED_KEY_COLUMNS;
        uint32_t column = index % INFRARED_KEY_COLUMNS;
        lv_obj_t *button = lv_button_create(page->root);
        lv_obj_t *label;
        char text[2] = {infrared_hex_keys[index], '\0'};

        lv_obj_set_size(button, 74, 42);
        lv_obj_set_pos(button, 16 + (int32_t)column * 82,
                       242 + (int32_t)row * 50);
        infrared_page_style_button(button, UI_COLOR_PANEL);
        lv_obj_add_event_cb(button, infrared_page_hex_clicked,
                            LV_EVENT_CLICKED, (void *)(uintptr_t)index);
        label = infrared_page_create_label(
            button, text, &lv_font_montserrat_18, UI_COLOR_TEXT);
        lv_obj_center(label);
        if (page->group != RT_NULL)
            lv_group_add_obj(page->group, button);
    }
}

static void infrared_page_create_send_button(ui_page_t *page)
{
    lv_obj_t *button = lv_button_create(page->root);
    lv_obj_t *icon;
    lv_obj_t *label;

    lv_obj_set_size(button, 112, 192);
    lv_obj_set_pos(button, 352, 242);
    infrared_page_style_button(button, UI_COLOR_PRIMARY);
    infrared_send_button = button;
    lv_obj_add_event_cb(button, infrared_page_send_event,
                        LV_EVENT_CLICKED, RT_NULL);
    icon = infrared_page_create_label(button, LV_SYMBOL_UPLOAD,
                                      &lv_font_montserrat_30,
                                      UI_COLOR_BACKGROUND);
    infrared_send_icon = icon;
    lv_obj_set_size(icon, 80, 40);
    lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(icon, 16, 54);
    label = infrared_page_create_label(button, "SEND",
                                       &lv_font_montserrat_16,
                                       UI_COLOR_BACKGROUND);
    infrared_send_label = label;
    lv_obj_set_size(label, 80, 26);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(label, 16, 104);
    if (page->group != RT_NULL)
        lv_group_add_obj(page->group, button);
}

static void infrared_page_update_state(void)
{
    infrared_service_state_t state = infrared_service_get_state();

    if (state == infrared_last_state)
        return;
    infrared_last_state = state;
    switch (state)
    {
    case INFRARED_SERVICE_STATE_READY:
        infrared_page_set_status(infrared_auto_sending ?
                                 "AUTO 1 S" : "READY",
                                 infrared_auto_sending ?
                                 UI_COLOR_WARNING : UI_COLOR_SUCCESS);
        break;
    case INFRARED_SERVICE_STATE_SENDING:
        infrared_page_set_status("SENDING", UI_COLOR_WARNING);
        break;
    case INFRARED_SERVICE_STATE_SENT:
        infrared_page_set_status(infrared_auto_sending ?
                                 "AUTO 1 S" : "SENT",
                                 infrared_auto_sending ?
                                 UI_COLOR_WARNING : UI_COLOR_SUCCESS);
        break;
    case INFRARED_SERVICE_STATE_ERROR:
        infrared_page_set_status("SEND ERROR", UI_COLOR_ERROR);
        break;
    case INFRARED_SERVICE_STATE_UNAVAILABLE:
    default:
        infrared_page_stop_auto_send(false);
        infrared_page_set_status("UNAVAILABLE", UI_COLOR_ERROR);
        break;
    }
}

static void infrared_page_on_create(ui_page_t *page)
{
    if (page == RT_NULL || page->root == RT_NULL)
        return;
    if (page->group != RT_NULL)
        lv_group_set_wrap(page->group, true);

    infrared_page_create_header(page);
    infrared_page_create_summary(page);
    infrared_page_create_field_buttons(page);
    infrared_page_create_hex_keys(page);
    infrared_page_create_send_button(page);
    infrared_nibble_count = 0U;
    infrared_active_field = INFRARED_FIELD_ADDRESS;
    infrared_auto_sending = false;
    infrared_send_elapsed_ms = 0U;
    infrared_last_state = INFRARED_SERVICE_STATE_UNAVAILABLE;
    infrared_page_update_values();
    infrared_page_update_field_style();
    infrared_page_update_send_button();
    infrared_page_update_state();
}

static void infrared_page_on_destroy(ui_page_t *page)
{
    (void)page;
    infrared_page_stop_auto_send(false);
    infrared_address_value = RT_NULL;
    infrared_command_value = RT_NULL;
    infrared_status_label = RT_NULL;
    infrared_send_button = RT_NULL;
    infrared_send_icon = RT_NULL;
    infrared_send_label = RT_NULL;
    infrared_field_buttons[0] = RT_NULL;
    infrared_field_buttons[1] = RT_NULL;
    infrared_field_labels[0] = RT_NULL;
    infrared_field_labels[1] = RT_NULL;
}

static void infrared_page_on_timer(ui_page_t *page)
{
    (void)page;
    infrared_page_update_state();
    if (!infrared_auto_sending)
        return;

    infrared_send_elapsed_ms += INFRARED_UPDATE_MS;
    if (infrared_send_elapsed_ms >= INFRARED_SEND_INTERVAL_MS)
    {
        rt_err_t result;

        infrared_send_elapsed_ms = 0U;
        result = infrared_page_send();
        if (result == RT_EOK)
            infrared_page_set_status("AUTO 1 S", UI_COLOR_WARNING);
    }
}

static void infrared_page_on_gesture(ui_page_t *page,
                                     ui_gesture_t gesture)
{
    (void)page;
    if (gesture == UI_GESTURE_RIGHT)
        ui_navigation_switch_async(UI_PAGE_HOME);
}

static void infrared_page_on_event(ui_page_t *page,
                                   const ui_event_t *event)
{
    uint32_t key;

    (void)page;
    if (event == RT_NULL || event->type != UI_EVENT_KEY)
        return;
    key = (uint32_t)event->data & UI_EVENT_KEY_CODE_MASK;
    if (key == LV_KEY_ESC || key == LV_KEY_HOME)
        ui_navigation_switch_async(UI_PAGE_HOME);
    else if (key >= '0' && key <= '9')
        infrared_page_enter_digit((uint8_t)(key - '0'));
    else if (key == '*')
        infrared_page_select_field(INFRARED_FIELD_ADDRESS);
    else if (key == '#')
        infrared_page_select_field(INFRARED_FIELD_COMMAND);
    else if (key == LV_KEY_BACKSPACE)
    {
        if (infrared_active_field == INFRARED_FIELD_ADDRESS)
            infrared_address = 0U;
        else
            infrared_command = 0U;
        infrared_nibble_count = 0U;
        infrared_page_update_values();
    }
}

rt_err_t infrared_page_register(void)
{
    const ui_page_t page = {
        .id = UI_PAGE_INFRARED,
        .app_id = UI_APP_NONE,
        .name = "NEC Infrared Page",
        .on_create = infrared_page_on_create,
        .on_destroy = infrared_page_on_destroy,
        .on_timer = infrared_page_on_timer,
        .on_gesture = infrared_page_on_gesture,
        .on_event = infrared_page_on_event,
        .timer_interval_ms = INFRARED_UPDATE_MS,
    };

    return ui_navigation_register_page(&page);
}
