#include "input_page.h"

#include <stdbool.h>
#include <stdint.h>

#include <lvgl.h>

#include "input_service.h"
#include "ui_config.h"
#include "ui_navigation.h"
#include "ui_status_bar.h"
#include "ui_theme.h"

#if defined(FACTORY_HW_USING_KEYBOARD)
#include "tca8418.h"
#endif

#define INPUT_PAGE_SIDE_PADDING 16
#define INPUT_PAGE_HEADER_Y (UI_STATUS_BAR_HEIGHT + 4)
#define INPUT_PAGE_CONTENT_WIDTH \
    (UI_SCREEN_WIDTH - INPUT_PAGE_SIDE_PADDING * 2)
#define INPUT_PAGE_TIMER_MS 50U
#define INPUT_PAGE_KEY_FLASH_MS 220U

#define INPUT_PAGE_EVENT_SOURCE(data) \
    ((input_source_t)(((data) >> UI_EVENT_INPUT_SOURCE_SHIFT) & 0xFFU))
#define INPUT_PAGE_EVENT_ACTION(data) \
    ((input_action_t)(((data) >> UI_EVENT_INPUT_ACTION_SHIFT) & 0xFFU))
#define INPUT_PAGE_EVENT_CODE(data) \
    ((int32_t)((data) & UI_EVENT_INPUT_CODE_MASK))

static lv_obj_t *input_back_button;
static lv_obj_t *input_last_event_label;

#if defined(FACTORY_HW_USING_KEYBOARD)
#define INPUT_KEYBOARD_KEY_COUNT 20U
#define INPUT_KEYBOARD_COLUMNS 4U

typedef struct
{
    int32_t code;
    const char *label;
} input_keyboard_key_t;

static const input_keyboard_key_t input_keyboard_keys[INPUT_KEYBOARD_KEY_COUNT] = {
    {KEY_PREV, "PREV"},
    {KEY_ESC, "ESC"},
    {KEY_HOME, "HOME"},
    {KEY_MAIL, "MAIL"},
    {KEY_ENTER, "ENTER"},
    {KEY_1, "1"},
    {KEY_2, "2"},
    {KEY_3, "3"},
    {KEY_NEXT, "NEXT"},
    {KEY_4, "4"},
    {KEY_5, "5"},
    {KEY_6, "6"},
    {KEY_CLOSE, "DEL"},
    {KEY_7, "7"},
    {KEY_8, "8"},
    {KEY_9, "9"},
    {KEY_STOP, "STOP"},
    {KEY_MUL, "*"},
    {KEY_0, "0"},
    {KEY_WELL, "#"},
};

static lv_obj_t *input_keyboard_buttons[INPUT_KEYBOARD_KEY_COUNT];
static lv_obj_t *input_keyboard_labels[INPUT_KEYBOARD_KEY_COUNT];
static rt_tick_t input_keyboard_release_ticks[INPUT_KEYBOARD_KEY_COUNT];
#elif defined(FACTORY_HW_USING_JOYPAD)
#define INPUT_JOYPAD_KEY_COUNT 7U

static lv_obj_t *input_joypad_buttons[INPUT_JOYPAD_KEY_COUNT];
static lv_obj_t *input_joypad_labels[INPUT_JOYPAD_KEY_COUNT];
static lv_obj_t *input_joypad_x_value;
static lv_obj_t *input_joypad_y_value;
static lv_obj_t *input_joypad_x_bar;
static lv_obj_t *input_joypad_y_bar;
static uint16_t input_joypad_last_key_state = 0xFFFFU;
#endif

