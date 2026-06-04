#include "rtthread.h"
#include "rtconfig.h"
#include "bf0_hal.h"
#include "drv_io.h"
#include "stdio.h"
#include "string.h"
#include "ulog.h"
#include "xl9555.h"

#define LOD_PIN 41

/**
 * @brief  Main program
 * @param  None
 * @retval 0 if success, otherwise failure number
 */

static void i2c_scan()
{
    uint8_t i2c_address[8];
    struct rt_i2c_bus_device *i2c_bus = rt_i2c_bus_device_find("i2c1");
    if (i2c_bus == RT_NULL)
    {
        LOG_E("I2C bus %s not found!\n", "i2c1");
        return;
    }

    if (rt_device_open((rt_device_t)i2c_bus, RT_DEVICE_OFLAG_RDWR) != RT_EOK)
    {
        LOG_E("open %s device failed", "i2c1");
        return;
    }

    struct rt_i2c_configuration configuration = {
        .timeout = 500,
        .max_hz = 100000,
    };

    rt_i2c_configure(i2c_bus, &configuration);

    rt_uint8_t found = 0;
    LOG_D("I2C Bus scanning...\n");

    for (uint8_t addr = 0x00; addr <= 0x7f; addr++)
    {
        struct rt_i2c_msg msgs;
        rt_uint8_t dummy = 0x00;

        msgs.addr = addr;
        msgs.flags = RT_I2C_WR;
        msgs.buf = &dummy;
        msgs.len = 1;

        if (rt_i2c_transfer(i2c_bus, &msgs, 1) == 1)
        {
            i2c_address[found] = addr;
            found++;
        }
    }

    if (found == 0)
    {
        LOG_D("No I2C devices found on bus i2c1\n");
    }
    else
    {
        LOG_D("Total %d device(s) found\n", found);
        for (int i = 0; i < found; i++)
        {
            LOG_D("Device %d: 0x%02X", i + 1, i2c_address[i]);
        }
    }
}
MSH_CMD_EXPORT(i2c_scan, i2c scan);


int main(void)
{
    rt_thread_mdelay(2000);
    HAL_PIN_Set(PAD_PA41, GPIO_A41, PIN_NOPULL, 1); // set PA41 to GPIO funtion
    rt_pin_mode(LOD_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(LOD_PIN, PIN_HIGH);


    rt_err_t ret = RT_EOK;
    xl9555_init();

    for (int i = 0; i < 15; i++)
    {
        xl9555_pin_mode(i, XL9555_PIN_OUTPUT);
    }
    static int out_pin = 0;

    while (1)
    {        
        xl9555_digital_write(out_pin, 1); // P0_0 输出高电平
        LOG_D("out_pin: %d is HIGH", out_pin);
        rt_thread_mdelay(1000);
        xl9555_digital_write(out_pin, 0); // P0_0 输出高电平
        LOG_D("out_pin: %d is LOW", out_pin);
        rt_thread_mdelay(1000);
        out_pin++;
        if (out_pin >= 18)
        {
            out_pin = 0;
        }
        
    }
}
