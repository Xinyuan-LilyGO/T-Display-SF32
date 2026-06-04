#include <rtthread.h>
#include "rtconfig.h"
#include "bf0_hal.h"
#include "drv_io.h"
#include "stdio.h"
#include "string.h"
#include "ulog.h"
#include "xl9555.h"
#include "l76k.h"
#include "TinyGPS.h"
#include "uart_mux.h"

#define LOD_PIN 41
void gps_test_thread(void *param);
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
    uart_mux_init(UART_BUS_NAME);
    if (l76k_init(UART_BUS_NAME, 115200) != RT_EOK)
    {
        rt_kprintf("GPS init failed\n");
        return -1;
    }

    rt_thread_t gps_tid = rt_thread_create("gps_test_thread", gps_test_thread, NULL,
                                       2048, 20, 5);
    if (gps_tid)
        rt_thread_startup(gps_tid);

    while (1)
    {
        rt_thread_mdelay(1000);
    }
}

void gps_test_thread(void *param)
{
    GPSInfo gps_info;
    while (1)
    {
        if (l76k_is_fixed())
        {
            l76k_get_gps_info(&gps_info);
            log_d("Time: %04d-%02d-%02d %02d:%02d:%02d UTC\n", gps_info.time.year, gps_info.time.month, gps_info.time.day,
                  gps_info.time.hour, gps_info.time.minute, gps_info.time.second);
            log_d("Latitude: %f, Longitude: %f\n", gps_info.latitude, gps_info.longitude);
            log_d("Altitude: %.2f m\n", gps_info.altitude);
            log_d("Speed: %.2f km/h\n", gps_info.speed);
            log_d("Satellite count: %d\n", gps_info.satellites);
            log_d("HDOP: %.2f\n", gps_info.hdop);
            log_d("Course: %.2f\n", gps_info.course);
        }
        else
        {
            log_d("GPS is not fixed\n");
        }
        rt_thread_mdelay(1000);
    }
}

