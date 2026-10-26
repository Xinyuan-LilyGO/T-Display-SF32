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
    KEYBOARD_INIT_ERROR_INFRARED = (1UL << 1),
};

rt_err_t keyboard_service_init(void);
bool keyboard_service_is_ready(void);
uint32_t keyboard_service_get_init_errors(void);
rt_err_t keyboard_service_set_led_brightness(uint8_t percent);
rt_err_t keyboard_service_get_led_brightness(uint8_t *percent);

#ifdef __cplusplus
}
#endif

#endif /* FACTORY_KEYBOARD_SERVICE_H */
