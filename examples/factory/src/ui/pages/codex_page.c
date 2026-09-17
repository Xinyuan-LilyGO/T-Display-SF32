#include "codex_page.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <lvgl.h>
#include <ulog.h>

#include "bluetooth_service.h"
#include "codex_micro_service.h"
#include "input_service.h"
#include "ui_config.h"
#include "ui_navigation.h"
#include "ui_status_bar.h"
#include "ui_theme.h"

#if defined(FACTORY_HW_USING_KEYBOARD)
#include "tca8418.h"
#endif

#define CODEX_PAGE_TIMER_MS       200U
#define CODEX_HEADER_Y            (UI_STATUS_BAR_HEIGHT + 4)
#define CODEX_AGENT_CARD_WIDTH    140
#define CODEX_AGENT_CARD_HEIGHT   90
#define CODEX_AGENT_CARD_GAP_X    14
#define CODEX_AGENT_CARD_GAP_Y    10
#define CODEX_AGENT_GRID_X        16
#define CODEX_AGENT_GRID_Y        154
#define CODEX_ACTION_Y            362
#define CODEX_ACTION_WIDTH        66
#define CODEX_ACTION_HEIGHT       50
#define CODEX_ACTION_GAP          8
#define CODEX_INPUT_SOURCE(data) \
    ((input_source_t)(((data) >> UI_EVENT_INPUT_SOURCE_SHIFT) & 0xFFU))
#define CODEX_INPUT_ACTION(data) \
    ((input_action_t)(((data) >> UI_EVENT_INPUT_ACTION_SHIFT) & 0xFFU))
#define CODEX_INPUT_CODE(data) \
    ((uint16_t)((data) & UI_EVENT_INPUT_CODE_MASK))

typedef struct
{
    lv_obj_t *button;
    lv_obj_t *indicator;
    lv_obj_t *state;
} codex_agent_view_t;

static codex_agent_view_t codex_agents[CODEX_MICRO_AGENT_COUNT];
static bool codex_mic_active;
static lv_obj_t *codex_connection_label;
static lv_obj_t *codex_detail_label;
static lv_obj_t *codex_counter_label;
static codex_micro_status_t codex_last_status;
static bool codex_last_status_valid;

#if defined(FACTORY_HW_USING_JOYPAD)
static int16_t codex_last_radial_angle = -1;
static int16_t codex_last_radial_distance = -1;
#endif

static lv_obj_t *codex_create_label(lv_obj_t *parent, const char *text,
                                    const lv_font_t *font, uint32_t color)
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

