#include "keyboard_service.h"

#include <rtdevice.h>
#include <ulog.h>

#if defined(FACTORY_HW_USING_KEYBOARD)
#include "aw21009.h"
#include "aw86224_rt_device.h"
#include "infrared.h"
#endif

#define KEYBOARD_MOTOR_DEFAULT_AMPLITUDE 70U
#define KEYBOARD_MOTOR_KEY_WAVE_ID       4U
#define KEYBOARD_MOTOR_ALERT_STACK_SIZE  1024U

static uint32_t keyboard_init_errors;

#if defined(FACTORY_HW_USING_KEYBOARD)
static rt_device_t keyboard_haptic;
static bool keyboard_initialized;
static bool keyboard_any_device_ready;
static uint8_t keyboard_led_brightness;
static bool keyboard_motor_enabled = true;
static uint8_t keyboard_motor_amplitude =
    KEYBOARD_MOTOR_DEFAULT_AMPLITUDE;
static volatile bool keyboard_motor_alert_active;
static uint32_t keyboard_motor_alert_duration_ms;

static rt_uint8_t keyboard_motor_gain(uint8_t amplitude_percent)
{
    return (rt_uint8_t)(((rt_uint32_t)amplitude_percent * 255U + 50U) /
                        100U);
}

static void keyboard_motor_alert_entry(void *parameter)
{
    struct aw86224_rt_ram_effect effect;
    rt_uint8_t gain;
    rt_err_t result;

    (void)parameter;
    gain = keyboard_motor_gain(keyboard_motor_amplitude);
    result = rt_device_control(keyboard_haptic,
                               AW86224_RT_CTRL_SET_GAIN, &gain);
    if (result == RT_EOK)
    {
        effect.wave_id = KEYBOARD_MOTOR_KEY_WAVE_ID;
        effect.loop = 15U;
        effect.auto_brake = RT_TRUE;
        result = rt_device_control(keyboard_haptic,
                                   AW86224_RT_CTRL_PLAY_RAM, &effect);
    }
    if (result == RT_EOK)
    {
        rt_thread_mdelay(keyboard_motor_alert_duration_ms);
        result = rt_device_control(keyboard_haptic,
                                   AW86224_RT_CTRL_STOP, RT_NULL);
    }
    if (result != RT_EOK)
        LOG_E("Keyboard alert vibration failed: %d", result);
    keyboard_motor_alert_active = false;
}

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

