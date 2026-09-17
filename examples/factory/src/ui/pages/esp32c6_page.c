#include "esp32c6_page.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <lvgl.h>

#include "communication_service.h"
#include "platform_psram.h"
#include "ui_config.h"
#include "ui_navigation.h"
#include "ui_status_bar.h"
#include "ui_theme.h"

#define ESP32C6_SIDE_PADDING   16
#define ESP32C6_HEADER_Y       (UI_STATUS_BAR_HEIGHT + 4)
#define ESP32C6_LINK_Y         88
#define ESP32C6_LINK_HEIGHT    54
#define ESP32C6_TERMINAL_Y     150
#define ESP32C6_TERMINAL_HEIGHT 174
#define ESP32C6_CONTENT_WIDTH  (UI_SCREEN_WIDTH - ESP32C6_SIDE_PADDING * 2)
#define ESP32C6_COMMAND_Y      356
#define ESP32C6_COMMAND_HEIGHT 52
#define ESP32C6_QUICK_Y        420
#define ESP32C6_QUICK_HEIGHT   44
#define ESP32C6_KEYBOARD_Y     20
#define ESP32C6_KEYBOARD_HEIGHT 292
#define ESP32C6_QUICK_COUNT    3U
#define ESP32C6_UPDATE_INTERVAL_MS 100U
#define ESP32C6_TRANSCRIPT_SIZE 2048U
#define ESP32C6_RESPONSE_ID_BASE 0xC600

static ui_page_t *esp32c6_page_owner;
static lv_obj_t *esp32c6_back_button;
static lv_obj_t *esp32c6_device_label;
static lv_obj_t *esp32c6_send_button;
static lv_obj_t *esp32c6_link_panel;
static lv_obj_t *esp32c6_link_badge;
static lv_obj_t *esp32c6_link_badge_text;
static lv_obj_t *esp32c6_terminal_panel;
static lv_obj_t *esp32c6_terminal_view;
static lv_obj_t *esp32c6_terminal_label;
static lv_obj_t *esp32c6_terminal_state_label;
static lv_obj_t *esp32c6_command_caption;
static lv_obj_t *esp32c6_command_input;
static lv_obj_t *esp32c6_quick_caption;
static lv_obj_t *esp32c6_quick_buttons[ESP32C6_QUICK_COUNT];
static lv_obj_t *esp32c6_keyboard;
static struct rt_mutex esp32c6_transcript_lock;
static bool esp32c6_transcript_lock_initialized;
static bool esp32c6_transcript_dirty;
static char *esp32c6_transcript;
static size_t esp32c6_transcript_length;
static uint32_t esp32c6_response_sequence;
static bool esp32c6_probe_sent;

static void esp32c6_page_add_main_group_objects(ui_page_t *page);
static void esp32c6_page_hide_keyboard(void);
static void esp32c6_page_send_command(const char *command);

static void esp32c6_page_append_transcript(const char *prefix,
                                           const char *text)
{
    char entry[320];
    size_t entry_length;

    if (!esp32c6_transcript_lock_initialized ||
            esp32c6_transcript == RT_NULL || prefix == RT_NULL ||
            text == RT_NULL)
    {
        return;
    }
    rt_snprintf(entry, sizeof(entry), "%s  %s\n", prefix, text);
    entry[sizeof(entry) - 1U] = '\0';
    entry_length = strlen(entry);

    rt_mutex_take(&esp32c6_transcript_lock, RT_WAITING_FOREVER);
    while (esp32c6_transcript_length + entry_length >=
            ESP32C6_TRANSCRIPT_SIZE)
    {
        char *line_end = strchr(esp32c6_transcript, '\n');
        size_t remove_length;

        if (line_end == RT_NULL)
        {
            esp32c6_transcript_length = 0U;
            esp32c6_transcript[0] = '\0';
            break;
        }
        remove_length = (size_t)(line_end - esp32c6_transcript) + 1U;
        memmove(esp32c6_transcript,
                esp32c6_transcript + remove_length,
                esp32c6_transcript_length - remove_length + 1U);
        esp32c6_transcript_length -= remove_length;
    }
    memcpy(esp32c6_transcript + esp32c6_transcript_length,
           entry, entry_length + 1U);
    esp32c6_transcript_length += entry_length;
    esp32c6_transcript_dirty = true;
    rt_mutex_release(&esp32c6_transcript_lock);
}

static void esp32c6_page_esp_response(int response_id,
                                      const char *response,
                                      int result)
{
    const char *prefix;

    (void)response_id;
    if (result == COMM_ESP_RESULT_URC)
        prefix = "URC";
    else if (result == COMM_ESP_RESULT_OK)
        prefix = "RX";
    else
        prefix = "ERR";
    esp32c6_page_append_transcript(
        prefix, response != RT_NULL && response[0] != '\0' ?
                    response : "(empty response)");
}

