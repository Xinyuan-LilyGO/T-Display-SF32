#ifndef FACTORY_KEYBOARD_SERVICE_H
#define FACTORY_KEYBOARD_SERVICE_H

#include <stdbool.h>
#include <stdint.h>
#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

enum
{
    KEYBOARD_INIT_ERROR_NONE     = 0,
    KEYBOARD_INIT_ERROR_AW21009  = (1UL << 0),
    KEYBOARD_INIT_ERROR_AW86224  = (1UL << 1),
    KEYBOARD_INIT_ERROR_INFRARED = (1UL << 2),
};

typedef enum
{
    KEYBOARD_MOTOR_EFFECT_SHORT = 0,
    KEYBOARD_MOTOR_EFFECT_DOUBLE,
    KEYBOARD_MOTOR_EFFECT_LONG,
    KEYBOARD_MOTOR_EFFECT_HEARTBEAT,
    KEYBOARD_MOTOR_EFFECT_COUNT,
} keyboard_motor_effect_t;

rt_err_t keyboard_service_init(void);
bool keyboard_service_is_ready(void);
uint32_t keyboard_service_get_init_errors(void);
rt_err_t keyboard_service_set_led_brightness(uint8_t percent);
rt_err_t keyboard_service_get_led_brightness(uint8_t *percent);
rt_err_t keyboard_service_set_motor_enabled(bool enabled);
rt_err_t keyboard_service_get_motor_enabled(bool *enabled);
rt_err_t keyboard_service_preview_motor(uint8_t amplitude_percent,
                                        keyboard_motor_effect_t effect);
rt_err_t keyboard_service_play_key_feedback(void);
rt_err_t keyboard_service_play_alert(uint32_t duration_ms);

#ifdef __cplusplus
}
#endif

#endif /* FACTORY_KEYBOARD_SERVICE_H */
