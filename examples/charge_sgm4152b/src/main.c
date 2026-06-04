#include "rtthread.h"
#include "rtconfig.h"
#include "rtdevice.h"
#include "bf0_hal.h"
#include "drv_io.h"
#include "stdio.h"
#include "string.h"
#include "ulog.h"
#include "sgm41562b.h"

static void charger_status_cb(rt_uint8_t status_reg, rt_uint8_t fault_reg, void *user_data)
{
    rt_kprintf("Charger status: 0x%02X, fault: 0x%02X\n", status_reg, fault_reg);

    /* Handle events */
    if (fault_reg & SGM41562B_VIN_FAULT)
        rt_kprintf("Input fault detected!\n");
    if (fault_reg & SGM41562B_BAT_FAULT)
        rt_kprintf("Battery OVP fault!\n");
}

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
    sgm41562b_handle_t charger;
    charger = sgm41562b_init(SGM41562B_I2C_BUS_NAME, SGM41562B_IRQ_PIN);
    if (charger == RT_NULL)
    {
        rt_kprintf("Charger init failed\n");
    }
    // /* Attach status callback */
    // sgm41562b_attach_status_callback(charger, charger_status_cb, RT_NULL);
    
    /* Configure charging parameters */
    sgm41562b_set_charge_current(charger, 200);   /* 400mA fast charge */
    sgm41562b_set_charge_voltage(charger, 4200);  /* 4.2V */
    sgm41562b_set_input_current_limit(charger, 500);
    
    // /* Enable charging */
    sgm41562b_enable_charging(charger, RT_TRUE);
    
    /* Enable watchdog with 40s timeout and feed regularly */
    sgm41562b_set_watchdog_timer(charger, WATCHDOG_40S);
    
    /* Main loop - feed watchdog periodically */
    while (1)
    {
        rt_thread_mdelay(5000);  /* 10 seconds */
        sgm41562b_watchdog_reset(charger);
        
        int32_t data = get_voltage();
        log_d("bat1 voltage:%d mV", data);

        rt_kprintf("=== Charger Status ===\n");
        rt_kprintf("  Charge Status   : %s\n", sgm41562b_get_charge_status_str(charger));
        rt_kprintf("  Input Power     : %s\n", sgm41562b_get_power_good_str(charger));
        rt_kprintf("  Thermal         : %s\n", sgm41562b_get_thermal_regulation_str(charger));
        rt_kprintf("  Watchdog        : %s\n", sgm41562b_get_watchdog_fault_str(charger));
        rt_kprintf("  VIN Fault       : %s\n", sgm41562b_get_vin_fault_str(charger));
        rt_kprintf("  Thermal Shutdown: %s\n", sgm41562b_get_thermal_shutdown_str(charger));
        rt_kprintf("  Battery Fault   : %s\n", sgm41562b_get_battery_fault_str(charger));
        rt_kprintf("  Safety Timer    : %s\n", sgm41562b_get_safety_timer_fault_str(charger));
        rt_kprintf("  NTC Status      : %s\n", sgm41562b_get_ntc_fault_str(charger));
    }
}