static lv_obj_t *esp32c6_page_create_label(lv_obj_t *parent,
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

static void esp32c6_page_style_container(lv_obj_t *object,
                                         uint32_t color,
                                         lv_opa_t opacity)
{
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(object, opacity, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_radius(object, 4, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_shadow_width(object, 0, 0);
}

static void esp32c6_page_back_clicked(lv_event_t *event)
{
    (void)event;
    if (esp32c6_keyboard != RT_NULL &&
            !lv_obj_has_flag(esp32c6_keyboard, LV_OBJ_FLAG_HIDDEN))
        esp32c6_page_hide_keyboard();
    else
        ui_navigation_switch_async(UI_PAGE_HOME);
}

static lv_obj_t *esp32c6_page_create_back_button(ui_page_t *page)
{
    lv_obj_t *button = lv_button_create(page->root);
    lv_obj_t *icon;

    lv_obj_set_size(button, 48, 44);
    lv_obj_set_pos(button, 12, ESP32C6_HEADER_Y);
    esp32c6_page_style_container(button, UI_COLOR_BACKGROUND,
                                 LV_OPA_TRANSP);
    ui_theme_apply_focus_style(button);
    lv_obj_add_event_cb(button, esp32c6_page_back_clicked,
                        LV_EVENT_CLICKED, RT_NULL);

    icon = esp32c6_page_create_label(button, LV_SYMBOL_LEFT,
                                     &lv_font_montserrat_24,
                                     UI_COLOR_TEXT);
    lv_obj_center(icon);
    return button;
}

static void esp32c6_page_create_header(ui_page_t *page)
{
    lv_obj_t *title;

    esp32c6_back_button = esp32c6_page_create_back_button(page);

    title = esp32c6_page_create_label(page->root, "ESP32-C6 AT",
                                      &lv_font_montserrat_24,
                                      UI_COLOR_TEXT);
    lv_obj_set_size(title, 244, 34);
    lv_obj_set_pos(title, 72, ESP32C6_HEADER_Y + 6);

    esp32c6_device_label = esp32c6_page_create_label(
        page->root, LV_SYMBOL_WIFI "  UART2",
        &lv_font_montserrat_14, UI_COLOR_WARNING);
    lv_obj_set_size(esp32c6_device_label, 140, 24);
    lv_obj_set_style_text_align(esp32c6_device_label,
                                LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(esp32c6_device_label, UI_SCREEN_WIDTH - 156,
                   ESP32C6_HEADER_Y + 10);

    if (page->group != RT_NULL)
        lv_group_add_obj(page->group, esp32c6_back_button);
}

static void esp32c6_page_create_link_panel(ui_page_t *page)
{
    lv_obj_t *icon;
    lv_obj_t *module;
    lv_obj_t *port;

    esp32c6_link_panel = lv_obj_create(page->root);
    lv_obj_set_size(esp32c6_link_panel, ESP32C6_CONTENT_WIDTH,
                    ESP32C6_LINK_HEIGHT);
    lv_obj_set_pos(esp32c6_link_panel, ESP32C6_SIDE_PADDING,
                   ESP32C6_LINK_Y);
    esp32c6_page_style_container(esp32c6_link_panel, UI_COLOR_PANEL,
                                 LV_OPA_COVER);
    lv_obj_remove_flag(esp32c6_link_panel, LV_OBJ_FLAG_SCROLLABLE);

    icon = esp32c6_page_create_label(esp32c6_link_panel, LV_SYMBOL_WIFI,
                                     &lv_font_montserrat_24,
                                     UI_COLOR_PRIMARY);
    lv_obj_set_pos(icon, 14, 13);

    module = esp32c6_page_create_label(esp32c6_link_panel, "ESP32-C6",
                                       &lv_font_montserrat_16,
                                       UI_COLOR_TEXT);
    lv_obj_set_pos(module, 50, 7);

    port = esp32c6_page_create_label(esp32c6_link_panel,
                                     "UART2 shared  /  115200 8N1",
                                     &lv_font_montserrat_12,
                                     UI_COLOR_MUTED);
    lv_obj_set_pos(port, 50, 31);

    esp32c6_link_badge = lv_obj_create(esp32c6_link_panel);
    lv_obj_set_size(esp32c6_link_badge, 106, 30);
    lv_obj_set_pos(esp32c6_link_badge,
                   ESP32C6_CONTENT_WIDTH - 120, 12);
    esp32c6_page_style_container(esp32c6_link_badge,
                                 UI_COLOR_TRACK, LV_OPA_COVER);
    lv_obj_remove_flag(esp32c6_link_badge, LV_OBJ_FLAG_SCROLLABLE);

    esp32c6_link_badge_text = esp32c6_page_create_label(
        esp32c6_link_badge, "WAITING", &lv_font_montserrat_12,
        UI_COLOR_WARNING);
    lv_obj_center(esp32c6_link_badge_text);
}

static void esp32c6_page_create_terminal(ui_page_t *page)
{
    lv_obj_t *section;
    lv_obj_t *separator;

    esp32c6_terminal_panel = lv_obj_create(page->root);
    lv_obj_set_size(esp32c6_terminal_panel, ESP32C6_CONTENT_WIDTH,
                    ESP32C6_TERMINAL_HEIGHT);
    lv_obj_set_pos(esp32c6_terminal_panel, ESP32C6_SIDE_PADDING,
                   ESP32C6_TERMINAL_Y);
    esp32c6_page_style_container(esp32c6_terminal_panel, UI_COLOR_PANEL,
                                 LV_OPA_COVER);
    lv_obj_remove_flag(esp32c6_terminal_panel, LV_OBJ_FLAG_SCROLLABLE);

    section = esp32c6_page_create_label(esp32c6_terminal_panel,
                                        "AT TRANSCRIPT",
                                        &lv_font_montserrat_12,
                                        UI_COLOR_MUTED);
    lv_obj_set_pos(section, 14, 10);

    esp32c6_terminal_state_label = esp32c6_page_create_label(
        esp32c6_terminal_panel, "LIVE", &lv_font_montserrat_12,
        UI_COLOR_SUCCESS);
    lv_obj_set_size(esp32c6_terminal_state_label, 80, 20);
    lv_obj_set_style_text_align(esp32c6_terminal_state_label,
                                LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(esp32c6_terminal_state_label,
                   ESP32C6_CONTENT_WIDTH - 94, 10);

    separator = lv_obj_create(esp32c6_terminal_panel);
    lv_obj_set_size(separator, ESP32C6_CONTENT_WIDTH - 28, 1);
    lv_obj_set_pos(separator, 14, 34);
    esp32c6_page_style_container(separator, UI_COLOR_TRACK,
                                 LV_OPA_COVER);

    esp32c6_terminal_view = lv_obj_create(esp32c6_terminal_panel);
    lv_obj_set_size(esp32c6_terminal_view,
                    ESP32C6_CONTENT_WIDTH - 20, 128);
    lv_obj_set_pos(esp32c6_terminal_view, 10, 38);
    esp32c6_page_style_container(esp32c6_terminal_view,
                                 UI_COLOR_PANEL, LV_OPA_TRANSP);
    lv_obj_set_style_pad_left(esp32c6_terminal_view, 4, 0);
    lv_obj_set_style_pad_right(esp32c6_terminal_view, 4, 0);
    lv_obj_set_scroll_dir(esp32c6_terminal_view, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(esp32c6_terminal_view,
                              LV_SCROLLBAR_MODE_AUTO);

    esp32c6_terminal_label = esp32c6_page_create_label(
        esp32c6_terminal_view, "", &lv_font_montserrat_14,
        UI_COLOR_TEXT);
    lv_obj_set_width(esp32c6_terminal_label,
                     ESP32C6_CONTENT_WIDTH - 36);
    lv_obj_set_height(esp32c6_terminal_label, LV_SIZE_CONTENT);
    lv_label_set_long_mode(esp32c6_terminal_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(esp32c6_terminal_label, 0, 0);
}

static void esp32c6_page_set_enabled(lv_obj_t *object, bool enabled)
{
    if (object == RT_NULL)
        return;
    if (enabled)
        lv_obj_remove_state(object, LV_STATE_DISABLED);
    else
        lv_obj_add_state(object, LV_STATE_DISABLED);
}

static void esp32c6_page_refresh_transcript(void)
{
    bool dirty;

    if (!esp32c6_transcript_lock_initialized ||
            esp32c6_transcript == RT_NULL ||
            esp32c6_terminal_label == RT_NULL)
    {
        return;
    }

    rt_mutex_take(&esp32c6_transcript_lock, RT_WAITING_FOREVER);
    dirty = esp32c6_transcript_dirty;
    if (dirty)
    {
        lv_label_set_text(esp32c6_terminal_label, esp32c6_transcript);
        esp32c6_transcript_dirty = false;
    }
    rt_mutex_release(&esp32c6_transcript_lock);

    if (dirty)
    {
        lv_obj_update_layout(esp32c6_terminal_view);
        lv_obj_scroll_to_y(esp32c6_terminal_view,
                           lv_obj_get_height(esp32c6_terminal_label),
                           LV_ANIM_OFF);
    }
}

static void esp32c6_page_update_link(void)
{
    communication_service_status_t status;
    bool available;
    bool enabled;
    uint32_t badge_color;
    uint32_t text_color;
    const char *badge_text;

    memset(&status, 0, sizeof(status));
    available = communication_service_get_status(&status) == RT_EOK &&
                status.esp_available;
    enabled = available && !status.esp_busy;

    if (!available)
    {
        badge_text = "OFFLINE";
        badge_color = UI_COLOR_ERROR;
        text_color = UI_COLOR_TEXT;
        lv_label_set_text(esp32c6_device_label,
                          LV_SYMBOL_WIFI "  OFFLINE");
        lv_obj_set_style_text_color(esp32c6_device_label,
                                    lv_color_hex(UI_COLOR_ERROR), 0);
    }
    else if (status.esp_busy)
    {
        badge_text = "AT BUSY";
        badge_color = UI_COLOR_WARNING;
        text_color = UI_COLOR_BACKGROUND;
        lv_label_set_text(esp32c6_device_label,
                          LV_SYMBOL_WIFI "  UART2: ESP");
        lv_obj_set_style_text_color(esp32c6_device_label,
                                    lv_color_hex(UI_COLOR_WARNING), 0);
    }
    else
    {
        badge_text = "READY";
        badge_color = UI_COLOR_SUCCESS;
        text_color = UI_COLOR_BACKGROUND;
        lv_label_set_text(esp32c6_device_label,
                          status.uart_active &&
                          status.active_device == COMM_DEVICE_ESP32C6 ?
                              LV_SYMBOL_WIFI "  UART2: ESP" :
                              LV_SYMBOL_WIFI "  UART2: GPS");
        lv_obj_set_style_text_color(esp32c6_device_label,
                                    lv_color_hex(UI_COLOR_SUCCESS), 0);
    }

    lv_obj_set_style_bg_color(esp32c6_link_badge,
                              lv_color_hex(badge_color), 0);
    lv_label_set_text(esp32c6_link_badge_text, badge_text);
    lv_obj_set_style_text_color(esp32c6_link_badge_text,
                                lv_color_hex(text_color), 0);
    lv_label_set_text(esp32c6_terminal_state_label,
                      !available ? "OFFLINE" :
                      (status.esp_busy ? "WAITING" : "LIVE"));
    lv_obj_set_style_text_color(esp32c6_terminal_state_label,
                                lv_color_hex(!available ? UI_COLOR_ERROR :
                                    (status.esp_busy ? UI_COLOR_WARNING :
                                                       UI_COLOR_SUCCESS)), 0);

    esp32c6_page_set_enabled(esp32c6_send_button, enabled);
    for (uint32_t index = 0U; index < ESP32C6_QUICK_COUNT; index++)
        esp32c6_page_set_enabled(esp32c6_quick_buttons[index], enabled);
}

static void esp32c6_page_send_command(const char *command)
{
    char wire_command[132];
    char error_text[48];
    const char *start = command;
    size_t length;
    int response_id;
    rt_err_t result;

    if (start == RT_NULL)
        return;
    while (*start == ' ' || *start == '\t' ||
            *start == '\r' || *start == '\n')
    {
        start++;
    }
    length = strlen(start);
    while (length > 0U &&
            (start[length - 1U] == ' ' || start[length - 1U] == '\t' ||
             start[length - 1U] == '\r' || start[length - 1U] == '\n'))
    {
        length--;
    }
    if (length == 0U || length > sizeof(wire_command) - 3U)
        return;

    memcpy(wire_command, start, length);
    wire_command[length] = '\0';
    esp32c6_page_append_transcript("TX", wire_command);
    wire_command[length++] = '\r';
    wire_command[length++] = '\n';
    response_id = ESP32C6_RESPONSE_ID_BASE +
                  (int)(esp32c6_response_sequence++ & 0x0FFFU);
    result = communication_service_send_esp_at(
        (const uint8_t *)wire_command, length, response_id);
    if (result != RT_EOK)
    {
        if (result == -RT_EBUSY)
            esp32c6_page_append_transcript("SYS", "UART2 busy");
        else if (result == -RT_ENOSYS)
            esp32c6_page_append_transcript("SYS", "ESP32-C6 unavailable");
        else
        {
            rt_snprintf(error_text, sizeof(error_text),
                        "send failed (%d)", result);
            esp32c6_page_append_transcript("SYS", error_text);
        }
    }
    esp32c6_page_refresh_transcript();
    esp32c6_page_update_link();
}

static void esp32c6_page_send_clicked(lv_event_t *event)
{
    (void)event;
    if (esp32c6_command_input != RT_NULL)
        esp32c6_page_send_command(
            lv_textarea_get_text(esp32c6_command_input));
}

static void esp32c6_page_quick_clicked(lv_event_t *event)
{
    const char *command = lv_event_get_user_data(event);

    if (esp32c6_command_input != RT_NULL && command != RT_NULL)
    {
        lv_textarea_set_text(esp32c6_command_input, command);
        esp32c6_page_send_command(command);
    }
}

static lv_obj_t *esp32c6_page_create_quick_command(lv_obj_t *parent,
                                                    ui_page_t *page,
                                                    lv_coord_t x,
                                                    lv_coord_t width,
                                                    const char *text)
{
    lv_obj_t *command = lv_button_create(parent);
    lv_obj_t *label;

    lv_obj_set_size(command, width, ESP32C6_QUICK_HEIGHT);
    lv_obj_set_pos(command, x, ESP32C6_QUICK_Y);
    esp32c6_page_style_container(command, UI_COLOR_PANEL, LV_OPA_COVER);
    lv_obj_remove_flag(command, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_opa(command, LV_OPA_40, LV_STATE_DISABLED);
    ui_theme_apply_focus_style(command);
    lv_obj_add_event_cb(command, esp32c6_page_quick_clicked,
                        LV_EVENT_CLICKED, (void *)text);

    label = esp32c6_page_create_label(command, text,
                                      &lv_font_montserrat_12,
                                      UI_COLOR_MUTED);
    lv_obj_center(label);
    (void)page;
    return command;
}

static void esp32c6_page_add_main_group_objects(ui_page_t *page)
{
    uint32_t index;

    if (page == RT_NULL || page->group == RT_NULL)
        return;

    lv_group_remove_all_objs(page->group);
    lv_group_add_obj(page->group, esp32c6_back_button);
    lv_group_add_obj(page->group, esp32c6_command_input);
    lv_group_add_obj(page->group, esp32c6_send_button);
    for (index = 0U; index < ESP32C6_QUICK_COUNT; index++)
        lv_group_add_obj(page->group, esp32c6_quick_buttons[index]);
}

static void esp32c6_page_hide_keyboard(void)
{
    if (esp32c6_keyboard == RT_NULL)
        return;

    lv_obj_add_flag(esp32c6_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(esp32c6_command_input, ESP32C6_SIDE_PADDING,
                   ESP32C6_COMMAND_Y);
    lv_obj_set_size(esp32c6_command_input, 342,
                    ESP32C6_COMMAND_HEIGHT);
    lv_obj_remove_flag(esp32c6_link_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(esp32c6_terminal_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(esp32c6_command_caption, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(esp32c6_send_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(esp32c6_quick_caption, LV_OBJ_FLAG_HIDDEN);
    for (uint32_t index = 0U; index < ESP32C6_QUICK_COUNT; index++)
        lv_obj_remove_flag(esp32c6_quick_buttons[index],
                           LV_OBJ_FLAG_HIDDEN);

    if (esp32c6_page_owner != RT_NULL &&
            esp32c6_page_owner->group != RT_NULL)
    {
        lv_group_set_editing(esp32c6_page_owner->group, false);
        esp32c6_page_add_main_group_objects(esp32c6_page_owner);
        lv_group_focus_obj(esp32c6_command_input);
    }
}

static void esp32c6_page_show_keyboard(void)
{
    if (esp32c6_keyboard == RT_NULL)
        return;

    lv_obj_add_flag(esp32c6_link_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(esp32c6_terminal_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(esp32c6_command_caption, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(esp32c6_send_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(esp32c6_quick_caption, LV_OBJ_FLAG_HIDDEN);
    for (uint32_t index = 0U; index < ESP32C6_QUICK_COUNT; index++)
        lv_obj_add_flag(esp32c6_quick_buttons[index], LV_OBJ_FLAG_HIDDEN);

    lv_obj_set_pos(esp32c6_command_input, ESP32C6_SIDE_PADDING, 78);
    lv_obj_set_size(esp32c6_command_input, ESP32C6_CONTENT_WIDTH, 42);
    lv_obj_remove_flag(esp32c6_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(esp32c6_command_input);
    lv_obj_move_foreground(esp32c6_keyboard);

    if (esp32c6_page_owner != RT_NULL &&
            esp32c6_page_owner->group != RT_NULL)
    {
        lv_group_remove_all_objs(esp32c6_page_owner->group);
        lv_group_add_obj(esp32c6_page_owner->group, esp32c6_keyboard);
        lv_group_focus_obj(esp32c6_keyboard);
        lv_group_set_editing(esp32c6_page_owner->group, true);
    }
}

static void esp32c6_page_keyboard_event(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);

    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL)
        esp32c6_page_hide_keyboard();
}

static void esp32c6_page_input_clicked(lv_event_t *event)
{
    (void)event;
    esp32c6_page_show_keyboard();
}

static void esp32c6_page_input_value_changed(lv_event_t *event)
{
    lv_obj_t *input = lv_event_get_target_obj(event);

    lv_obj_scroll_to_y(input, 0, LV_ANIM_OFF);
}

static void esp32c6_page_create_keyboard(ui_page_t *page)
{
    esp32c6_keyboard = lv_keyboard_create(page->root);
    lv_obj_set_size(esp32c6_keyboard, UI_SCREEN_WIDTH,
                    ESP32C6_KEYBOARD_HEIGHT);
    lv_obj_set_pos(esp32c6_keyboard, 0, ESP32C6_KEYBOARD_Y);
    lv_obj_set_style_bg_color(esp32c6_keyboard,
                              lv_color_hex(UI_COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(esp32c6_keyboard, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(esp32c6_keyboard, 4, 0);
    lv_obj_set_style_pad_row(esp32c6_keyboard, 4, 0);
    lv_obj_set_style_pad_column(esp32c6_keyboard, 3, 0);
    lv_obj_set_style_bg_color(esp32c6_keyboard,
                              lv_color_hex(UI_COLOR_PANEL),
                              LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(esp32c6_keyboard, LV_OPA_COVER,
                            LV_PART_ITEMS);
    lv_obj_set_style_border_color(esp32c6_keyboard,
                                  lv_color_hex(UI_COLOR_TRACK),
                                  LV_PART_ITEMS);
    lv_obj_set_style_border_width(esp32c6_keyboard, 1,
                                  LV_PART_ITEMS);
    lv_obj_set_style_radius(esp32c6_keyboard, 3, LV_PART_ITEMS);
    lv_obj_set_style_text_color(esp32c6_keyboard,
                                lv_color_hex(UI_COLOR_TEXT),
                                LV_PART_ITEMS);
    lv_obj_set_style_text_font(esp32c6_keyboard,
                               &lv_font_montserrat_16,
                               LV_PART_ITEMS);
    lv_obj_set_style_bg_color(esp32c6_keyboard,
                              lv_color_hex(UI_COLOR_PRIMARY),
                              LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(esp32c6_keyboard,
                                lv_color_hex(UI_COLOR_BACKGROUND),
                                LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(esp32c6_keyboard,
                              lv_color_hex(UI_COLOR_SECONDARY),
                              LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(esp32c6_keyboard,
                                lv_color_hex(UI_COLOR_BACKGROUND),
                                LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_keyboard_set_mode(esp32c6_keyboard, LV_KEYBOARD_MODE_TEXT_UPPER);
    lv_keyboard_set_textarea(esp32c6_keyboard, esp32c6_command_input);
    lv_obj_add_event_cb(esp32c6_keyboard, esp32c6_page_keyboard_event,
                        LV_EVENT_ALL, RT_NULL);
    lv_obj_add_flag(esp32c6_keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void esp32c6_page_create_command_area(ui_page_t *page)
{
    lv_obj_t *send_label;

    esp32c6_command_caption = esp32c6_page_create_label(
        page->root, "COMMAND", &lv_font_montserrat_12, UI_COLOR_MUTED);
    lv_obj_set_pos(esp32c6_command_caption, ESP32C6_SIDE_PADDING, 334);

    esp32c6_command_input = lv_textarea_create(page->root);
    lv_textarea_set_one_line(esp32c6_command_input, true);
    lv_textarea_set_max_length(esp32c6_command_input, 128U);
    lv_textarea_set_text(esp32c6_command_input, "AT+GMR");
    lv_textarea_set_placeholder_text(esp32c6_command_input,
                                     "Enter AT command...");
    lv_obj_set_size(esp32c6_command_input, 342,
                    ESP32C6_COMMAND_HEIGHT);
    lv_obj_set_pos(esp32c6_command_input, ESP32C6_SIDE_PADDING,
                   ESP32C6_COMMAND_Y);
    lv_obj_set_style_bg_color(esp32c6_command_input,
                              lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(esp32c6_command_input, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(esp32c6_command_input,
                                  lv_color_hex(UI_COLOR_TRACK), 0);
    lv_obj_set_style_border_width(esp32c6_command_input, 1, 0);
    lv_obj_set_style_radius(esp32c6_command_input, 4, 0);
    lv_obj_set_style_text_color(esp32c6_command_input,
                                lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(esp32c6_command_input,
                               &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_letter_space(esp32c6_command_input, 0, 0);
    lv_obj_set_style_pad_left(esp32c6_command_input, 14, 0);
    lv_obj_set_style_pad_right(esp32c6_command_input, 14, 0);
    lv_obj_set_style_pad_top(esp32c6_command_input, 10, 0);
    lv_obj_set_style_pad_bottom(esp32c6_command_input, 8, 0);
    lv_obj_set_scrollbar_mode(esp32c6_command_input,
                              LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(esp32c6_command_input, LV_DIR_HOR);
    lv_obj_remove_flag(esp32c6_command_input, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_remove_flag(esp32c6_command_input,
                       LV_OBJ_FLAG_SCROLL_MOMENTUM);
    ui_theme_apply_focus_style(esp32c6_command_input);
    lv_obj_add_event_cb(esp32c6_command_input,
                        esp32c6_page_input_clicked,
                        LV_EVENT_CLICKED, RT_NULL);
    lv_obj_add_event_cb(esp32c6_command_input,
                        esp32c6_page_input_value_changed,
                        LV_EVENT_VALUE_CHANGED, RT_NULL);

    esp32c6_send_button = lv_button_create(page->root);
    lv_obj_set_size(esp32c6_send_button, 96, ESP32C6_COMMAND_HEIGHT);
    lv_obj_set_pos(esp32c6_send_button,
                   UI_SCREEN_WIDTH - ESP32C6_SIDE_PADDING - 96,
                   ESP32C6_COMMAND_Y);
    esp32c6_page_style_container(esp32c6_send_button,
                                 UI_COLOR_PRIMARY, LV_OPA_COVER);
    ui_theme_apply_focus_style(esp32c6_send_button);
    lv_obj_set_style_opa(esp32c6_send_button, LV_OPA_40,
                         LV_STATE_DISABLED);
    lv_obj_add_event_cb(esp32c6_send_button,
                        esp32c6_page_send_clicked,
                        LV_EVENT_CLICKED, RT_NULL);
    send_label = esp32c6_page_create_label(esp32c6_send_button,
                                           LV_SYMBOL_RIGHT "  SEND",
                                           &lv_font_montserrat_14,
                                           UI_COLOR_BACKGROUND);
    lv_obj_center(send_label);

    esp32c6_quick_caption = esp32c6_page_create_label(
        page->root, "QUICK", &lv_font_montserrat_12, UI_COLOR_MUTED);
    lv_obj_set_size(esp32c6_quick_caption, 52, 20);
    lv_obj_set_pos(esp32c6_quick_caption, ESP32C6_SIDE_PADDING,
                   ESP32C6_QUICK_Y + 13);

    esp32c6_quick_buttons[0] = esp32c6_page_create_quick_command(
        page->root, page, 76, 86, "AT");
    esp32c6_quick_buttons[1] = esp32c6_page_create_quick_command(
        page->root, page, 170, 116, "AT+GMR");
    esp32c6_quick_buttons[2] = esp32c6_page_create_quick_command(
        page->root, page, 294, 170, "AT+CWMODE?");
}

static void esp32c6_page_on_create(ui_page_t *page)
{
    if (page == RT_NULL || page->root == RT_NULL)
        return;

    esp32c6_page_owner = page;
    esp32c6_transcript = t_sf32_psram_calloc(
        ESP32C6_TRANSCRIPT_SIZE, sizeof(*esp32c6_transcript));
    if (esp32c6_transcript_lock_initialized)
    {
        rt_mutex_take(&esp32c6_transcript_lock, RT_WAITING_FOREVER);
        if (esp32c6_transcript != RT_NULL)
            esp32c6_transcript[0] = '\0';
        esp32c6_transcript_length = 0U;
        esp32c6_transcript_dirty = esp32c6_transcript != RT_NULL;
        rt_mutex_release(&esp32c6_transcript_lock);
    }
    esp32c6_response_sequence = 0U;
    esp32c6_probe_sent = false;
    esp32c6_page_create_header(page);
    esp32c6_page_create_link_panel(page);
    esp32c6_page_create_terminal(page);
    esp32c6_page_create_command_area(page);
    esp32c6_page_create_keyboard(page);

    if (page->group != RT_NULL)
    {
        lv_group_set_wrap(page->group, true);
        esp32c6_page_add_main_group_objects(page);
        lv_group_focus_obj(esp32c6_back_button);
    }
}

static void esp32c6_page_on_enter(ui_page_t *page)
{
    communication_service_status_t status;
    rt_err_t result;

    (void)page;
    result = communication_service_select_device(COMM_DEVICE_ESP32C6);
    if (result != RT_EOK)
        esp32c6_page_append_transcript("SYS", "UART2 switch failed");

    result = communication_service_set_esp_callback(
        esp32c6_page_esp_response);
    if (result != RT_EOK)
        esp32c6_page_append_transcript("SYS", "callback unavailable");

    memset(&status, 0, sizeof(status));
    esp32c6_page_update_link();
    esp32c6_page_refresh_transcript();
    if (!esp32c6_probe_sent &&
            communication_service_get_status(&status) == RT_EOK &&
            status.esp_available && !status.esp_busy)
    {
        esp32c6_probe_sent = true;
        esp32c6_page_send_command("AT");
    }
}

static void esp32c6_page_on_destroy(ui_page_t *page)
{
    (void)page;
    esp32c6_page_owner = RT_NULL;
    esp32c6_back_button = RT_NULL;
    esp32c6_device_label = RT_NULL;
    esp32c6_send_button = RT_NULL;
    esp32c6_link_panel = RT_NULL;
    esp32c6_link_badge = RT_NULL;
    esp32c6_link_badge_text = RT_NULL;
    esp32c6_terminal_panel = RT_NULL;
    esp32c6_terminal_view = RT_NULL;
    esp32c6_terminal_label = RT_NULL;
    esp32c6_terminal_state_label = RT_NULL;
    esp32c6_command_caption = RT_NULL;
    esp32c6_command_input = RT_NULL;
    esp32c6_quick_caption = RT_NULL;
    for (uint32_t index = 0U; index < ESP32C6_QUICK_COUNT; index++)
        esp32c6_quick_buttons[index] = RT_NULL;
    esp32c6_keyboard = RT_NULL;
    if (esp32c6_transcript_lock_initialized)
        rt_mutex_take(&esp32c6_transcript_lock, RT_WAITING_FOREVER);
    t_sf32_psram_free(esp32c6_transcript);
    esp32c6_transcript = RT_NULL;
    esp32c6_transcript_length = 0U;
    esp32c6_transcript_dirty = false;
    if (esp32c6_transcript_lock_initialized)
        rt_mutex_release(&esp32c6_transcript_lock);
}

static void esp32c6_page_on_leave(ui_page_t *page)
{
    (void)page;
    communication_service_set_esp_callback(RT_NULL);
    communication_service_select_device(COMM_DEVICE_ESP32C6);
    esp32c6_page_hide_keyboard();
}

static void esp32c6_page_on_timer(ui_page_t *page)
{
    communication_service_status_t status;

    (void)page;
    esp32c6_page_refresh_transcript();
    esp32c6_page_update_link();
    memset(&status, 0, sizeof(status));
    if (!esp32c6_probe_sent &&
            communication_service_get_status(&status) == RT_EOK &&
            status.esp_available && !status.esp_busy)
    {
        esp32c6_probe_sent = true;
        esp32c6_page_send_command("AT");
    }
}

static void esp32c6_page_on_gesture(ui_page_t *page,
                                    ui_gesture_t gesture)
{
    if (gesture != UI_GESTURE_RIGHT)
        return;

    if (esp32c6_keyboard != RT_NULL &&
            !lv_obj_has_flag(esp32c6_keyboard, LV_OBJ_FLAG_HIDDEN))
        esp32c6_page_hide_keyboard();
    else
    {
        (void)page;
        ui_navigation_switch_async(UI_PAGE_HOME);
    }
}

static void esp32c6_page_on_event(ui_page_t *page,
                                  const ui_event_t *event)
{
    if (event == RT_NULL || event->type != UI_EVENT_KEY)
        return;

    if (event->data == LV_KEY_HOME)
        ui_navigation_switch_async(UI_PAGE_HOME);
    else if (event->data == LV_KEY_ESC)
        esp32c6_page_on_gesture(page, UI_GESTURE_RIGHT);
}

rt_err_t esp32c6_page_register(void)
{
    const ui_page_t page = {
        .id = UI_PAGE_ESP32C6,
        .app_id = UI_APP_NONE,
        .name = "ESP32-C6 AT Page",
        .on_create = esp32c6_page_on_create,
        .on_enter = esp32c6_page_on_enter,
        .on_leave = esp32c6_page_on_leave,
        .on_destroy = esp32c6_page_on_destroy,
        .on_timer = esp32c6_page_on_timer,
        .on_gesture = esp32c6_page_on_gesture,
        .on_event = esp32c6_page_on_event,
        .timer_interval_ms = ESP32C6_UPDATE_INTERVAL_MS,
    };

    if (!esp32c6_transcript_lock_initialized)
    {
        rt_err_t result = rt_mutex_init(&esp32c6_transcript_lock,
                                        "esp_ui", RT_IPC_FLAG_PRIO);

        if (result != RT_EOK)
            return result;
        esp32c6_transcript_lock_initialized = true;
    }
    return ui_navigation_register_page(&page);
}
