#include "aw86224_rt_device.h"
#include <rtthread.h>
#include "xl9555.h"

#define DBG_TAG "motor.app"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define EFFECT_PERIOD_MS 1500U
#define EFFECT_GAIN 0x8FU

int main(void)
{
    rt_thread_mdelay(1000);
    rt_device_t motor_device;
    struct aw86224_rt_ram_effect effect = {
        .wave_id = 1,
        .loop = 14,
        .auto_brake = RT_TRUE,
    };
    struct aw86224_rt_status status;
    rt_uint8_t gain = EFFECT_GAIN;
    rt_err_t result;

    motor_device = rt_device_find(AW86224_RT_DEVICE_NAME);
    if (motor_device == RT_NULL)
    {
        LOG_E("motor device '%s' was not registered",
              AW86224_RT_DEVICE_NAME);
        return -RT_ENOSYS;
    }

    result = rt_device_open(motor_device, RT_DEVICE_FLAG_RDWR);
    if (result != RT_EOK)
    {
        LOG_E("open motor device '%s' failed: %d",
              AW86224_RT_DEVICE_NAME, result);
        return result;
    }

    result = rt_device_control(motor_device, AW86224_RT_CTRL_SET_GAIN,
                               &gain);
    if (result != RT_EOK)
    {
        LOG_E("set motor gain failed: %d", result);
        rt_device_close(motor_device);
        return result;
    }

    LOG_I("motor device '%s' opened, gain=0x%02x",
          AW86224_RT_DEVICE_NAME, gain);

    while (1)
    {
        result = rt_device_control(motor_device,
                                   AW86224_RT_CTRL_PLAY_RAM, &effect);
        if (result != RT_EOK)
        {
            LOG_E("play RAM effect %u failed: %d", effect.wave_id, result);
        }
        else
        {
            rt_thread_mdelay(100);
            rt_memset(&status, 0, sizeof(status));
            if (rt_device_read(motor_device, 0, &status, 1) == 1)
            {
                LOG_I("effect=%u, state=0x%02x, gain=0x%02x, playing=%u",
                      effect.wave_id, status.state, status.gain,
                      status.playing);
            }
        }

        effect.wave_id = (effect.wave_id >= 4U) ? 1U
                                                 : effect.wave_id + 1U;
        rt_thread_mdelay(EFFECT_PERIOD_MS);
    }
}
