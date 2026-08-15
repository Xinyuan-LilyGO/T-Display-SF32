#include "bf0_hal.h"
#include "bme280_rt_sensor.h"
#include "drv_io.h"
#include "rtdevice.h"
#include "rtthread.h"
#include "sensor.h"
#include "xl9555.h"

#define DBG_TAG "bme280.app"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define POWER_HOLD_PIN 41
#define SAMPLE_PERIOD_MS 3000

#define BME280_TEMP_DEVICE_NAME "temp_" BME280_RT_SENSOR_NAME
#define BME280_HUMI_DEVICE_NAME "humi_" BME280_RT_SENSOR_NAME
#define BME280_BARO_DEVICE_NAME "baro_" BME280_RT_SENSOR_NAME

static rt_err_t board_sensor_power_on(void)
{
    rt_err_t result;

    HAL_PIN_Set(PAD_PA41, GPIO_A41, PIN_NOPULL, 1);
    rt_pin_mode(POWER_HOLD_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(POWER_HOLD_PIN, PIN_HIGH);

    result = xl9555_init();
    if (result != RT_EOK)
        return result;

    /* BME280 is powered by the board's WIFI_3V3 rail. */
    xl9555_pin_mode(XL9555_WIFI_EN_PIN, XL9555_PIN_OUTPUT);
    xl9555_digital_write(XL9555_WIFI_EN_PIN, 0);
    rt_thread_mdelay(100);
    xl9555_digital_write(XL9555_WIFI_EN_PIN, 1);
    rt_thread_mdelay(500);
    return RT_EOK;
}

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

static rt_err_t sensor_read_one(rt_device_t device,
                                struct rt_sensor_data *data)
{
    rt_memset(data, 0, sizeof(*data));
    return (rt_device_read(device, 0, data, 1) == 1) ? RT_EOK : -RT_EIO;
}

static void print_sample(const struct rt_sensor_data *temperature,
                         const struct rt_sensor_data *humidity,
                         const struct rt_sensor_data *pressure)
{
    rt_int32_t temp = temperature->data.temp;
    rt_uint32_t temp_abs = (temp < 0) ? (rt_uint32_t)(-temp)
                                      : (rt_uint32_t)temp;
    rt_uint32_t humi = (rt_uint32_t)humidity->data.humi;
    rt_uint32_t baro = (rt_uint32_t)pressure->data.baro;

    rt_kprintf("Temperature: %s%u.%u C, Humidity: %u.%u %%RH, "
               "Pressure: %u Pa (%u.%02u hPa)\n",
               (temp < 0) ? "-" : "", temp_abs / 10, temp_abs % 10,
               humi / 10, humi % 10, baro, baro / 100, baro % 100);
}

int main(void)
{
    rt_device_t temp_device = RT_NULL;
    rt_device_t humi_device = RT_NULL;
    rt_device_t baro_device = RT_NULL;
    struct rt_sensor_data temperature;
    struct rt_sensor_data humidity;
    struct rt_sensor_data pressure;
    rt_uint8_t chip_id = 0;
    rt_err_t result;

    rt_thread_mdelay(1000);

    result = board_sensor_power_on();
    if (result != RT_EOK)
    {
        LOG_E("power on BME280 failed: %d", result);
        return result;
    }

    result = sensor_open(BME280_TEMP_DEVICE_NAME, &temp_device);
    if (result != RT_EOK)
        goto exit;
    result = sensor_open(BME280_HUMI_DEVICE_NAME, &humi_device);
    if (result != RT_EOK)
        goto exit;
    result = sensor_open(BME280_BARO_DEVICE_NAME, &baro_device);
    if (result != RT_EOK)
        goto exit;

    result = rt_device_control(temp_device, RT_SENSOR_CTRL_GET_ID, &chip_id);
    if (result != RT_EOK)
    {
        LOG_E("read BME280 chip ID failed: %d", result);
        goto exit;
    }
    LOG_I("BME280 sensor devices opened, chip ID: 0x%02x", chip_id);

    while (1)
    {
        result = sensor_read_one(temp_device, &temperature);
        if (result == RT_EOK)
            result = sensor_read_one(humi_device, &humidity);
        if (result == RT_EOK)
            result = sensor_read_one(baro_device, &pressure);

        if (result == RT_EOK)
            print_sample(&temperature, &humidity, &pressure);
        else
            LOG_E("read BME280 sensor data failed: %d", result);

        rt_thread_mdelay(SAMPLE_PERIOD_MS);
    }

exit:
    if (baro_device != RT_NULL)
        rt_device_close(baro_device);
    if (humi_device != RT_NULL)
        rt_device_close(humi_device);
    if (temp_device != RT_NULL)
        rt_device_close(temp_device);
    return result;
}
