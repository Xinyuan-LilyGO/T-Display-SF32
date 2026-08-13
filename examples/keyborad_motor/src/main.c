#include "rtthread.h"
#include "rtconfig.h"
#include "bf0_hal.h"
#include "drv_io.h"
#include "stdio.h"
#include "string.h"
#include "ulog.h"
#include "aw86224.h"
#include "xl9555.h"

int main(void)
{
    rt_err_t ret = RT_EOK;

    ret = xl9555_init();
    if (ret != RT_EOK)
    {
        LOG_D("xl9555_init failed\n");
    }

    ret = aw86224_init();
    if (ret != RT_EOK)
    {
        LOG_D("aw86224_init failed\n");
    }

    aw86224_set_gain(0xff);

    while (1)
    {
        log_d("play ram 1 long\n");
        aw86224_play_ram_long(1, 14,RT_TRUE);
        rt_thread_mdelay(1000);
        log_d("play ram 2 long\n");
        aw86224_play_ram_long(2, 14,RT_TRUE);
        rt_thread_mdelay(1000);
        log_d("play ram 3 long\n");
        aw86224_play_ram_long(3, 14,RT_TRUE);
        rt_thread_mdelay(1000);
        log_d("play ram 4 long\n");
        aw86224_play_ram_long(4, 14,RT_TRUE);
        rt_thread_mdelay(1000);
    }
}
