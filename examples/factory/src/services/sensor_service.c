#include "sensor_service.h"

#include <rtdevice.h>
#include <ulog.h>

#if defined(PKG_USING_BME280) && defined(BME280_USING_RT_SENSOR)
#define FACTORY_USING_BME280
#include "bme280_rt_sensor.h"
#endif

#if defined(PKG_USING_BHI260AP) && defined(BHI260AP_USING_RT_SENSOR)
#define FACTORY_USING_BHI260AP
#include "bhi260ap_rt_sensor.h"
#endif

#if defined(FACTORY_USING_BME280) || defined(FACTORY_USING_BHI260AP)
#define FACTORY_USING_SENSOR_DEVICES
#include <sensor.h>
#endif

#define SENSOR_SAMPLE_PERIOD_MS 500U
#define SENSOR_THREAD_STACK_SIZE 1536U
#define SENSOR_THREAD_PRIORITY   20U
#define SENSOR_THREAD_TICK       10U
#define BHI260AP_ODR_HZ          100U
#define BHI260AP_WARMUP_MS       300U
#define SENSOR_STOP_TIMEOUT_MS   1000U

#ifdef FACTORY_USING_SENSOR_DEVICES
#define SENSOR_DEVICE_COUNT 6U

enum sensor_device_index
{
    SENSOR_TEMP = 0,
    SENSOR_HUMI,
    SENSOR_BARO,
    SENSOR_ACCE,
    SENSOR_GYRO,
    SENSOR_STEP,
};

static const char *const sensor_device_names[SENSOR_DEVICE_COUNT] =
{
#ifdef FACTORY_USING_BME280
    "temp_" BME280_RT_SENSOR_NAME,
    "humi_" BME280_RT_SENSOR_NAME,
    "baro_" BME280_RT_SENSOR_NAME,
#else
    RT_NULL,
    RT_NULL,
    RT_NULL,
#endif
#ifdef FACTORY_USING_BHI260AP
    "acce_" BHI260AP_RT_SENSOR_NAME,
    "gyro_" BHI260AP_RT_SENSOR_NAME,
    "step_" BHI260AP_RT_SENSOR_NAME,
#else
    RT_NULL,
    RT_NULL,
    RT_NULL,
#endif
};

static rt_device_t sensor_devices[SENSOR_DEVICE_COUNT];
static rt_mutex_t sensor_lock;
static rt_sem_t sensor_stop_sem;
static rt_thread_t sensor_thread;
static sensor_service_data_t sensor_latest;
static uint32_t sensor_available_mask;
static uint32_t sensor_init_errors;
static bool sensor_initialized;
static volatile bool sensor_stopping;

static rt_err_t sensor_read_one(enum sensor_device_index index,
                                struct rt_sensor_data *data)
{
    rt_memset(data, 0, sizeof(*data));
    if (sensor_devices[index] == RT_NULL)
        return -RT_ENOSYS;

    return rt_device_read(sensor_devices[index], 0, data, 1) == 1
               ? RT_EOK
               : -RT_EIO;
}

static uint32_t sensor_collect(sensor_service_data_t *sample)
{
    struct rt_sensor_data data;
    uint32_t valid_mask = 0;

    if (sensor_read_one(SENSOR_TEMP, &data) == RT_EOK)
    {
        sample->temperature = data.data.temp;
        valid_mask |= SENSOR_DATA_TEMPERATURE;
    }
    if (sensor_read_one(SENSOR_HUMI, &data) == RT_EOK)
    {
        sample->humidity = data.data.humi;
        valid_mask |= SENSOR_DATA_HUMIDITY;
    }
    if (sensor_read_one(SENSOR_BARO, &data) == RT_EOK)
    {
        sample->pressure = data.data.baro;
        valid_mask |= SENSOR_DATA_PRESSURE;
    }
    if (sensor_read_one(SENSOR_ACCE, &data) == RT_EOK)
    {
        sample->acceleration[0] = data.data.acce.x;
        sample->acceleration[1] = data.data.acce.y;
        sample->acceleration[2] = data.data.acce.z;
        valid_mask |= SENSOR_DATA_ACCELEROMETER;
    }
    if (sensor_read_one(SENSOR_GYRO, &data) == RT_EOK)
    {
        sample->gyroscope[0] = data.data.gyro.x;
        sample->gyroscope[1] = data.data.gyro.y;
        sample->gyroscope[2] = data.data.gyro.z;
        valid_mask |= SENSOR_DATA_GYROSCOPE;
    }
    if (sensor_read_one(SENSOR_STEP, &data) == RT_EOK)
    {
        sample->step_count = data.data.step;
        valid_mask |= SENSOR_DATA_STEP_COUNTER;
    }

    sample->sample_tick = rt_tick_get();
    return valid_mask;
}

