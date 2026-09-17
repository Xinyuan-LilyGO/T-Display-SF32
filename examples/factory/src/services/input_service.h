#ifndef FACTORY_INPUT_SERVICE_H
#define FACTORY_INPUT_SERVICE_H

#include <stdbool.h>
#include <stdint.h>
#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    INPUT_SOURCE_BUTTON = 0,
    INPUT_SOURCE_KEYBOARD,
    INPUT_SOURCE_JOYPAD_KEY,
    INPUT_SOURCE_JOYPAD_AXIS,
} input_source_t;

typedef enum
{
    INPUT_PROFILE_KEYBOARD = 0,
    INPUT_PROFILE_JOYPAD,
    INPUT_PROFILE_NONE,
} input_profile_t;

typedef enum
{
    INPUT_ACTION_PRESSED = 0,
    INPUT_ACTION_RELEASED,
    INPUT_ACTION_CLICKED,
    INPUT_ACTION_LONG_PRESSED,
} input_action_t;

enum
{
    INPUT_JOYPAD_ESC_PIN = 4,
    INPUT_JOYPAD_HOME_PIN = 5,
    INPUT_JOYPAD_ENTER_PIN = 6,
    INPUT_JOYPAD_CODE_UP = 0x100,
    INPUT_JOYPAD_CODE_DOWN,
    INPUT_JOYPAD_CODE_LEFT,
    INPUT_JOYPAD_CODE_RIGHT,
};

enum
{
    INPUT_INIT_ERROR_NONE     = 0,
    INPUT_INIT_ERROR_XL9555   = (1UL << 0),
    INPUT_INIT_ERROR_TCA8418  = (1UL << 1),
    INPUT_INIT_ERROR_BUTTONS  = (1UL << 2),
    INPUT_INIT_ERROR_JOYPAD_KEYS = (1UL << 3),
    INPUT_INIT_ERROR_ADS1115     = (1UL << 4),
    INPUT_INIT_ERROR_WS2812      = (1UL << 5),
};

typedef struct
{
    input_source_t source;
    input_action_t action;
    /* GPIO pin, TCA8418 KeyCode, XL9555 P0 pin, or joypad code. */
    int32_t code;
    rt_tick_t timestamp;
} input_event_t;

typedef struct
{
    int32_t x_mv;          /* ADC_X on ADS1115 AIN0. */
    int32_t y_mv;          /* ADC_Y on ADS1115 AIN1. */
    uint16_t key_state;    /* Pressed keys, bit 0..6 map to P0_0..P0_6. */
    bool axis_valid;
    bool keys_valid;
    rt_tick_t timestamp;
} input_joypad_state_t;

/* Called from the input service thread. The callback must not block. */
typedef void (*input_event_callback_t)(const input_event_t *event,
                                       void *user_data);

rt_err_t input_service_init(void);
rt_err_t input_service_start_background(void);
bool input_service_is_ready(void);
input_profile_t input_service_get_profile(void);
uint32_t input_service_get_init_errors(void);
rt_err_t input_service_get_joypad_state(input_joypad_state_t *state);
rt_err_t input_service_set_event_callback(input_event_callback_t callback,
                                          void *user_data);
rt_err_t input_service_set_joypad_rgb(bool enabled, uint8_t brightness,
                                      uint8_t red, uint8_t green,
                                      uint8_t blue);
rt_err_t input_service_get_joypad_rgb(bool *enabled, uint8_t *brightness,
                                      uint8_t *red, uint8_t *green,
                                      uint8_t *blue);

#ifdef __cplusplus
}
#endif

#endif /* FACTORY_INPUT_SERVICE_H */