static void keyboard_init_aw86224(void)
{
    rt_err_t result;
    rt_uint8_t gain;

    keyboard_haptic = rt_device_find(AW86224_RT_DEVICE_NAME);
    if (keyboard_haptic == RT_NULL)
    {
        result = -RT_ENOSYS;
    }
    else
    {
        result = rt_device_open(keyboard_haptic, RT_DEVICE_OFLAG_RDWR);
    }

    if (result == RT_EOK)
    {
        gain = keyboard_motor_gain(keyboard_motor_amplitude);
        result = rt_device_control(keyboard_haptic,
                                   AW86224_RT_CTRL_SET_GAIN, &gain);
    }

    if (result == RT_EOK)
    {
        keyboard_any_device_ready = true;
        LOG_I("AW86224 haptic device initialized");
    }
    else
    {
        keyboard_haptic = RT_NULL;
        keyboard_init_errors |= KEYBOARD_INIT_ERROR_AW86224;
        LOG_E("AW86224 initialization failed: %d", result);
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
    keyboard_init_aw86224();
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

rt_err_t keyboard_service_set_motor_enabled(bool enabled)
{
#if defined(FACTORY_HW_USING_KEYBOARD)
    rt_uint8_t gain = enabled
                          ? keyboard_motor_gain(keyboard_motor_amplitude)
                          : 0U;
    rt_err_t result;

    if (!keyboard_initialized || keyboard_haptic == RT_NULL)
        return -RT_ENOSYS;
    if (!enabled)
    {
        result = rt_device_control(keyboard_haptic,
                                   AW86224_RT_CTRL_STOP, RT_NULL);
        if (result != RT_EOK)
            return result;
    }
    result = rt_device_control(keyboard_haptic,
                               AW86224_RT_CTRL_SET_GAIN, &gain);
    if (result == RT_EOK)
        keyboard_motor_enabled = enabled;
    return result;
#else
    (void)enabled;
    return -RT_ENOSYS;
#endif
}

rt_err_t keyboard_service_get_motor_enabled(bool *enabled)
{
    if (enabled == RT_NULL)
        return -RT_EINVAL;
#if defined(FACTORY_HW_USING_KEYBOARD)
    if (!keyboard_initialized || keyboard_haptic == RT_NULL)
        return -RT_ENOSYS;
    *enabled = keyboard_motor_enabled;
    return RT_EOK;
#else
    return -RT_ENOSYS;
#endif
}

rt_err_t keyboard_service_preview_motor(uint8_t amplitude_percent,
                                        keyboard_motor_effect_t effect)
{
#if defined(FACTORY_HW_USING_KEYBOARD)
    static const rt_uint8_t effect_loops[KEYBOARD_MOTOR_EFFECT_COUNT] = {
        0U, 1U, 6U, 2U,
    };
    struct aw86224_rt_ram_effect ram_effect;
    rt_uint8_t gain;
    rt_err_t result;

    if (!keyboard_initialized || keyboard_haptic == RT_NULL)
        return -RT_ENOSYS;
    if (amplitude_percent > 100U || effect >= KEYBOARD_MOTOR_EFFECT_COUNT)
        return -RT_EINVAL;
    if (keyboard_motor_alert_active)
        return -RT_EBUSY;
    keyboard_motor_amplitude = amplitude_percent;
    if (!keyboard_motor_enabled || amplitude_percent == 0U)
        return RT_EOK;

    gain = keyboard_motor_gain(amplitude_percent);
    result = rt_device_control(keyboard_haptic,
                               AW86224_RT_CTRL_SET_GAIN, &gain);
    if (result != RT_EOK)
        return result;

    ram_effect.wave_id = KEYBOARD_MOTOR_KEY_WAVE_ID;
    ram_effect.loop = effect_loops[effect];
    ram_effect.auto_brake = RT_TRUE;
    return rt_device_control(keyboard_haptic,
                             AW86224_RT_CTRL_PLAY_RAM, &ram_effect);
#else
    (void)amplitude_percent;
    (void)effect;
    return -RT_ENOSYS;
#endif
}

rt_err_t keyboard_service_play_key_feedback(void)
{
#if defined(FACTORY_HW_USING_KEYBOARD)
    struct aw86224_rt_ram_effect ram_effect;
    rt_uint8_t gain;
    rt_err_t result;

    if (!keyboard_initialized || keyboard_haptic == RT_NULL)
        return -RT_ENOSYS;
    if (!keyboard_motor_enabled || keyboard_motor_amplitude == 0U)
        return RT_EOK;
    if (keyboard_motor_alert_active)
        return RT_EOK;

    gain = keyboard_motor_gain(keyboard_motor_amplitude);
    result = rt_device_control(keyboard_haptic,
                               AW86224_RT_CTRL_SET_GAIN, &gain);
    if (result != RT_EOK)
        return result;

    ram_effect.wave_id = KEYBOARD_MOTOR_KEY_WAVE_ID;
    ram_effect.loop = 0U;
    ram_effect.auto_brake = RT_TRUE;
    return rt_device_control(keyboard_haptic,
                             AW86224_RT_CTRL_PLAY_RAM, &ram_effect);
#else
    return -RT_ENOSYS;
#endif
}

rt_err_t keyboard_service_play_alert(uint32_t duration_ms)
{
#if defined(FACTORY_HW_USING_KEYBOARD)
    rt_thread_t thread;
    rt_err_t result;

    if (!keyboard_initialized || keyboard_haptic == RT_NULL)
        return -RT_ENOSYS;
    if (duration_ms == 0U)
        return -RT_EINVAL;
    if (!keyboard_motor_enabled || keyboard_motor_amplitude == 0U)
        return RT_EOK;
    if (keyboard_motor_alert_active)
        return -RT_EBUSY;

    keyboard_motor_alert_duration_ms = duration_ms;
    keyboard_motor_alert_active = true;
    thread = rt_thread_create("pom_vib", keyboard_motor_alert_entry,
                              RT_NULL, KEYBOARD_MOTOR_ALERT_STACK_SIZE,
                              RT_THREAD_PRIORITY_LOW, 10U);
    if (thread == RT_NULL)
    {
        keyboard_motor_alert_active = false;
        return -RT_ENOMEM;
    }
    result = rt_thread_startup(thread);
    if (result != RT_EOK)
    {
        keyboard_motor_alert_active = false;
        rt_thread_delete(thread);
    }
    return result;
#else
    (void)duration_ms;
    return -RT_ENOSYS;
#endif
}