static void sensor_update_latest(const sensor_service_data_t *sample,
                                 uint32_t updated_mask)
{
    if (rt_mutex_take(sensor_lock, RT_WAITING_FOREVER) != RT_EOK)
        return;

    if ((updated_mask & SENSOR_DATA_TEMPERATURE) != 0U)
        sensor_latest.temperature = sample->temperature;
    if ((updated_mask & SENSOR_DATA_HUMIDITY) != 0U)
        sensor_latest.humidity = sample->humidity;
    if ((updated_mask & SENSOR_DATA_PRESSURE) != 0U)
        sensor_latest.pressure = sample->pressure;
    if ((updated_mask & SENSOR_DATA_ACCELEROMETER) != 0U)
    {
        sensor_latest.acceleration[0] = sample->acceleration[0];
        sensor_latest.acceleration[1] = sample->acceleration[1];
        sensor_latest.acceleration[2] = sample->acceleration[2];
    }
    if ((updated_mask & SENSOR_DATA_GYROSCOPE) != 0U)
    {
        sensor_latest.gyroscope[0] = sample->gyroscope[0];
        sensor_latest.gyroscope[1] = sample->gyroscope[1];
        sensor_latest.gyroscope[2] = sample->gyroscope[2];
    }
    if ((updated_mask & SENSOR_DATA_STEP_COUNTER) != 0U)
        sensor_latest.step_count = sample->step_count;

    sensor_latest.sample_tick = sample->sample_tick;
    sensor_latest.valid_mask = updated_mask;
    rt_mutex_release(sensor_lock);
}

static void sensor_service_thread(void *parameter)
{
    bool failure_reported = false;

    (void)parameter;
    while (!sensor_stopping)
    {
        sensor_service_data_t sample;
        uint32_t updated_mask;

        rt_memset(&sample, 0, sizeof(sample));
        updated_mask = sensor_collect(&sample);
        sensor_update_latest(&sample, updated_mask);

        if ((updated_mask & sensor_available_mask) != sensor_available_mask &&
                !failure_reported)
        {
            LOG_W("Sensor sample incomplete: valid=0x%02x",
                  (unsigned int)updated_mask);
            failure_reported = true;
        }
        else if ((updated_mask & sensor_available_mask) ==
                     sensor_available_mask && failure_reported)
        {
            LOG_I("All sensor readings recovered");
            failure_reported = false;
        }

        rt_thread_mdelay(SENSOR_SAMPLE_PERIOD_MS);
    }

    rt_sem_release(sensor_stop_sem);
}

static void sensor_close_devices(void)
{
    rt_size_t index;

    for (index = SENSOR_DEVICE_COUNT; index > 0; index--)
    {
        if (sensor_devices[index - 1] != RT_NULL)
        {
            rt_device_close(sensor_devices[index - 1]);
            sensor_devices[index - 1] = RT_NULL;
        }
    }
}

static void sensor_close_range(rt_size_t first, rt_size_t last)
{
    rt_size_t index;

    for (index = first; index <= last; index++)
    {
        if (sensor_devices[index] != RT_NULL)
        {
            rt_device_close(sensor_devices[index]);
            sensor_devices[index] = RT_NULL;
        }
    }
}

