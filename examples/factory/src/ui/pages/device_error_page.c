#include "device_error_page.h"

#include "app_init.h"
#include "ui_context.h"
#include "ui_navigation.h"
#include "ui_status_bar.h"
#include "ui_theme.h"

#define DEVICE_ERROR_ROW_STEP 38
#define DEVICE_ERROR_LIST_Y   (UI_STATUS_BAR_HEIGHT + 78)
#define DEVICE_ERROR_LIST_H   270

typedef struct
{
    const char *name;
    const char *detail;
    uint32_t error_bit;
} device_error_definition_t;

static const device_error_definition_t error_definitions[] = {
    {"Battery / charger", "Initialization failed", APP_INIT_ERROR_BATTERY},
    {"TF card", "Storage unavailable", APP_INIT_ERROR_STORAGE},
    {"GPIO keys", "Initialization failed", APP_INIT_ERROR_BUTTONS},
    {"XL9555", "Device not found", APP_INIT_ERROR_XL9555},
    {"TCA8418", "Device not found", APP_INIT_ERROR_TCA8418},
    {"Joypad keys", "Input unavailable", APP_INIT_ERROR_JOYPAD_KEYS},
    {"ADS1115", "Joystick unavailable", APP_INIT_ERROR_ADS1115},
    {"WS2812", "Joypad RGB unavailable", APP_INIT_ERROR_WS2812},
    {"BME280", "Device not found", APP_INIT_ERROR_BME280},
    {"BHI260AP", "Device not found", APP_INIT_ERROR_BHI260AP},
    {"SX1262", "LoRa radio not found", APP_INIT_ERROR_SX1262},
    {"AW21009", "Keyboard LEDs unavailable", APP_INIT_ERROR_AW21009},
    {"AW86224", "Haptic device unavailable", APP_INIT_ERROR_AW86224},
    {"Infrared", "Transmitter unavailable", APP_INIT_ERROR_INFRARED},
    {"AUDPRC", "Processor unavailable", APP_INIT_ERROR_AUDPRC},
    {"AUDCODEC", "Codec unavailable", APP_INIT_ERROR_AUDCODEC},
    {"Audio manager", "Initialization failed", APP_INIT_ERROR_AUDIO_MGR},
    {"UART2", "Shared bus unavailable", APP_INIT_ERROR_UART2},
    {"UART mux", "Initialization failed", APP_INIT_ERROR_UART_MUX},
    {"L76K", "GPS data not detected", APP_INIT_ERROR_L76K},
    {"ESP32-C6", "AT response not detected", APP_INIT_ERROR_ESP32C6},
};

#define DEVICE_ERROR_ROW_COUNT \
    (sizeof(error_definitions) / sizeof(error_definitions[0]))