static void codex_style_button(lv_obj_t *button, uint32_t background)
{
    lv_obj_set_style_bg_color(button, lv_color_hex(background), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(UI_COLOR_TRACK), 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_radius(button, 4, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(UI_COLOR_TRACK),
                              LV_STATE_PRESSED);
    ui_theme_apply_focus_style(button);
}

static void codex_back_clicked(lv_event_t *event)
{
    (void)event;
    ui_navigation_switch_async(UI_PAGE_HOME);
}

static void codex_send_button_state(codex_micro_key_t key, bool pressed)
{
    (void)codex_micro_service_send_key(key, pressed);
    if (key == CODEX_MICRO_KEY_MIC)
        codex_mic_active = pressed;
}

static void codex_key_button_event(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    codex_micro_key_t key = (codex_micro_key_t)(uintptr_t)
                                lv_event_get_user_data(event);

    if (code == LV_EVENT_PRESSED)
        codex_send_button_state(key, true);
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
        codex_send_button_state(key, false);
}

static void codex_encoder_clicked(lv_event_t *event)
{
    bool clockwise = (bool)(uintptr_t)lv_event_get_user_data(event);

    (void)codex_micro_service_send_encoder(clockwise);
}

static lv_obj_t *codex_create_header_button(ui_page_t *page, int32_t x,
                                             const char *symbol,
                                             uint32_t color,
                                             lv_event_cb_t callback,
                                             void *user_data)
{
    lv_obj_t *button = lv_button_create(page->root);
    lv_obj_t *label;

    lv_obj_set_size(button, 44, 44);
    lv_obj_set_pos(button, x, CODEX_HEADER_Y);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_radius(button, 4, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    ui_theme_apply_focus_style(button);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user_data);
    label = codex_create_label(button, symbol, &lv_font_montserrat_22, color);
    lv_obj_center(label);
    if (page->group != RT_NULL)
        lv_group_add_obj(page->group, button);
    return button;
}

static void codex_create_header(ui_page_t *page)
{
    lv_obj_t *label;

    codex_create_header_button(page, 12, LV_SYMBOL_LEFT, UI_COLOR_TEXT,
                               codex_back_clicked, RT_NULL);
    label = codex_create_label(page->root, "CODEX MICRO",
                               &lv_font_montserrat_24, UI_COLOR_TEXT);
    lv_obj_set_pos(label, 70, CODEX_HEADER_Y + 7);
    codex_create_header_button(page, 370, LV_SYMBOL_MINUS, UI_COLOR_MUTED,
                               codex_encoder_clicked, (void *)false);
    codex_create_header_button(page, 424, LV_SYMBOL_PLUS, UI_COLOR_PRIMARY,
                               codex_encoder_clicked, (void *)true);
}

static void codex_create_connection_panel(ui_page_t *page)
{
    lv_obj_t *panel = lv_obj_create(page->root);

    lv_obj_set_size(panel, 448, 48);
    lv_obj_set_pos(panel, 16, 100);
    lv_obj_set_style_bg_color(panel, lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, 4, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_set_style_shadow_width(panel, 0, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE |
                              LV_OBJ_FLAG_CLICKABLE);

    codex_connection_label = codex_create_label(
        panel, "STARTING", &lv_font_montserrat_18, UI_COLOR_WARNING);
    lv_obj_set_size(codex_connection_label, 214, 24);
    lv_obj_set_pos(codex_connection_label, 14, 7);
    codex_detail_label = codex_create_label(
        panel, "BLE HID", &lv_font_montserrat_12, UI_COLOR_MUTED);
    lv_obj_set_size(codex_detail_label, 214, 18);
    lv_obj_set_pos(codex_detail_label, 14, 29);
    codex_counter_label = codex_create_label(
        panel, "RX 0  TX 0", &lv_font_montserrat_14, UI_COLOR_MUTED);
    lv_obj_set_size(codex_counter_label, 190, 22);
    lv_obj_set_style_text_align(codex_counter_label,
                                LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(codex_counter_label, 242, 14);
}

static const char *codex_agent_state_text(codex_micro_agent_state_t state)
{
    switch (state)
    {
    case CODEX_MICRO_AGENT_OFF: return "UNASSIGNED";
    case CODEX_MICRO_AGENT_IDLE: return "IDLE";
    case CODEX_MICRO_AGENT_WORKING: return "THINKING";
    case CODEX_MICRO_AGENT_COMPLETE: return "COMPLETE";
    case CODEX_MICRO_AGENT_AWAITING_INPUT: return "NEEDS INPUT";
    case CODEX_MICRO_AGENT_ERROR: return "ERROR";
    case CODEX_MICRO_AGENT_UNKNOWN:
    default: return "ACTIVE";
    }
}

static uint32_t codex_agent_color(const codex_micro_agent_t *agent)
{
    if (agent->state == CODEX_MICRO_AGENT_OFF || agent->color == 0U)
        return UI_COLOR_TRACK;
    return agent->color;
}

static void codex_create_agent_card(ui_page_t *page, uint8_t index)
{
    int32_t column = index % 3U;
    int32_t row = index / 3U;
    int32_t x = CODEX_AGENT_GRID_X +
                column * (CODEX_AGENT_CARD_WIDTH + CODEX_AGENT_CARD_GAP_X);
    int32_t y = CODEX_AGENT_GRID_Y +
                row * (CODEX_AGENT_CARD_HEIGHT + CODEX_AGENT_CARD_GAP_Y);
    lv_obj_t *button = lv_button_create(page->root);
    lv_obj_t *label;

    lv_obj_set_size(button, CODEX_AGENT_CARD_WIDTH,
                    CODEX_AGENT_CARD_HEIGHT);
    lv_obj_set_pos(button, x, y);
    codex_style_button(button, UI_COLOR_PANEL);
    lv_obj_add_event_cb(button, codex_key_button_event, LV_EVENT_PRESSED,
                        (void *)(uintptr_t)index);
    lv_obj_add_event_cb(button, codex_key_button_event, LV_EVENT_RELEASED,
                        (void *)(uintptr_t)index);
    lv_obj_add_event_cb(button, codex_key_button_event, LV_EVENT_PRESS_LOST,
                        (void *)(uintptr_t)index);
    if (page->group != RT_NULL)
        lv_group_add_obj(page->group, button);

    label = codex_create_label(button, "AGENT", &lv_font_montserrat_12,
                               UI_COLOR_MUTED);
    lv_obj_set_pos(label, 12, 10);
    label = codex_create_label(button, "1", &lv_font_montserrat_30,
                               UI_COLOR_TEXT);
    lv_label_set_text_fmt(label, "%u", (unsigned int)index + 1U);
    lv_obj_set_pos(label, 12, 27);

    codex_agents[index].indicator = lv_obj_create(button);
    lv_obj_set_size(codex_agents[index].indicator, 12, 12);
    lv_obj_set_pos(codex_agents[index].indicator, 112, 14);
    lv_obj_set_style_bg_color(codex_agents[index].indicator,
                              lv_color_hex(UI_COLOR_TRACK), 0);
    lv_obj_set_style_bg_opa(codex_agents[index].indicator, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(codex_agents[index].indicator, 0, 0);
    lv_obj_set_style_radius(codex_agents[index].indicator,
                            LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(codex_agents[index].indicator, 0, 0);
    lv_obj_remove_flag(codex_agents[index].indicator,
                       LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    codex_agents[index].state = codex_create_label(
        button, "UNASSIGNED", &lv_font_montserrat_12, UI_COLOR_MUTED);
    lv_obj_set_size(codex_agents[index].state, 94, 18);
    lv_label_set_long_mode(codex_agents[index].state, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(codex_agents[index].state, 40, 61);
    codex_agents[index].button = button;
}

static void codex_create_action_button(ui_page_t *page, uint8_t slot,
                                       const char *text,
                                       codex_micro_key_t key,
                                       uint32_t color)
{
    lv_obj_t *button = lv_button_create(page->root);
    lv_obj_t *label;
    int32_t x = 20 + slot * (CODEX_ACTION_WIDTH + CODEX_ACTION_GAP);

    lv_obj_set_size(button, CODEX_ACTION_WIDTH, CODEX_ACTION_HEIGHT);
    lv_obj_set_pos(button, x, CODEX_ACTION_Y);
    codex_style_button(button, color);
    lv_obj_add_event_cb(button, codex_key_button_event, LV_EVENT_PRESSED,
                        (void *)(uintptr_t)key);
    lv_obj_add_event_cb(button, codex_key_button_event, LV_EVENT_RELEASED,
                        (void *)(uintptr_t)key);
    lv_obj_add_event_cb(button, codex_key_button_event, LV_EVENT_PRESS_LOST,
                        (void *)(uintptr_t)key);
    label = codex_create_label(button, text, &lv_font_montserrat_12,
                               UI_COLOR_TEXT);
    lv_obj_set_size(label, CODEX_ACTION_WIDTH, 18);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
    if (page->group != RT_NULL)
        lv_group_add_obj(page->group, button);
}

static void codex_update_status(void)
{
    codex_micro_status_t status;
    uint8_t index;

    if (codex_micro_service_get_status(&status) != RT_EOK ||
            codex_connection_label == RT_NULL)
        return;
    if (codex_last_status_valid &&
            memcmp(&status, &codex_last_status, sizeof(status)) == 0)
        return;
    codex_last_status = status;
    codex_last_status_valid = true;

    if (status.protocol_ready)
    {
        lv_label_set_text(codex_connection_label, "CODEX CONNECTED");
        lv_label_set_text(codex_detail_label, "BLE HID READY");
        lv_obj_set_style_text_color(codex_connection_label,
                                    lv_color_hex(UI_COLOR_SUCCESS), 0);
    }
    else if (status.connected)
    {
        lv_label_set_text(codex_connection_label, "PC CONNECTED");
        lv_label_set_text(codex_detail_label, "WAITING FOR CODEX");
        lv_obj_set_style_text_color(codex_connection_label,
                                    lv_color_hex(UI_COLOR_WARNING), 0);
    }
    else if (status.advertising)
    {
        lv_label_set_text(codex_connection_label, "PAIR IN WINDOWS");
        lv_label_set_text(codex_detail_label, "CODEX MICRO #1");
        lv_obj_set_style_text_color(codex_connection_label,
                                    lv_color_hex(UI_COLOR_PRIMARY), 0);
    }
    else
    {
        lv_label_set_text(codex_connection_label, "STARTING BLE HID");
        lv_label_set_text(codex_detail_label, "WAITING FOR BT STACK");
        lv_obj_set_style_text_color(codex_connection_label,
                                    lv_color_hex(UI_COLOR_MUTED), 0);
    }
    lv_label_set_text_fmt(codex_counter_label, "RX %lu  TX %lu  E %lu",
                          (unsigned long)status.rx_messages,
                          (unsigned long)status.tx_messages,
                          (unsigned long)status.protocol_errors);

    for (index = 0U; index < CODEX_MICRO_AGENT_COUNT; index++)
    {
        uint32_t color = codex_agent_color(&status.agents[index]);

        lv_obj_set_style_bg_color(codex_agents[index].indicator,
                                  lv_color_hex(color), 0);
        lv_label_set_text(codex_agents[index].state,
                          codex_agent_state_text(status.agents[index].state));
        lv_obj_set_style_text_color(codex_agents[index].state,
                                    lv_color_hex(color), 0);
        lv_obj_set_style_border_color(
            codex_agents[index].button,
            lv_color_hex(status.agents[index].selected
                             ? UI_COLOR_PRIMARY : UI_COLOR_TRACK), 0);
        lv_obj_set_style_border_width(codex_agents[index].button,
                                      status.agents[index].selected ? 2 : 1,
                                      0);
    }
}

#if defined(FACTORY_HW_USING_JOYPAD)
static void codex_update_radial(void)
{
    input_joypad_state_t state;
    float x;
    float y;
    float distance;
    float angle;
    int16_t angle_step;
    int16_t distance_step;

    if (input_service_get_joypad_state(&state) != RT_EOK ||
            !state.axis_valid)
        return;
    x = ((float)state.x_mv - 1600.0f) / 1600.0f;
    y = (1600.0f - (float)state.y_mv) / 1600.0f;
    distance = sqrtf(x * x + y * y);
    if (distance < 0.16f)
    {
        distance = 0.0f;
        angle = 0.0f;
    }
    else
    {
        angle = atan2f(y, x) / (2.0f * 3.14159265f);
        if (angle < 0.0f)
            angle += 1.0f;
        if (distance > 1.0f)
            distance = 1.0f;
    }
    angle_step = (int16_t)(angle * 32.0f + 0.5f);
    distance_step = (int16_t)(distance * 20.0f + 0.5f);
    if (angle_step == codex_last_radial_angle &&
            distance_step == codex_last_radial_distance)
        return;
    codex_last_radial_angle = angle_step;
    codex_last_radial_distance = distance_step;
    (void)codex_micro_service_send_radial(angle, distance);
}
#endif

static bool codex_keyboard_key(uint16_t code, codex_micro_key_t *key)
{
#if defined(FACTORY_HW_USING_KEYBOARD)
    switch ((KeyCode)code)
    {
    case KEY_1: *key = CODEX_MICRO_KEY_AGENT_0; return true;
    case KEY_2: *key = CODEX_MICRO_KEY_AGENT_1; return true;
    case KEY_3: *key = CODEX_MICRO_KEY_AGENT_2; return true;
    case KEY_4: *key = CODEX_MICRO_KEY_AGENT_3; return true;
    case KEY_5: *key = CODEX_MICRO_KEY_AGENT_4; return true;
    case KEY_6: *key = CODEX_MICRO_KEY_AGENT_5; return true;
    case KEY_MUL: *key = CODEX_MICRO_KEY_FAST; return true;
    case KEY_7: *key = CODEX_MICRO_KEY_APPROVE; return true;
    case KEY_8: *key = CODEX_MICRO_KEY_DECLINE; return true;
    case KEY_9: *key = CODEX_MICRO_KEY_SPLIT; return true;
    case KEY_0: *key = CODEX_MICRO_KEY_MIC; return true;
    case KEY_WELL: *key = CODEX_MICRO_KEY_SEND; return true;
    default: return false;
    }
#else
    (void)code;
    (void)key;
    return false;
#endif
}

static bool codex_joypad_key(uint16_t code, codex_micro_key_t *key)
{
#if defined(FACTORY_HW_USING_JOYPAD)
    switch (code)
    {
    case 0U: *key = CODEX_MICRO_KEY_FAST; return true;
    case 1U: *key = CODEX_MICRO_KEY_APPROVE; return true;
    case 2U: *key = CODEX_MICRO_KEY_DECLINE; return true;
    case 3U: *key = CODEX_MICRO_KEY_SPLIT; return true;
    default: return false;
    }
#else
    (void)code;
    (void)key;
    return false;
#endif
}

static void codex_handle_raw_input(uintptr_t data)
{
    input_source_t source = CODEX_INPUT_SOURCE(data);
    input_action_t action = CODEX_INPUT_ACTION(data);
    uint16_t code = CODEX_INPUT_CODE(data);
    codex_micro_key_t key;
    bool mapped = false;

    if (source == INPUT_SOURCE_KEYBOARD)
    {
        mapped = codex_keyboard_key(code, &key);
        if (mapped && (action == INPUT_ACTION_CLICKED ||
                       action == INPUT_ACTION_LONG_PRESSED))
        {
            if (key == CODEX_MICRO_KEY_MIC)
            {
                codex_send_button_state(
                    key, !codex_mic_active);
            }
            else
            {
                codex_send_button_state(key, true);
                codex_send_button_state(key, false);
            }
        }
    }
    else if (source == INPUT_SOURCE_JOYPAD_KEY)
    {
        mapped = codex_joypad_key(code, &key);
        if (mapped && (action == INPUT_ACTION_PRESSED ||
                       action == INPUT_ACTION_RELEASED))
        {
            codex_send_button_state(key, action == INPUT_ACTION_PRESSED);
        }
    }
}

static void codex_page_on_create(ui_page_t *page)
{
    uint8_t index;

    if (page == RT_NULL || page->root == RT_NULL)
        return;
    memset(codex_agents, 0, sizeof(codex_agents));
    codex_create_header(page);
    codex_create_connection_panel(page);
    for (index = 0U; index < CODEX_MICRO_AGENT_COUNT; index++)
        codex_create_agent_card(page, index);
    codex_create_action_button(page, 0U, "FAST",
                               CODEX_MICRO_KEY_FAST, UI_COLOR_PANEL);
    codex_create_action_button(page, 1U, "APPROVE",
                               CODEX_MICRO_KEY_APPROVE, 0x174025U);
    codex_create_action_button(page, 2U, "DECLINE",
                               CODEX_MICRO_KEY_DECLINE, 0x481923U);
    codex_create_action_button(page, 3U, "SPLIT",
                               CODEX_MICRO_KEY_SPLIT, UI_COLOR_PANEL);
    codex_create_action_button(page, 4U, "MIC",
                               CODEX_MICRO_KEY_MIC, UI_COLOR_PANEL);
    codex_create_action_button(page, 5U, "SEND",
                               CODEX_MICRO_KEY_SEND, 0x16405AU);
    codex_last_status_valid = false;
    codex_update_status();
}

static void codex_page_on_destroy(ui_page_t *page)
{
    (void)page;
    if (codex_mic_active)
        codex_send_button_state(CODEX_MICRO_KEY_MIC, false);
    memset(codex_agents, 0, sizeof(codex_agents));
    codex_connection_label = RT_NULL;
    codex_detail_label = RT_NULL;
    codex_counter_label = RT_NULL;
    codex_last_status_valid = false;
#if defined(FACTORY_HW_USING_JOYPAD)
    codex_last_radial_angle = -1;
    codex_last_radial_distance = -1;
#endif
}

static void codex_page_on_enter(ui_page_t *page)
{
    rt_err_t result;

    (void)page;
    result = bluetooth_service_set_enabled(true);
    if (result != RT_EOK)
        LOG_W("Enable Bluetooth for Codex Micro failed: %d", result);
    result = codex_micro_service_start();
    if (result != RT_EOK)
        LOG_W("Start Codex Micro service failed: %d", result);
}

static void codex_page_on_timer(ui_page_t *page)
{
    (void)page;
    codex_update_status();
#if defined(FACTORY_HW_USING_JOYPAD)
    codex_update_radial();
#endif
}

static void codex_page_on_gesture(ui_page_t *page, ui_gesture_t gesture)
{
    (void)page;
    if (gesture == UI_GESTURE_RIGHT)
        ui_navigation_switch_async(UI_PAGE_HOME);
}

static void codex_page_on_event(ui_page_t *page, const ui_event_t *event)
{
    uint32_t key;

    (void)page;
    if (event == RT_NULL)
        return;
    if (event->type == UI_EVENT_INPUT_CHANGED)
    {
        codex_handle_raw_input(event->data);
        return;
    }
    if (event->type != UI_EVENT_KEY)
        return;
    key = (uint32_t)event->data & UI_EVENT_KEY_CODE_MASK;
    if (key == LV_KEY_ESC || key == LV_KEY_HOME)
        ui_navigation_switch_async(UI_PAGE_HOME);
}

rt_err_t codex_page_register(void)
{
    const ui_page_t page = {
        .id = UI_PAGE_CODEX,
        .app_id = UI_APP_NONE,
        .name = "Codex Micro Page",
        .on_create = codex_page_on_create,
        .on_destroy = codex_page_on_destroy,
        .on_enter = codex_page_on_enter,
        .on_timer = codex_page_on_timer,
        .on_gesture = codex_page_on_gesture,
        .on_event = codex_page_on_event,
        .timer_interval_ms = CODEX_PAGE_TIMER_MS,
    };

    return ui_navigation_register_page(&page);
}
