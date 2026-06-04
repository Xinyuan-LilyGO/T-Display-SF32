#include "rtthread.h"
#include "rtconfig.h"
#include "bf0_hal.h"
#include "drv_io.h"
#include "stdio.h"
#include "string.h"
#include "ulog.h"
#include "bme280.h"
#include "xl9555.h"

#define LOD_PIN 41
#define XL9555_WIFI_EN_PIN 2 //BME280 use WIFI_3V3 power

static bme280_device_t *bme;
/**
 * @brief  Main program
 * @param  None
 * @retval 0 if success, otherwise failure number
 */
int main(void)
{
    HAL_PIN_Set(PAD_PA41, GPIO_A41, PIN_NOPULL, 1); // set PA41 to GPIO funtion
    rt_pin_mode(LOD_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(LOD_PIN, PIN_HIGH);

    float temp, hum,press;
    xl9555_init();

    xl9555_pin_mode(XL9555_WIFI_EN_PIN, XL9555_PIN_OUTPUT);
    xl9555_digital_write(XL9555_WIFI_EN_PIN, 0);
    rt_thread_mdelay(100);
    xl9555_digital_write(XL9555_WIFI_EN_PIN, 1);
    rt_thread_mdelay(500);

    int ret = bme280_init();
    if (ret != RT_EOK)
    {
        rt_kprintf("BME280 init failed\n");
    }

    bme = bme280_get_device();

    while (1)
    {
        /* 读取一次数据 */
        if (bme280_read_all(bme, &temp, &press, &hum) == RT_EOK)
        {
            rt_kprintf("Temperature: %.2f °C\n", temp);
            rt_kprintf("Pressure   : %.2f Pa (%.2f hPa)\n", press, press / 100.0f);
            rt_kprintf("Humidity   : %.2f %%\n", hum);
        }
        else
        {
            rt_kprintf("Read failed\n");
        }
        rt_thread_mdelay(3000);
    }
}
