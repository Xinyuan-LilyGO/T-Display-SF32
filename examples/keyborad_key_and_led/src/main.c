#include "rtthread.h"
#include "rtconfig.h"
#include "bf0_hal.h"
#include "drv_io.h"
#include "stdio.h"
#include "string.h"
#include "ulog.h"

#ifdef PKG_USING_PKG_KEY_BOARD
#include "tca8418.h"
#include "aw21009.h"
#endif
#include "xl9555.h"

#define LOD_PIN 41
void key_led_heart_thread_entry(void *parameter);

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

    xl9555_init();

    xl9555_pin_mode(XL9555_WIFI_EN_PIN, XL9555_PIN_OUTPUT);
    xl9555_digital_write(XL9555_WIFI_EN_PIN, 0);
    rt_thread_mdelay(100);
    xl9555_digital_write(XL9555_WIFI_EN_PIN, 1);
    rt_thread_mdelay(200);

    xl9555_pin_mode(XL9555_KEY_LED_EN_PIN, XL9555_PIN_OUTPUT);
    xl9555_digital_write(XL9555_KEY_LED_EN_PIN, 0);
    rt_thread_mdelay(100);
    xl9555_digital_write(XL9555_KEY_LED_EN_PIN, 1);
    rt_thread_mdelay(200);

    xl9555_pin_mode(XL9555_KEY_RST_PIN, XL9555_PIN_OUTPUT);
    xl9555_digital_write(XL9555_KEY_RST_PIN, 0);
    rt_thread_mdelay(100);
    xl9555_digital_write(XL9555_KEY_RST_PIN, 1);
    rt_thread_mdelay(200);

    if (aw21009_app_init() != RT_EOK)
    {
        LOG_D("aw21009_app_init failed!");
    }
    rt_thread_t tid =
        rt_thread_create("key_led_heart", key_led_heart_thread_entry, RT_NULL, 1024, 12, 10);
    rt_thread_startup(tid);
    if (key_board_tca8418_init() != RT_EOK)
    {
        LOG_D("key_board_tca8418_init failed!");
        return -RT_EOK;
    }

    static key_board_event_msg_t key_msg;
    static rt_mq_t my_key_mq;
    
    // 在使用前初始化
    if (my_key_mq == RT_NULL) {
        my_key_mq = key_board_get_mq();
    }

    while (1)
    {
        if (rt_mq_recv(my_key_mq, &key_msg, sizeof(key_board_event_msg_t), RT_WAITING_FOREVER) == RT_EOK)
        {
            LOG_D("key code : %d", key_msg.code);
        }
        rt_thread_mdelay(100);
    }
}

void key_led_heart_thread_entry(void *parameter)
{
    while (1)
    {
        for (int i = 0; i < 4096; i += 128)
        {
            aw21009_set_all_brightness(i);
            rt_thread_mdelay(40);
        }

        for (int i = 4095; i > 0; i -= 128)
        {
            aw21009_set_all_brightness(i);
            rt_thread_mdelay(40);
        }
    }
}

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