static lv_obj_t *input_page_create_label(lv_obj_t *parent,
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

static void input_page_style_container(lv_obj_t *object,
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

static void input_page_set_key_state(lv_obj_t *button,
                                     lv_obj_t *label,
                                     bool pressed)
{
    if (button == RT_NULL || label == RT_NULL)
        return;

    lv_obj_set_style_bg_color(
        button,
        lv_color_hex(pressed ? UI_COLOR_PRIMARY : UI_COLOR_PANEL), 0);
    lv_obj_set_style_text_color(
        label,
        lv_color_hex(pressed ? UI_COLOR_BACKGROUND : UI_COLOR_TEXT), 0);
}

static void input_page_back_clicked(lv_event_t *event)
{
    (void)event;
    ui_navigation_switch_async(UI_PAGE_HOME);
}

static void input_page_create_header(ui_page_t *page)
{
    lv_obj_t *title;
    lv_obj_t *profile;
    lv_obj_t *icon;

    input_back_button = lv_button_create(page->root);
    lv_obj_set_size(input_back_button, 48, 44);
    lv_obj_set_pos(input_back_button, 12, INPUT_PAGE_HEADER_Y);
    input_page_style_container(input_back_button, UI_COLOR_BACKGROUND,
                               LV_OPA_TRANSP);
    ui_theme_apply_focus_style(input_back_button);
    lv_obj_add_event_cb(input_back_button, input_page_back_clicked,
                        LV_EVENT_CLICKED, RT_NULL);

    icon = input_page_create_label(input_back_button, LV_SYMBOL_LEFT,
                                   &lv_font_montserrat_24, UI_COLOR_TEXT);
    lv_obj_center(icon);

    title = input_page_create_label(page->root, "INPUT TEST",
                                    &lv_font_montserrat_24,
                                    UI_COLOR_TEXT);
    lv_obj_set_size(title, 230, 34);
    lv_obj_set_pos(title, 72, INPUT_PAGE_HEADER_Y + 6);

#if defined(FACTORY_HW_USING_KEYBOARD)
    profile = input_page_create_label(page->root,
                                      LV_SYMBOL_KEYBOARD "  KEYBOARD",
                                      &lv_font_montserrat_14,
                                      UI_COLOR_PRIMARY);
#elif defined(FACTORY_HW_USING_JOYPAD)
    profile = input_page_create_label(page->root, "JOYPAD",
                                      &lv_font_montserrat_14,
                                      UI_COLOR_PRIMARY);
#else
    profile = input_page_create_label(page->root, "NO PROFILE",
                                      &lv_font_montserrat_14,
                                      UI_COLOR_WARNING);
#endif
    lv_obj_set_size(profile, 160, 24);
    lv_obj_set_style_text_align(profile, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(profile, UI_SCREEN_WIDTH - 176,
                   INPUT_PAGE_HEADER_Y + 10);

    if (page->group != RT_NULL)
        lv_group_add_obj(page->group, input_back_button);
}

static void input_page_create_event_panel(ui_page_t *page,
                                          const char *device_text)
{
    lv_obj_t *panel = lv_obj_create(page->root);
    lv_obj_t *device;

    lv_obj_set_size(panel, INPUT_PAGE_CONTENT_WIDTH, 44);
    lv_obj_set_pos(panel, INPUT_PAGE_SIDE_PADDING, 88);
    input_page_style_container(panel, UI_COLOR_PANEL, LV_OPA_COVER);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    device = input_page_create_label(panel, device_text,
                                     &lv_font_montserrat_12,
                                     UI_COLOR_MUTED);
    lv_obj_set_pos(device, 12, 13);

    input_last_event_label = input_page_create_label(
        panel, "WAITING FOR INPUT", &lv_font_montserrat_12,
        UI_COLOR_WARNING);
    lv_obj_set_size(input_last_event_label, 250, 20);
    lv_obj_set_style_text_align(input_last_event_label,
                                LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(input_last_event_label,
                   INPUT_PAGE_CONTENT_WIDTH - 262, 13);
}

#if defined(FACTORY_HW_USING_KEYBOARD)
static int32_t input_page_find_keyboard_key(int32_t code)
{
    uint32_t index;

    for (index = 0U; index < INPUT_KEYBOARD_KEY_COUNT; index++)
    {
        if (input_keyboard_keys[index].code == code)
            return (int32_t)index;
    }
    return -1;
}

static void input_page_create_keyboard(ui_page_t *page)
{
    uint32_t index;

    input_page_create_event_panel(page, "TCA8418  /  5 x 4 MATRIX");

    for (index = 0U; index < INPUT_KEYBOARD_KEY_COUNT; index++)
    {
        uint32_t row = index / INPUT_KEYBOARD_COLUMNS;
        uint32_t column = index % INPUT_KEYBOARD_COLUMNS;
        lv_obj_t *button = lv_obj_create(page->root);
        lv_obj_t *label;

        lv_obj_set_size(button, 106, 50);
        lv_obj_set_pos(button,
                       INPUT_PAGE_SIDE_PADDING + (lv_coord_t)column * 114,
                       140 + (lv_coord_t)row * 56);
        input_page_style_container(button, UI_COLOR_PANEL, LV_OPA_COVER);
        lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(button, LV_OBJ_FLAG_CLICKABLE);

        label = input_page_create_label(button,
                                        input_keyboard_keys[index].label,
                                        &lv_font_montserrat_14,
                                        UI_COLOR_TEXT);
        lv_obj_center(label);
        input_keyboard_buttons[index] = button;
        input_keyboard_labels[index] = label;
        input_keyboard_release_ticks[index] = 0;
    }
}

static void input_page_handle_keyboard_event(int32_t code,
                                             input_action_t action)
{
    int32_t index = input_page_find_keyboard_key(code);

    if (index < 0)
        return;

    input_page_set_key_state(input_keyboard_buttons[index],
                             input_keyboard_labels[index], true);
    input_keyboard_release_ticks[index] =
        rt_tick_get() + rt_tick_from_millisecond(INPUT_PAGE_KEY_FLASH_MS);
    lv_label_set_text_fmt(input_last_event_label, "%s  /  CODE %d",
                          input_keyboard_keys[index].label, (int)code);
    lv_obj_set_style_text_color(
        input_last_event_label,
        lv_color_hex(action == INPUT_ACTION_LONG_PRESSED
                         ? UI_COLOR_SECONDARY
                         : UI_COLOR_SUCCESS),
        0);
}

static void input_page_update_keyboard(void)
{
    rt_tick_t now = rt_tick_get();
    uint32_t index;

    for (index = 0U; index < INPUT_KEYBOARD_KEY_COUNT; index++)
    {
        if (input_keyboard_release_ticks[index] != 0U &&
            (rt_int32_t)(now - input_keyboard_release_ticks[index]) >= 0)
        {
            input_page_set_key_state(input_keyboard_buttons[index],
                                     input_keyboard_labels[index], false);
            input_keyboard_release_ticks[index] = 0;
        }
    }
}
#elif defined(FACTORY_HW_USING_JOYPAD)
static void input_page_create_axis_panel(lv_obj_t *parent,
                                         lv_coord_t x,
                                         const char *axis,
                                         lv_obj_t **value,
                                         lv_obj_t **bar)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_t *name;

    lv_obj_set_size(panel, 216, 82);
    lv_obj_set_pos(panel, x, 140);
    input_page_style_container(panel, UI_COLOR_PANEL, LV_OPA_COVER);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    name = input_page_create_label(panel, axis, &lv_font_montserrat_14,
                                   UI_COLOR_PRIMARY);
    lv_obj_set_pos(name, 12, 10);

    *value = input_page_create_label(panel, "---- mV",
                                     &lv_font_montserrat_20,
                                     UI_COLOR_TEXT);
    lv_obj_set_size(*value, 160, 28);
    lv_obj_set_style_text_align(*value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(*value, 44, 8);

    *bar = lv_bar_create(panel);
    lv_obj_set_size(*bar, 192, 8);
    lv_obj_set_pos(*bar, 12, 56);
    lv_bar_set_range(*bar, 0, 3300);
    lv_bar_set_value(*bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(*bar, lv_color_hex(UI_COLOR_TRACK),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(*bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(*bar, lv_color_hex(UI_COLOR_PRIMARY),
                              LV_PART_INDICATOR);
    lv_obj_set_style_radius(*bar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(*bar, 4, LV_PART_INDICATOR);
}

static void input_page_create_joypad_key(ui_page_t *page,
                                         uint32_t index,
                                         lv_coord_t x,
                                         lv_coord_t y)
{
    lv_obj_t *button = lv_obj_create(page->root);
    lv_obj_t *name;
    lv_obj_t *pin;

    lv_obj_set_size(button, 106, 72);
    lv_obj_set_pos(button, x, y);
    input_page_style_container(button, UI_COLOR_PANEL, LV_OPA_COVER);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_CLICKABLE);

    name = input_page_create_label(button, "",
                                   &lv_font_montserrat_18,
                                   UI_COLOR_TEXT);
    lv_label_set_text_fmt(name, "K%u", (unsigned int)(index + 1U));
    lv_obj_set_size(name, 82, 25);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(name, 12, 12);

    pin = input_page_create_label(button, "",
                                  &lv_font_montserrat_12,
                                  UI_COLOR_MUTED);
    lv_label_set_text_fmt(pin, "P0.%u", (unsigned int)index);
    lv_obj_set_size(pin, 82, 20);
    lv_obj_set_style_text_align(pin, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(pin, 12, 40);

    input_joypad_buttons[index] = button;
    input_joypad_labels[index] = name;
}

static void input_page_create_joypad(ui_page_t *page)
{
    uint32_t index;

    input_page_create_event_panel(page, "ADS1115 + XL9555");
    input_page_create_axis_panel(page->root, INPUT_PAGE_SIDE_PADDING,
                                 "X", &input_joypad_x_value,
                                 &input_joypad_x_bar);
    input_page_create_axis_panel(page->root, 248, "Y",
                                 &input_joypad_y_value,
                                 &input_joypad_y_bar);

    for (index = 0U; index < 4U; index++)
    {
        input_page_create_joypad_key(
            page, index,
            INPUT_PAGE_SIDE_PADDING + (lv_coord_t)index * 114, 232);
    }
    for (index = 4U; index < INPUT_JOYPAD_KEY_COUNT; index++)
    {
        input_page_create_joypad_key(
            page, index,
            73 + (lv_coord_t)(index - 4U) * 122, 314);
    }
}

static void input_page_update_joypad(void)
{
    input_joypad_state_t state;
    uint32_t index;

    if (input_service_get_joypad_state(&state) != RT_EOK)
        return;

    if (state.axis_valid)
    {
        lv_label_set_text_fmt(input_joypad_x_value, "%d mV",
                              (int)state.x_mv);
        lv_label_set_text_fmt(input_joypad_y_value, "%d mV",
                              (int)state.y_mv);
        lv_bar_set_value(input_joypad_x_bar, state.x_mv, LV_ANIM_OFF);
        lv_bar_set_value(input_joypad_y_bar, state.y_mv, LV_ANIM_OFF);
    }

    if (!state.keys_valid || state.key_state == input_joypad_last_key_state)
        return;

    for (index = 0U; index < INPUT_JOYPAD_KEY_COUNT; index++)
    {
        bool pressed = (state.key_state & (uint16_t)(1U << index)) != 0U;

        input_page_set_key_state(input_joypad_buttons[index],
                                 input_joypad_labels[index], pressed);
    }
    input_joypad_last_key_state = state.key_state;
}

static void input_page_handle_joypad_event(int32_t code,
                                           input_action_t action)
{
    if (code < 0 || code >= (int32_t)INPUT_JOYPAD_KEY_COUNT)
        return;

    lv_label_set_text_fmt(input_last_event_label, "K%d  /  P0.%d  /  %s",
                          (int)code + 1, (int)code,
                          action == INPUT_ACTION_PRESSED ? "DOWN" : "UP");
    lv_obj_set_style_text_color(
        input_last_event_label,
        lv_color_hex(action == INPUT_ACTION_PRESSED
                         ? UI_COLOR_SUCCESS
                         : UI_COLOR_MUTED),
        0);
}
#endif

static void input_page_on_create(ui_page_t *page)
{
    if (page == RT_NULL || page->root == RT_NULL)
        return;

    input_page_create_header(page);
#if defined(FACTORY_HW_USING_KEYBOARD)
    input_page_create_keyboard(page);
#elif defined(FACTORY_HW_USING_JOYPAD)
    input_page_create_joypad(page);
#else
    input_page_create_event_panel(page, "INPUT PROFILE NOT CONFIGURED");
#endif
}

static void input_page_on_enter(ui_page_t *page)
{
    (void)page;
#if defined(FACTORY_HW_USING_JOYPAD)
    input_joypad_last_key_state = 0xFFFFU;
    input_page_update_joypad();
#endif
}

static void input_page_on_destroy(ui_page_t *page)
{
    (void)page;
    input_back_button = RT_NULL;
    input_last_event_label = RT_NULL;
#if defined(FACTORY_HW_USING_KEYBOARD)
    rt_memset(input_keyboard_buttons, 0, sizeof(input_keyboard_buttons));
    rt_memset(input_keyboard_labels, 0, sizeof(input_keyboard_labels));
    rt_memset(input_keyboard_release_ticks, 0,
              sizeof(input_keyboard_release_ticks));
#elif defined(FACTORY_HW_USING_JOYPAD)
    rt_memset(input_joypad_buttons, 0, sizeof(input_joypad_buttons));
    rt_memset(input_joypad_labels, 0, sizeof(input_joypad_labels));
    input_joypad_x_value = RT_NULL;
    input_joypad_y_value = RT_NULL;
    input_joypad_x_bar = RT_NULL;
    input_joypad_y_bar = RT_NULL;
#endif
}

static void input_page_on_timer(ui_page_t *page)
{
    (void)page;
#if defined(FACTORY_HW_USING_KEYBOARD)
    input_page_update_keyboard();
#elif defined(FACTORY_HW_USING_JOYPAD)
    input_page_update_joypad();
#endif
}

static void input_page_on_gesture(ui_page_t *page, ui_gesture_t gesture)
{
    (void)page;
    if (gesture == UI_GESTURE_RIGHT)
        ui_navigation_switch_async(UI_PAGE_HOME);
}

static void input_page_on_event(ui_page_t *page,
                                const ui_event_t *event)
{
    uintptr_t data;
    input_source_t source;
    input_action_t action;
    int32_t code;

    (void)page;
    if (event == RT_NULL || event->type != UI_EVENT_INPUT_CHANGED)
        return;

    data = event->data;
    source = INPUT_PAGE_EVENT_SOURCE(data);
    action = INPUT_PAGE_EVENT_ACTION(data);
    code = INPUT_PAGE_EVENT_CODE(data);

#if defined(FACTORY_HW_USING_KEYBOARD)
    if (source == INPUT_SOURCE_KEYBOARD && code == KEY_HOME &&
        (action == INPUT_ACTION_CLICKED ||
         action == INPUT_ACTION_LONG_PRESSED))
    {
        ui_navigation_switch_async(UI_PAGE_HOME);
        return;
    }
    if (source == INPUT_SOURCE_KEYBOARD)
        input_page_handle_keyboard_event(code, action);
#elif defined(FACTORY_HW_USING_JOYPAD)
    if (source == INPUT_SOURCE_JOYPAD_KEY &&
        code == INPUT_JOYPAD_HOME_PIN &&
        action == INPUT_ACTION_PRESSED)
    {
        ui_navigation_switch_async(UI_PAGE_HOME);
        return;
    }
    if (source == INPUT_SOURCE_JOYPAD_KEY)
        input_page_handle_joypad_event(code, action);
#else
    (void)source;
    (void)action;
    (void)code;
#endif
}

rt_err_t input_page_register(void)
{
    const ui_page_t page = {
        .id = UI_PAGE_INPUT,
        .app_id = UI_APP_NONE,
        .name = "Input Test Page",
        .on_create = input_page_on_create,
        .on_destroy = input_page_on_destroy,
        .on_enter = input_page_on_enter,
        .on_timer = input_page_on_timer,
        .on_gesture = input_page_on_gesture,
        .on_event = input_page_on_event,
        .timer_interval_ms = INPUT_PAGE_TIMER_MS,
    };

    return ui_navigation_register_page(&page);
}
