#include "keyboard_service.h"

#include <rtdevice.h>
#include <ulog.h>

#if defined(FACTORY_HW_USING_KEYBOARD)
#include "aw21009.h"
#include "infrared.h"
#endif

static uint32_t keyboard_init_errors;

#if defined(FACTORY_HW_USING_KEYBOARD)
static bool keyboard_initialized;
static bool keyboard_any_device_ready;
static uint8_t keyboard_led_brightness;

static void keyboard_init_aw21009(void)
{
    rt_err_t result = aw21009_app_init();

    if (result == RT_EOK)
    {
        /* Leave the key LEDs off after the factory startup light check. */
        result = aw21009_set_all_brightness(0U);
    }
    if (result == RT_EOK)
    {
        keyboard_any_device_ready = true;
        keyboard_led_brightness = 0U;
        LOG_I("AW21009 keyboard LED driver initialized");
    }
    else
    {
        keyboard_init_errors |= KEYBOARD_INIT_ERROR_AW21009;
        LOG_E("AW21009 initialization failed: %d", result);
    }
}

static void keyboard_init_infrared(void)
{
    int result = infrared_init();

    if (result == RT_EOK)
    {
        keyboard_any_device_ready = true;
        LOG_I("Infrared transmitter initialized");
    }
    else
    {
        keyboard_init_errors |= KEYBOARD_INIT_ERROR_INFRARED;
        LOG_E("Infrared initialization failed: %d", result);
    }
}
#endif

rt_err_t keyboard_service_init(void)
{
#if defined(FACTORY_HW_USING_KEYBOARD)
    if (keyboard_initialized)
        return keyboard_any_device_ready ? RT_EOK : -RT_ENOSYS;

    keyboard_init_errors = KEYBOARD_INIT_ERROR_NONE;
    keyboard_any_device_ready = false;

    keyboard_init_aw21009();
    keyboard_init_infrared();
    keyboard_initialized = true;

    if (keyboard_init_errors != KEYBOARD_INIT_ERROR_NONE)
    {
        LOG_W("Keyboard peripherals running in degraded mode, errors=0x%02x",
              (unsigned int)keyboard_init_errors);
    }
    return keyboard_any_device_ready ? RT_EOK : -RT_ENOSYS;
#else
    keyboard_init_errors = KEYBOARD_INIT_ERROR_NONE;
    return -RT_ENOSYS;
#endif
}

bool keyboard_service_is_ready(void)
{
#if defined(FACTORY_HW_USING_KEYBOARD)
    return keyboard_initialized && keyboard_any_device_ready;
#else
    return false;
#endif
}

uint32_t keyboard_service_get_init_errors(void)
{
    return keyboard_init_errors;
}

rt_err_t keyboard_service_set_led_brightness(uint8_t percent)
{
#if defined(FACTORY_HW_USING_KEYBOARD)
    rt_uint16_t brightness;
    rt_err_t result;

    if (!keyboard_initialized ||
        (keyboard_init_errors & KEYBOARD_INIT_ERROR_AW21009) != 0U)
        return -RT_ENOSYS;
    if (percent > 100U)
        return -RT_EINVAL;

    brightness = (rt_uint16_t)(((rt_uint32_t)percent * 4095U + 50U) /
                               100U);
    result = aw21009_set_all_brightness(brightness);
    if (result == RT_EOK)
        keyboard_led_brightness = percent;
    return result;
#else
    (void)percent;
    return -RT_ENOSYS;
#endif
}

rt_err_t keyboard_service_get_led_brightness(uint8_t *percent)
{
    if (percent == RT_NULL)
        return -RT_EINVAL;
#if defined(FACTORY_HW_USING_KEYBOARD)
    if (!keyboard_initialized ||
        (keyboard_init_errors & KEYBOARD_INIT_ERROR_AW21009) != 0U)
        return -RT_ENOSYS;
    *percent = keyboard_led_brightness;
    return RT_EOK;
#else
    return -RT_ENOSYS;
#endif
}
