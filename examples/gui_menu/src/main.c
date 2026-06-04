#include "rtthread.h"
#include "rtconfig.h"
#include "bf0_hal.h"
#include "drv_io.h"
#include "stdio.h"
#include "string.h"
#include "ulog.h"

#include "littlevgl2rtt.h"
#include "lvgl.h"
#include "gui/gui_main.h"

rt_err_t lvgl_init(void);
void lvgl_task_entry(void *parameter);

static rt_thread_t lvgl_task = RT_NULL;
static rt_mutex_t lvgl_mutex = RT_NULL;

/**
 * @brief  Main program
 * @param  None
 * @retval 0 if success, otherwise failure number
 */
int main(void)
{
    rt_err_t ret = RT_EOK;
    if(lvgl_init() == RT_EOK)
    {
        LOG_D("LVGL init success!");
    }
    gui_main();

    while(1)
    {
        // LOG_D("Hello LILYGO T-Display-SF32!");
        rt_thread_mdelay(1000);
    }

}

rt_err_t lvgl_init(void)
{
    rt_err_t ret = RT_EOK;

    ret = littlevgl2rtt_init("lcd");
    if (ret != RT_EOK)
    {
        return ret;
    }

    lvgl_mutex = rt_mutex_create("lvgl_mutex", RT_IPC_FLAG_FIFO);
    if (lvgl_mutex == RT_NULL)
    {
        log_e("Failed to create scroll mutex");
        return -RT_ERROR;
    }

    lvgl_task = rt_thread_create("lvgl_task", lvgl_task_entry, RT_NULL, 8 * 1024, 10, 10);
    if (lvgl_task != RT_NULL)
    {
        rt_thread_startup(lvgl_task);
    }
}

void lvgl_task_entry(void *parameter)
{
    rt_uint32_t ms;
    rt_uint32_t time;
    while (1)
    {
        rt_mutex_take(lvgl_mutex, RT_WAITING_FOREVER);
        ms = lv_task_handler();
        rt_mutex_release(lvgl_mutex);
        rt_thread_mdelay(ms);
    }
}
