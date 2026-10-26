#include "rtthread.h"
#include "rtconfig.h"
#include "bf0_hal.h"
#include "drv_io.h"
#include "littlevgl2rtt.h"
#include "lv_demos.h"
#include "sgm41562b.h"

/**
 * @brief  Main program
 * @param  None
 * @retval 0 if success, otherwise failure number
 */
int main(void)
{
    rt_err_t ret;
    rt_uint32_t delay_ms;
    sgm41562b_handle_t charger;

    charger = sgm41562b_init(SGM41562B_I2C_BUS_NAME, SGM41562B_IRQ_PIN);
    if (charger == RT_NULL)
    {
        rt_kprintf("Charger init failed\n");
    }

    ret = littlevgl2rtt_init("lcd");
    if (ret != RT_EOK)
        return ret;

    lv_demo_benchmark();

    while (1)
    {
        delay_ms = lv_timer_handler();
        rt_thread_mdelay(delay_ms);
    }
    return RT_EOK;
}