static rt_err_t sensor_open_range(rt_size_t first, rt_size_t last)
{
    rt_size_t index;
    rt_err_t result;

    for (index = first; index <= last; index++)
    {
        sensor_devices[index] = rt_device_find(sensor_device_names[index]);
        if (sensor_devices[index] == RT_NULL)
        {
            LOG_E("Sensor device '%s' not found", sensor_device_names[index]);
            sensor_close_range(first, index);
            return -RT_ENOSYS;
        }

        result = rt_device_open(sensor_devices[index], RT_DEVICE_FLAG_RDONLY);
        if (result != RT_EOK)
        {
            LOG_E("Open sensor device '%s' failed: %d",
                  sensor_device_names[index], result);
            sensor_devices[index] = RT_NULL;
            sensor_close_range(first, index);
            return result;
        }
    }

    return RT_EOK;
}

#ifdef FACTORY_USING_BHI260AP
static rt_err_t sensor_configure_bhi260ap(void)
{
    rt_size_t index;
    rt_err_t result;

    for (index = SENSOR_ACCE; index <= SENSOR_STEP; index++)
    {
        result = rt_device_control(sensor_devices[index],
                                   RT_SENSOR_CTRL_SET_ODR,
                                   (void *)(rt_ubase_t)BHI260AP_ODR_HZ);
        if (result != RT_EOK)
        {
            LOG_E("Set sensor '%s' ODR failed: %d",
                  sensor_device_names[index], result);
            return result;
        }
    }

    return RT_EOK;
}
#endif
#endif /* FACTORY_USING_SENSOR_DEVICES */

rt_err_t sensor_service_init(void)
{
#ifdef FACTORY_USING_SENSOR_DEVICES
    rt_err_t result;
#ifdef FACTORY_USING_BME280
    rt_err_t bme280_result;
#endif
#ifdef FACTORY_USING_BHI260AP
    rt_err_t bhi260ap_result;
#endif

    if (sensor_initialized)
        return RT_EOK;

    sensor_init_errors = SENSOR_INIT_ERROR_NONE;
    sensor_available_mask = 0;
    rt_memset(sensor_devices, 0, sizeof(sensor_devices));

    sensor_lock = rt_mutex_create("sensor", RT_IPC_FLAG_PRIO);
    if (sensor_lock == RT_NULL)
        return -RT_ENOMEM;

    sensor_stop_sem = rt_sem_create("sen_stop", 0, RT_IPC_FLAG_FIFO);
    if (sensor_stop_sem == RT_NULL)
    {
        result = -RT_ENOMEM;
        goto fail;
    }

#ifdef FACTORY_USING_BME280
    bme280_result = sensor_open_range(SENSOR_TEMP, SENSOR_BARO);
    if (bme280_result == RT_EOK)
    {
        sensor_available_mask |= SENSOR_DATA_TEMPERATURE |
                                 SENSOR_DATA_HUMIDITY |
                                 SENSOR_DATA_PRESSURE;
    }
    else
    {
        sensor_init_errors |= SENSOR_INIT_ERROR_BME280;
    }
#endif

#ifdef FACTORY_USING_BHI260AP
    bhi260ap_result = sensor_open_range(SENSOR_ACCE, SENSOR_STEP);
    if (bhi260ap_result == RT_EOK)
        bhi260ap_result = sensor_configure_bhi260ap();
    if (bhi260ap_result == RT_EOK)
    {
        sensor_available_mask |= SENSOR_DATA_ACCELEROMETER |
                                 SENSOR_DATA_GYROSCOPE |
                                 SENSOR_DATA_STEP_COUNTER;
        rt_thread_mdelay(BHI260AP_WARMUP_MS);
    }
    else
    {
        sensor_init_errors |= SENSOR_INIT_ERROR_BHI260AP;
        sensor_close_range(SENSOR_ACCE, SENSOR_STEP);
    }
#endif

    if (sensor_available_mask == 0U)
    {
        result = -RT_ENOSYS;
        goto fail;
    }

    rt_memset(&sensor_latest, 0, sizeof(sensor_latest));
    sensor_stopping = false;
    sensor_thread = rt_thread_create("sensor", sensor_service_thread,
                                     RT_NULL, SENSOR_THREAD_STACK_SIZE,
                                     SENSOR_THREAD_PRIORITY,
                                     SENSOR_THREAD_TICK);
    if (sensor_thread == RT_NULL)
    {
        result = -RT_ENOMEM;
        goto fail;
    }

    result = rt_thread_startup(sensor_thread);
    if (result != RT_EOK)
    {
        rt_thread_delete(sensor_thread);
        sensor_thread = RT_NULL;
        goto fail;
    }

    sensor_initialized = true;
    if (sensor_init_errors == SENSOR_INIT_ERROR_NONE)
    {
#if defined(FACTORY_USING_BME280) && defined(FACTORY_USING_BHI260AP)
        LOG_I("Sensor service initialized: BME280 and BHI260AP at %u Hz",
              BHI260AP_ODR_HZ);
#elif defined(FACTORY_USING_BHI260AP)
        LOG_I("Sensor service initialized: BHI260AP at %u Hz",
              BHI260AP_ODR_HZ);
#else
        LOG_I("Sensor service initialized: BME280");
#endif
    }
    else
    {
        LOG_W("Sensor service running in degraded mode, errors=0x%02x",
              (unsigned int)sensor_init_errors);
    }
    return RT_EOK;

fail:
    sensor_close_devices();
    if (sensor_stop_sem != RT_NULL)
    {
        rt_sem_delete(sensor_stop_sem);
        sensor_stop_sem = RT_NULL;
    }
    if (sensor_lock != RT_NULL)
    {
        rt_mutex_delete(sensor_lock);
        sensor_lock = RT_NULL;
    }
    return result;
#else
    LOG_E("No factory RT-Sensor adapter is enabled");
    return -RT_ENOSYS;
#endif
}

