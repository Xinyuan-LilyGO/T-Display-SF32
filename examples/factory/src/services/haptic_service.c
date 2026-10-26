#include "haptic_service.h"

#include <rtdevice.h>
#include <ulog.h>

#ifdef PKG_USING_AW86224
#include "aw86224_rt_device.h"
#endif

#define HAPTIC_DEFAULT_AMPLITUDE 70U
#define HAPTIC_WAVE_ID 4U
#define HAPTIC_ALERT_STACK_SIZE 1024U

static bool haptic_init_attempted;
static bool haptic_ready;
static bool haptic_enabled = true;
static uint8_t haptic_amplitude = HAPTIC_DEFAULT_AMPLITUDE;
static haptic_effect_t haptic_effect = HAPTIC_EFFECT_SHORT;

#ifdef PKG_USING_AW86224
static const rt_uint8_t haptic_effect_loops[HAPTIC_EFFECT_COUNT] = {
    0U, 1U, 6U, 2U,
};

static rt_device_t haptic_device;
static rt_mutex_t haptic_lock;
static volatile bool haptic_alert_active;
static uint32_t haptic_alert_duration_ms;

static rt_uint8_t haptic_gain(uint8_t amplitude_percent)
{
    return (rt_uint8_t)(((rt_uint32_t)amplitude_percent * 255U + 50U) /
                        100U);
}

static rt_err_t haptic_play_locked(haptic_effect_t effect)
{
    struct aw86224_rt_ram_effect ram_effect = {
        .wave_id = HAPTIC_WAVE_ID,
        .loop = haptic_effect_loops[effect],
        .auto_brake = RT_TRUE,
    };
    rt_uint8_t gain = haptic_gain(haptic_amplitude);
    rt_err_t result;

    result = rt_device_control(haptic_device, AW86224_RT_CTRL_SET_GAIN,
                               &gain);
    if (result != RT_EOK)
        return result;
    return rt_device_control(haptic_device, AW86224_RT_CTRL_PLAY_RAM,
                             &ram_effect);
}

static void haptic_alert_entry(void *parameter)
{
    struct aw86224_rt_ram_effect effect = {
        .wave_id = HAPTIC_WAVE_ID,
        .loop = 15U,
        .auto_brake = RT_TRUE,
    };
    rt_uint8_t gain;
    rt_err_t result;

    (void)parameter;
    rt_mutex_take(haptic_lock, RT_WAITING_FOREVER);
    gain = haptic_gain(haptic_amplitude);
    result = rt_device_control(haptic_device, AW86224_RT_CTRL_SET_GAIN,
                               &gain);
    if (result == RT_EOK)
    {
        result = rt_device_control(haptic_device,
                                   AW86224_RT_CTRL_PLAY_RAM, &effect);
    }
    rt_mutex_release(haptic_lock);

    if (result == RT_EOK)
    {
        rt_thread_mdelay(haptic_alert_duration_ms);
        rt_mutex_take(haptic_lock, RT_WAITING_FOREVER);
        result = rt_device_control(haptic_device,
                                   AW86224_RT_CTRL_STOP, RT_NULL);
        rt_mutex_release(haptic_lock);
    }
    if (result != RT_EOK)
        LOG_E("Haptic alert failed: %d", result);
    haptic_alert_active = false;
}
#endif

rt_err_t haptic_service_init(void)
{
#ifdef PKG_USING_AW86224
    rt_uint8_t gain;
    rt_err_t result;

    if (haptic_init_attempted)
        return haptic_ready ? RT_EOK : -RT_ENOSYS;
    haptic_init_attempted = true;

    haptic_lock = rt_mutex_create("haptic", RT_IPC_FLAG_FIFO);
    if (haptic_lock == RT_NULL)
        return -RT_ENOMEM;

    haptic_device = rt_device_find(AW86224_RT_DEVICE_NAME);
    if (haptic_device == RT_NULL)
        result = -RT_ENOSYS;
    else
        result = rt_device_open(haptic_device, RT_DEVICE_OFLAG_RDWR);

    if (result == RT_EOK)
    {
        gain = haptic_gain(haptic_amplitude);
        result = rt_device_control(haptic_device,
                                   AW86224_RT_CTRL_SET_GAIN, &gain);
    }
    if (result != RT_EOK)
    {
        haptic_device = RT_NULL;
        LOG_E("AW86224 haptic initialization failed: %d", result);
        return result;
    }

    haptic_ready = true;
    LOG_I("AW86224 haptic initialized: amplitude=%u effect=%u",
          haptic_amplitude, (unsigned int)haptic_effect);
    return RT_EOK;
#else
    haptic_init_attempted = true;
    return -RT_ENOSYS;
#endif
}

