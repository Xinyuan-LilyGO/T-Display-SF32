#include <rtthread.h>
#include <ulog.h>
#include "app_init.h"

int main(void)
{
    rt_err_t result;

    LOG_I("T-SF32 factory application starting");

    /*
     * 等待扩展板电源、I2C 器件和显示电源稳定。
     * 后续可以改为由 platform_power_init() 内部处理。
     */
    rt_thread_mdelay(100);

    result = app_init();
    if (result != RT_EOK)
    {
        LOG_E("Application initialization failed: %d", result);
    }

    LOG_I("T-SF32 factory application started");

    while (1)
    {
        /*
         * 实际工作应放在线程、定时器和设备回调中，
         * main 线程只保留低频健康检查。
         */
        app_update_status_bar();
        rt_thread_mdelay(1000);
    }
}
