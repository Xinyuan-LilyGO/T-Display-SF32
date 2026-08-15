#include "rtdevice.h"
#include "rtthread.h"
#include "sgm41562b_rt_device.h"

#define DBG_TAG "sgm41562b.app"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define INPUT_VOLTAGE_LIMIT_MV 4600U
#define INPUT_CURRENT_LIMIT_MA 500U
#define CHARGE_VOLTAGE_MV 4200U
#define CHARGE_CURRENT_MA 200U
#define STATUS_PERIOD_MS 5000U

static const char *charge_status_name(rt_uint8_t status)
{
    static const char *const names[] =
    {
        "not charging",
        "pre-charge",
        "fast charge",
        "charge done",
    };

    return (status < (sizeof(names) / sizeof(names[0])))
               ? names[status]
               : "unknown";
}

static rt_err_t charger_set_u16(rt_device_t device, int command,
                                rt_uint16_t value)
{
    return rt_device_control(device, command, &value);
}

static rt_err_t charger_configure(rt_device_t device)
{
    rt_bool_t enabled;
    rt_err_t result;

    enabled = RT_FALSE;
    result = rt_device_control(device, SGM41562B_RT_CTRL_SET_HIZ, &enabled);
    if (result != RT_EOK)
        return result;

    result = charger_set_u16(device, SGM41562B_RT_CTRL_SET_INPUT_VOLTAGE,
                             INPUT_VOLTAGE_LIMIT_MV);
    if (result != RT_EOK)
        return result;
    result = charger_set_u16(device, SGM41562B_RT_CTRL_SET_INPUT_CURRENT,
                             INPUT_CURRENT_LIMIT_MA);
    if (result != RT_EOK)
        return result;
    result = charger_set_u16(device, SGM41562B_RT_CTRL_SET_CHARGE_VOLTAGE,
                             CHARGE_VOLTAGE_MV);
    if (result != RT_EOK)
        return result;
    result = charger_set_u16(device, SGM41562B_RT_CTRL_SET_CHARGE_CURRENT,
                             CHARGE_CURRENT_MA);
    if (result != RT_EOK)
        return result;

    enabled = RT_TRUE;
    return rt_device_control(device, SGM41562B_RT_CTRL_ENABLE_CHARGING,
                             &enabled);
}

static rt_err_t charger_print_status(rt_device_t device)
{
    struct sgm41562b_rt_status status;

    rt_memset(&status, 0, sizeof(status));
    if (rt_device_read(device, 0, &status, 1) != 1)
        return -RT_EIO;

    rt_kprintf("Charger: %s, power=%s, status=0x%02x, fault=0x%02x\n",
               charge_status_name(status.charge_status),
               status.power_good ? "good" : "absent",
               status.system_status, status.fault_status);
    return RT_EOK;
}

int main(void)
{
    rt_device_t charger;
    rt_uint8_t device_id = 0;
    rt_err_t result;

    rt_thread_mdelay(1000);

    charger = rt_device_find(SGM41562B_RT_DEVICE_NAME);
    if (charger == RT_NULL)
    {
        LOG_E("charger device '%s' was not found", SGM41562B_RT_DEVICE_NAME);
        return -RT_ENOSYS;
    }

    result = rt_device_open(charger, RT_DEVICE_OFLAG_RDWR);
    if (result != RT_EOK)
    {
        LOG_E("open charger device failed: %d", result);
        return result;
    }

    result = rt_device_control(charger,
                               SGM41562B_RT_CTRL_GET_DEVICE_ID,
                               &device_id);
    if (result != RT_EOK)
    {
        LOG_E("read charger device ID failed: %d", result);
        goto exit;
    }

    result = charger_configure(charger);
    if (result != RT_EOK)
    {
        LOG_E("configure charger failed: %d", result);
        goto exit;
    }

    LOG_I("charger 0x%02x configured: VIN=%u mV, IIN=%u mA, "
          "VBAT=%u mV, ICHG=%u mA",
          device_id, INPUT_VOLTAGE_LIMIT_MV, INPUT_CURRENT_LIMIT_MA,
          CHARGE_VOLTAGE_MV, CHARGE_CURRENT_MA);

    while (1)
    {
        result = charger_print_status(charger);
        if (result != RT_EOK)
            LOG_E("read charger status failed: %d", result);
        rt_thread_mdelay(STATUS_PERIOD_MS);
    }

exit:
    rt_device_close(charger);
    return result;
}
