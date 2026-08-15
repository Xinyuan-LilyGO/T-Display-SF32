#include "bf0_hal.h"
#include "drv_io.h"
#include "l76k_rt_sensor.h"
#include "rtdevice.h"
#include "rtthread.h"
#include "sensor.h"
#include "uart_mux.h"

#define DBG_TAG "l76k.app"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define POWER_HOLD_PIN 41
#define L76K_DEVICE_NAME "gps_" L76K_RT_SENSOR_NAME
#define SAMPLE_PERIOD_MS 1000U

static void board_power_hold(void)
{
    HAL_PIN_Set(PAD_PA41, GPIO_A41, PIN_NOPULL, 1);
    rt_pin_mode(POWER_HOLD_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(POWER_HOLD_PIN, PIN_HIGH);
}

static rt_err_t select_l76k_uart(void)
{
    return uart_mux_switch_to(UART_MUX_DEVICE_GPS, L76K_RT_BAUDRATE);
}

int main(void)
{
    rt_device_t gps_device;
    struct rt_sensor_data sample;
    GPSInfo info;
    rt_err_t result;

    board_power_hold();

    result = uart_mux_init(UART_BUS_NAME);
    if (result != RT_EOK)
    {
        LOG_E("initialize UART mux on '%s' failed: %d", UART_BUS_NAME,
              result);
        return result;
    }

    gps_device = rt_device_find(L76K_DEVICE_NAME);
    if (gps_device == RT_NULL)
    {
        LOG_E("sensor device '%s' was not registered", L76K_DEVICE_NAME);
        return -RT_ENOSYS;
    }

    result = rt_device_open(gps_device, RT_DEVICE_FLAG_RDONLY);
    if (result != RT_EOK)
    {
        LOG_E("open sensor device '%s' failed: %d", L76K_DEVICE_NAME,
              result);
        return result;
    }

    result = rt_device_control(gps_device, RT_SENSOR_CTRL_SET_MODE,
                               (void *)(rt_ubase_t)RT_SENSOR_MODE_POLLING);
    if (result == RT_EOK)
        result = rt_device_control(
            gps_device, RT_SENSOR_CTRL_SET_POWER,
            (void *)(rt_ubase_t)RT_SENSOR_POWER_NORMAL);
    if (result != RT_EOK)
    {
        LOG_E("configure L76K sensor failed: %d", result);
        rt_device_close(gps_device);
        return result;
    }

    LOG_I("L76K sensor '%s' opened on %s at %u baud", L76K_DEVICE_NAME,
          UART_BUS_NAME, (unsigned int)L76K_RT_BAUDRATE);

    while (1)
    {
        rt_uint32_t rx_bytes;

        /* UART2 is shared, so select the GPS path before every transaction. */
        result = select_l76k_uart();
        if (result == RT_EOK)
        {
            rt_memset(&sample, 0, sizeof(sample));
            if (rt_device_read(gps_device, 0, &sample, 1) != 1)
            {
                LOG_W("no L76K sample available");
            }
            else if (l76k_get_gps_info(&info) != 0)
            {
                LOG_W("read extended L76K information failed");
            }
            else if (!info.valid)
            {
                rx_bytes = l76k_get_rx_bytes();
                LOG_I("waiting for GPS fix, rx=%u bytes, satellites=%u",
                      (unsigned int)rx_bytes,
                      (unsigned int)info.satellites);
            }
            else
            {
                LOG_I("UTC %04d-%02d-%02d %02d:%02d:%02d, "
                      "lat=%.6f, lon=%.6f, alt=%.2f m, sats=%u",
                      info.time.year, info.time.month, info.time.day,
                      info.time.hour, info.time.minute, info.time.second,
                      sample.data.gps.lati, sample.data.gps.longi,
                      sample.data.gps.alti,
                      (unsigned int)info.satellites);
            }
        }
        else
        {
            LOG_E("switch UART mux to L76K failed: %d", result);
        }

        rt_thread_mdelay(SAMPLE_PERIOD_MS);
    }
}
