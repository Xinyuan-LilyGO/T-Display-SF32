#include "rtthread.h"
#include "rtconfig.h"
#include "bf0_hal.h"
#include "drv_io.h"
#include "stdio.h"
#include "string.h"
#include "ulog.h"
#include "bq21080.h"
#include "adc.h"

static rt_device_t s_adc_dev;
static rt_adc_cmd_read_arg_t read_arg;

int32_t get_voltage(void)
{
    rt_err_t ret;

    int32_t data;
    /* find device */
    s_adc_dev = rt_device_find("bat1");
    if (s_adc_dev != RT_NULL)
    {
        // /* set channel 0*/
        read_arg.channel = 7;
        ret = rt_adc_enable((rt_adc_device_t)s_adc_dev, read_arg.channel);
        rt_uint32_t value = rt_adc_read((rt_adc_device_t)s_adc_dev, 7);
        data = (int32_t)value * 0.1;
        // LOG_I("rt_adc_read:%d,value:%.2f mV", read_arg.channel, value * 0.1); /* (0.1mV), 20700 is 2070mV or 2.070V */
        /* disable adc */
        rt_adc_disable((rt_adc_device_t)s_adc_dev, read_arg.channel);
    }
    else
    {
        log_e("adc device not found");
    }

    return data;
}

/**
 * @brief  Main program
 * @param  None
 * @retval 0 if success, otherwise failure number
 */
int main(void)
{
    rt_err_t ret = RT_EOK;
    bq21080_status_t status;
    bq21080_init();
    bq21080_set_charge_voltage(4200);
    bq21080_set_charge_current(180);

    while (1)
    {
        int32_t data = get_voltage();
        log_d("bat1 voltage:%d mV", data);

        if (bq21080_get_status(&status) == RT_EOK)
        {
            log_d("=== BQ21080 Charge Status ===");
            log_d("Charge Status: %s",
                  charge_status_str[status.charge_status]);
            log_d("VIN Power Good: %s", status.vin_power_good ? "Yes" : "No");

            log_d("=== Power Management Status ===");
            log_d("Thermal Regulation: %s",
                  status.thermal_reg_active ? "Active" : "Inactive");
            log_d("VINDPM Active: %s",
                  status.vindpm_active ? "Active" : "Inactive");
            log_d("VDPPM Active: %s",
                  status.vdppm_active ? "Active" : "Inactive");
            log_d("ILIM Active: %s",
                  status.ilim_active ? "Active" : "Inactive");

            log_d("--- Temperature Status ---");
            log_d("TS Open: %s", status.ts_open ? "YES - Check NTC!" : "No");
            log_d("TS Status: %s", ts_status_str[status.ts_status]);

            if (status.safety_timer_fault)
            {
                log_d("!!! Safety Timer Fault !!!");
            }
            if (status.bat_ocp_fault)
            {
                log_d("!!! Battery OCP Fault !!!");
            }
            if (status.buvlo_fault)
            {
                log_d("!!! Battery UVLO Fault !!!");
            }
            if (status.vin_ovp_fault)
            {
                log_d("!!! Input OVP Fault !!!");
            }

            log_d("========================");
        }
        else
        {
            log_d("Failed to read BQ21080 status");
        }

        rt_thread_mdelay(2000);
    }
}