bool haptic_service_is_ready(void)
{
    return haptic_ready;
}

rt_err_t haptic_service_set_enabled(bool enabled)
{
#ifdef PKG_USING_AW86224
    rt_uint8_t gain;
    rt_err_t result = RT_EOK;

    if (!haptic_ready || haptic_lock == RT_NULL)
        return -RT_ENOSYS;

    rt_mutex_take(haptic_lock, RT_WAITING_FOREVER);
    if (!enabled)
        result = rt_device_control(haptic_device,
                                   AW86224_RT_CTRL_STOP, RT_NULL);
    gain = enabled ? haptic_gain(haptic_amplitude) : 0U;
    if (result == RT_EOK)
    {
        result = rt_device_control(haptic_device,
                                   AW86224_RT_CTRL_SET_GAIN, &gain);
    }
    if (result == RT_EOK)
        haptic_enabled = enabled;
    rt_mutex_release(haptic_lock);
    return result;
#else
    (void)enabled;
    return -RT_ENOSYS;
#endif
}

rt_err_t haptic_service_get_config(bool *enabled, uint8_t *amplitude_percent,
                                   haptic_effect_t *effect)
{
    if (enabled == RT_NULL || amplitude_percent == RT_NULL ||
            effect == RT_NULL)
    {
        return -RT_EINVAL;
    }
    if (!haptic_ready)
        return -RT_ENOSYS;

#ifdef PKG_USING_AW86224
    rt_mutex_take(haptic_lock, RT_WAITING_FOREVER);
#endif
    *enabled = haptic_enabled;
    *amplitude_percent = haptic_amplitude;
    *effect = haptic_effect;
#ifdef PKG_USING_AW86224
    rt_mutex_release(haptic_lock);
#endif
    return RT_EOK;
}

rt_err_t haptic_service_preview(uint8_t amplitude_percent,
                                haptic_effect_t effect)
{
#ifdef PKG_USING_AW86224
    rt_err_t result = RT_EOK;

    if (!haptic_ready || haptic_lock == RT_NULL)
        return -RT_ENOSYS;
    if (amplitude_percent > 100U || effect >= HAPTIC_EFFECT_COUNT)
        return -RT_EINVAL;

    rt_mutex_take(haptic_lock, RT_WAITING_FOREVER);
    if (haptic_alert_active)
    {
        result = -RT_EBUSY;
    }
    else
    {
        haptic_amplitude = amplitude_percent;
        haptic_effect = effect;
        if (haptic_enabled && haptic_amplitude > 0U)
            result = haptic_play_locked(haptic_effect);
    }
    rt_mutex_release(haptic_lock);
    return result;
#else
    (void)amplitude_percent;
    (void)effect;
    return -RT_ENOSYS;
#endif
}

rt_err_t haptic_service_play_key_feedback(void)
{
#ifdef PKG_USING_AW86224
    rt_err_t result = RT_EOK;

    if (!haptic_ready || haptic_lock == RT_NULL)
        return -RT_ENOSYS;

    rt_mutex_take(haptic_lock, RT_WAITING_FOREVER);
    if (haptic_enabled && haptic_amplitude > 0U && !haptic_alert_active)
        result = haptic_play_locked(haptic_effect);
    rt_mutex_release(haptic_lock);
    return result;
#else
    return -RT_ENOSYS;
#endif
}

rt_err_t haptic_service_play_alert(uint32_t duration_ms)
{
#ifdef PKG_USING_AW86224
    rt_thread_t thread;
    rt_err_t result;

    if (!haptic_ready)
        return -RT_ENOSYS;
    if (duration_ms == 0U)
        return -RT_EINVAL;
    if (!haptic_enabled || haptic_amplitude == 0U)
        return RT_EOK;
    if (haptic_alert_active)
        return -RT_EBUSY;

    haptic_alert_duration_ms = duration_ms;
    haptic_alert_active = true;
    thread = rt_thread_create("hap_alert", haptic_alert_entry, RT_NULL,
                              HAPTIC_ALERT_STACK_SIZE,
                              RT_THREAD_PRIORITY_LOW, 10U);
    if (thread == RT_NULL)
    {
        haptic_alert_active = false;
        return -RT_ENOMEM;
    }
    result = rt_thread_startup(thread);
    if (result != RT_EOK)
    {
        haptic_alert_active = false;
        rt_thread_delete(thread);
    }
    return result;
#else
    (void)duration_ms;
    return -RT_ENOSYS;
#endif
}
