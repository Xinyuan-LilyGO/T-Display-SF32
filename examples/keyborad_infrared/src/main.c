#include "rtthread.h"
#include "rtconfig.h"
#include "bf0_hal.h"
#include "drv_io.h"
#include "stdio.h"
#include "string.h"
#include "ulog.h"
#include "infrared.h"
#include "ir_nec.h"
#include "xl9555.h"

#define LOD_PIN 41
#define XL9555_WIFI_EN_PIN 2 // BME280 use WIFI_3V3 power
#define MS_TO_NS(ms) ((rt_uint32_t)(ms) * 1000000UL)
#define US_TO_NS(us) ((rt_uint32_t)(us) * 1000UL)

int main(void)
{
    rt_err_t ret;

    HAL_PIN_Set(PAD_PA41, GPIO_A41, PIN_NOPULL, 1); // set PA41 to GPIO funtion
    rt_pin_mode(LOD_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(LOD_PIN, PIN_HIGH);

    ret = xl9555_init();
    if (ret != RT_EOK)
    {
        LOG_E("xl9555 init failed");
    }

    xl9555_pin_mode(XL9555_WIFI_EN_PIN, XL9555_PIN_OUTPUT);
    xl9555_digital_write(XL9555_WIFI_EN_PIN, 0);
    rt_thread_mdelay(100);
    xl9555_digital_write(XL9555_WIFI_EN_PIN, 1);
    rt_thread_mdelay(500);

    if (ir_nec_init() != 0)
    {
        rt_kprintf("IR init failed\n");
        return -1;
    }

    while (1)
    {
        // 发送地址 0x00，命令 0x45（例如电视机电源键）
        ir_nec_send(0x00, 0x45);
        log_d("send infrared signal");
        rt_thread_mdelay(2000); // Wait for 2 seconds before sending the next signal
    }
}
