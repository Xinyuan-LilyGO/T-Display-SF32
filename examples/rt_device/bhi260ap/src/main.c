#include "bhi260ap_rt_sensor.h"
#include "rtdevice.h"
#include "rtthread.h"
#include "sensor.h"

#define DBG_TAG "bhi260.app"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define BHI260AP_ACCE_DEVICE_NAME "acce_" BHI260AP_RT_SENSOR_NAME
#define BHI260AP_GYRO_DEVICE_NAME "gyro_" BHI260AP_RT_SENSOR_NAME
#define BHI260AP_STEP_DEVICE_NAME "step_" BHI260AP_RT_SENSOR_NAME

#define BHI260AP_SAMPLE_ODR_HZ 100U
#define BHI260AP_WARMUP_MS 300U
#define SAMPLE_PERIOD_MS 500U

static rt_err_t sensor_open(const char *name, rt_device_t *device)
{
    rt_err_t result;

    *device = rt_device_find(name);
    if (*device == RT_NULL)
    {
        LOG_E("sensor device '%s' was not found", name);
        return -RT_ENOSYS;
    }

    result = rt_device_open(*device, RT_DEVICE_FLAG_RDONLY);
    if (result != RT_EOK)
    {
        LOG_E("open sensor device '%s' failed: %d", name, result);
        *device = RT_NULL;
    }
    return result;
}

static rt_err_t sensor_set_odr(rt_device_t device, rt_uint32_t odr_hz)
{
    return rt_device_control(device, RT_SENSOR_CTRL_SET_ODR,
                             (void *)(rt_ubase_t)odr_hz);
}

static rt_err_t sensor_read_one(rt_device_t device,
                                struct rt_sensor_data *data)
{
    rt_memset(data, 0, sizeof(*data));
    return (rt_device_read(device, 0, data, 1) == 1) ? RT_EOK : -RT_EIO;
}

static void print_sample(const struct rt_sensor_data *acceleration,
                         const struct rt_sensor_data *gyroscope,
                         const struct rt_sensor_data *step)
{
    rt_kprintf("Accel[mg] x=%d y=%d z=%d | "
               "Gyro[mdps] x=%d y=%d z=%d | Steps=%u\n",
               acceleration->data.acce.x,
               acceleration->data.acce.y,
               acceleration->data.acce.z,
               gyroscope->data.gyro.x,
               gyroscope->data.gyro.y,
               gyroscope->data.gyro.z,
               step->data.step);
}

int main(void)
{
    rt_device_t acce_device = RT_NULL;
    rt_device_t gyro_device = RT_NULL;
    rt_device_t step_device = RT_NULL;
    struct rt_sensor_data acceleration;
    struct rt_sensor_data gyroscope;
    struct rt_sensor_data step;
    rt_err_t result;

    rt_thread_mdelay(1000);

    result = sensor_open(BHI260AP_ACCE_DEVICE_NAME, &acce_device);
    if (result != RT_EOK)
        goto exit;
    result = sensor_open(BHI260AP_GYRO_DEVICE_NAME, &gyro_device);
    if (result != RT_EOK)
        goto exit;
    result = sensor_open(BHI260AP_STEP_DEVICE_NAME, &step_device);
    if (result != RT_EOK)
        goto exit;

    result = sensor_set_odr(acce_device, BHI260AP_SAMPLE_ODR_HZ);
    if (result == RT_EOK)
        result = sensor_set_odr(gyro_device, BHI260AP_SAMPLE_ODR_HZ);
    if (result == RT_EOK)
        result = sensor_set_odr(step_device, BHI260AP_SAMPLE_ODR_HZ);
    if (result != RT_EOK)
    {
        LOG_E("configure BHI260AP ODR failed: %d", result);
        goto exit;
    }

    LOG_I("BHI260AP sensor devices opened at %u Hz",
          BHI260AP_SAMPLE_ODR_HZ);
    rt_thread_mdelay(BHI260AP_WARMUP_MS);

    while (1)
    {
        result = sensor_read_one(acce_device, &acceleration);
        if (result == RT_EOK)
            result = sensor_read_one(gyro_device, &gyroscope);
        if (result == RT_EOK)
            result = sensor_read_one(step_device, &step);

        if (result == RT_EOK)
            print_sample(&acceleration, &gyroscope, &step);
        else
            LOG_E("read BHI260AP sensor data failed: %d", result);

        rt_thread_mdelay(SAMPLE_PERIOD_MS);
    }

exit:
    if (step_device != RT_NULL)
        rt_device_close(step_device);
    if (gyro_device != RT_NULL)
        rt_device_close(gyro_device);
    if (acce_device != RT_NULL)
        rt_device_close(acce_device);
    return result;
}