static lv_obj_t *device_error_page_create_label(lv_obj_t *parent,
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

static uint32_t device_error_page_count(uint32_t error_mask)
{
    uint32_t count = 0;
    rt_size_t index;

    for (index = 0; index < DEVICE_ERROR_ROW_COUNT; index++)
    {
        if ((error_mask & error_definitions[index].error_bit) != 0U)
            count++;
    }

    return count;
}

static void device_error_page_home_clicked(lv_event_t *event)
{
    (void)event;
    ui_navigation_switch_async(UI_PAGE_HOME);
}

static void device_error_page_add_row(lv_obj_t *parent,
                                      const device_error_definition_t *error,
                                      uint32_t row_index)
{
    lv_obj_t *row;
    lv_obj_t *indicator;
    lv_obj_t *name;
    lv_obj_t *detail;

    row = lv_obj_create(parent);
    lv_obj_set_size(row, 432, 32);
    lv_obj_set_pos(row, 0,
                   (lv_coord_t)row_index * DEVICE_ERROR_ROW_STEP);
    lv_obj_set_style_bg_color(row, lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 4, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    indicator = lv_obj_create(row);
    lv_obj_set_size(indicator, 4, 32);
    lv_obj_set_pos(indicator, 0, 0);
    lv_obj_set_style_bg_color(indicator, lv_color_hex(UI_COLOR_ERROR), 0);
    lv_obj_set_style_bg_opa(indicator, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(indicator, 0, 0);
    lv_obj_set_style_radius(indicator, 0, 0);
    lv_obj_set_style_pad_all(indicator, 0, 0);
    lv_obj_remove_flag(indicator, LV_OBJ_FLAG_SCROLLABLE);

    name = device_error_page_create_label(row, error->name,
                                          &lv_font_montserrat_16,
                                          UI_COLOR_TEXT);
    lv_obj_set_size(name, 180, 22);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 16, 0);

    detail = device_error_page_create_label(row, error->detail,
                                            &lv_font_montserrat_14,
                                            UI_COLOR_ERROR);
    lv_obj_set_size(detail, 210, 20);
    lv_obj_set_style_text_align(detail, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(detail, LV_ALIGN_RIGHT_MID, -12, 0);
}

static void device_error_page_on_create(ui_page_t *page)
{
    ui_context_t context;
    uint32_t error_mask = 0U;
    uint32_t error_count;
    uint32_t row_index = 0U;
    rt_size_t index;
    lv_obj_t *title;
    lv_obj_t *summary;
    lv_obj_t *error_list;
    lv_obj_t *home_button;
    lv_obj_t *home_label;

    if (page == RT_NULL || page->root == RT_NULL)
        return;

    if (ui_context_get(&context) == RT_EOK)
        error_mask = context.init_error_mask;
    error_count = device_error_page_count(error_mask);

    lv_obj_set_scrollbar_mode(page->root, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(page->root, LV_DIR_NONE);

    title = device_error_page_create_label(page->root,
                                           "Device Check Failed",
                                           &lv_font_montserrat_30,
                                           UI_COLOR_ERROR);
    lv_obj_set_size(title, 432, 40);
    lv_obj_set_pos(title, 24, UI_STATUS_BAR_HEIGHT + 8);

    summary = device_error_page_create_label(page->root, "",
                                             &lv_font_montserrat_16,
                                             UI_COLOR_MUTED);
    lv_label_set_text_fmt(summary, "%u device%s unavailable",
                          (unsigned int)error_count,
                          error_count == 1U ? "" : "s");
    lv_obj_set_size(summary, 432, 24);
    lv_obj_set_pos(summary, 24, UI_STATUS_BAR_HEIGHT + 48);

    error_list = lv_obj_create(page->root);
    lv_obj_set_size(error_list, 432, DEVICE_ERROR_LIST_H);
    lv_obj_set_pos(error_list, 24, DEVICE_ERROR_LIST_Y);
    lv_obj_set_style_bg_opa(error_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(error_list, 0, 0);
    lv_obj_set_style_pad_all(error_list, 0, 0);
    lv_obj_set_scroll_dir(error_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(error_list, LV_SCROLLBAR_MODE_AUTO);

    for (index = 0; index < DEVICE_ERROR_ROW_COUNT; index++)
    {
        if ((error_mask & error_definitions[index].error_bit) == 0U)
            continue;

        device_error_page_add_row(error_list, &error_definitions[index],
                                  row_index);
        row_index++;
    }

    home_button = lv_button_create(page->root);
    lv_obj_set_size(home_button, 176, 48);
    lv_obj_set_pos(home_button, 280, 408);
    lv_obj_set_style_bg_color(home_button,
                              lv_color_hex(UI_COLOR_PRIMARY), 0);
    lv_obj_set_style_bg_opa(home_button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(home_button, 0, 0);
    lv_obj_set_style_radius(home_button, 4, 0);
    lv_obj_set_style_shadow_width(home_button, 12, 0);
    lv_obj_set_style_shadow_offset_y(home_button, 10, 0);
    lv_obj_set_style_shadow_color(home_button, lv_color_hex(UI_COLOR_TRACK), 0);
    lv_obj_set_style_shadow_opa(home_button, LV_OPA_40, 0);
    ui_theme_apply_focus_style(home_button);
    lv_obj_add_event_cb(home_button, device_error_page_home_clicked,
                        LV_EVENT_CLICKED, RT_NULL);

    home_label = lv_label_create(home_button);
    lv_label_set_text(home_label, LV_SYMBOL_HOME "  Home");
    lv_obj_set_style_text_color(home_label,
                                lv_color_hex(UI_COLOR_BACKGROUND), 0);
    lv_obj_center(home_label);

    if (page->group != RT_NULL)
        lv_group_add_obj(page->group, home_button);
}

rt_err_t device_error_page_register(void)
{
    const ui_page_t page = {
        .id = UI_PAGE_DEVICE_ERROR,
        .app_id = UI_APP_NONE,
        .name = "Device Error Page",
        .on_create = device_error_page_on_create,
    };

    return ui_navigation_register_page(&page);
}