rt_err_t sensor_service_deinit(void)
{
#ifdef FACTORY_USING_SENSOR_DEVICES
    rt_err_t result;

    if (!sensor_initialized)
        return RT_EOK;

    sensor_stopping = true;
    result = rt_sem_take(sensor_stop_sem,
                         rt_tick_from_millisecond(SENSOR_STOP_TIMEOUT_MS));
    if (result != RT_EOK && sensor_thread != RT_NULL)
        rt_thread_delete(sensor_thread);
    sensor_thread = RT_NULL;

    sensor_close_devices();
    rt_sem_delete(sensor_stop_sem);
    sensor_stop_sem = RT_NULL;
    rt_mutex_delete(sensor_lock);
    sensor_lock = RT_NULL;
    sensor_initialized = false;
    rt_memset(&sensor_latest, 0, sizeof(sensor_latest));
    return RT_EOK;
#else
    return RT_EOK;
#endif
}

bool sensor_service_is_ready(void)
{
#ifdef FACTORY_USING_SENSOR_DEVICES
    return sensor_initialized;
#else
    return false;
#endif
}

uint32_t sensor_service_get_init_errors(void)
{
#ifdef FACTORY_USING_SENSOR_DEVICES
    return sensor_init_errors;
#else
    return SENSOR_INIT_ERROR_NONE;
#endif
}

rt_err_t sensor_service_get_data(sensor_service_data_t *data)
{
#ifdef FACTORY_USING_SENSOR_DEVICES
    rt_err_t result;

    if (data == RT_NULL)
        return -RT_EINVAL;
    if (!sensor_initialized || sensor_lock == RT_NULL)
        return -RT_ERROR;

    result = rt_mutex_take(sensor_lock, RT_WAITING_FOREVER);
    if (result != RT_EOK)
        return result;

    *data = sensor_latest;
    rt_mutex_release(sensor_lock);
    return data->valid_mask != 0U ? RT_EOK : -RT_ERROR;
#else
    (void)data;
    return -RT_ENOSYS;
#endif
}
