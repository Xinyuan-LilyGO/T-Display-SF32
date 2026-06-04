#include <rtthread.h>
#include "rtconfig.h"
#include "bf0_hal.h"
#include "drv_io.h"
#include "stdio.h"
#include "string.h"
#include "ulog.h"
#include "xl9555.h"
#include "esp32c6_at_cmd.h"
#include "uart_mux.h"
#include "sgm41562b.h"

#define LOD_PIN 41

static void at_callback(int resp_id, const char *response, int result)
{
    if (resp_id == AT_RESP_ID_TEST)
    {
        if (result == AT_RESULT_OK)
            rt_kprintf("%s\n", response);
    }
    else if (resp_id == AT_RESP_ID_CWJAP)
    {
        /* 多行响应: WIFI CONNECTED\nWIFI GOT IP\nOK */
        if (result == AT_RESULT_OK)
        {
            rt_kprintf("WiFi connected!\n");
            esp32_at_sntp_cfg(AT_RESP_ID_SNTPCFG, true, 8, "cn.ntp.org.cn");
        }
    }
    else if (resp_id == AT_RESP_ID_SNTPCFG)
    {
        if (result == AT_RESULT_OK)
        {
            rt_kprintf("SNTP config OK\n");
            esp32_at_sntp_time_q(AT_RESP_ID_SNTPTIME_Q);
        }
    }
    else if (resp_id == -1)
    {
        rt_kprintf("URC: %s\n", response); /* 主动上报 */
    }
}

int main(void)
{
    HAL_PIN_Set(PAD_PA41, GPIO_A41, PIN_NOPULL, 1);
    rt_pin_mode(LOD_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(LOD_PIN, PIN_HIGH);

    sgm41562b_handle_t charger;
    charger = sgm41562b_init(SGM41562B_I2C_BUS_NAME, SGM41562B_IRQ_PIN);
    if (charger == RT_NULL)
    {
        rt_kprintf("Charger init failed\n");
    }

    xl9555_init();
    uart_mux_init(UART_BUS_NAME);
    at_async_register_callback(at_callback);
    at_async_init(UART_BUS_NAME, 115200);

    rt_thread_mdelay(2000);
    esp32_at_test(AT_RESP_ID_TEST);
    esp32_at_cwinit(AT_RESP_ID_CWINIT);
    esp32_at_cwmode(AT_RESP_ID_CWMODE, 3);
    esp32_at_cwjap(AT_RESP_ID_CWJAP, "xinyuandianzi", "AA15994823428");

    while (1)
    {
        esp32_at_sntp_time_q(AT_RESP_ID_SNTPTIME_Q);
        rt_thread_mdelay(5000);
    }
}