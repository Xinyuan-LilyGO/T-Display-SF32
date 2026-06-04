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

    aw86224_set_gain(0x80);
    
    while (1)
    {
        log_d("play short\n");
        aw86224_play_ram_short(1, RT_TRUE);
        rt_thread_mdelay(2000);
        log_d("play long\n");
        aw86224_play_ram_long(4, 3, RT_TRUE);
        rt_thread_mdelay(2000);
    }
}
