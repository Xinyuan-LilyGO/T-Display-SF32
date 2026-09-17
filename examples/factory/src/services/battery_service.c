#include "battery_service.h"

#include <rtdevice.h>
#include <string.h>
#include <ulog.h>

#if defined(PKG_USING_SGM41562B) && defined(SGM41562B_USING_RT_DEVICE)
#include "sgm41562b_rt_device.h"
#define BATTERY_USING_SGM41562B_DEVICE
#endif

#define BATTERY_ADC_DEVICE_NAME "bat1"
#define BATTERY_ADC_CHANNEL     7

#define BATTERY_STATUS_UNAVAILABLE "Unavailable"

#define CHARGER_DEFAULT_VOLTAGE_MV 4200U
#define CHARGER_DEFAULT_CURRENT_MA 344U

#define CHARGER_SYSTEM_THERMAL_REGULATION (1U << 0)
#define CHARGER_FAULT_VIN                  (1U << 5)
#define CHARGER_FAULT_BATTERY              (1U << 3)
#define CHARGER_FAULT_NTC_HOT              (1U << 1)
#define CHARGER_FAULT_NTC_COLD             (1U << 0)

#define CHARGER_NOT_CHARGING 0U
#define CHARGER_PRE_CHARGE   1U
#define CHARGER_FAST_CHARGE  2U
#define CHARGER_CHARGE_DONE  3U

static rt_device_t battery_adc;
static rt_mutex_t battery_lock;
static bool battery_initialized;
static rt_err_t battery_init_result = -RT_ERROR;

#ifdef BATTERY_USING_SGM41562B_DEVICE
static rt_device_t battery_charger;
static rt_uint16_t charger_voltage_mv = CHARGER_DEFAULT_VOLTAGE_MV;
static rt_uint16_t charger_current_ma = CHARGER_DEFAULT_CURRENT_MA;

static const char *charger_charge_status_name(rt_uint8_t status)
{
    switch (status)
    {
    case CHARGER_NOT_CHARGING:
        return "Not Charging";
    case CHARGER_PRE_CHARGE:
        return "Pre-charge";
    case CHARGER_FAST_CHARGE:
        return "Fast Charge";
    case CHARGER_CHARGE_DONE:
        return "Charge Done";
    default:
        return "Unknown";
    }
}

static const char *charger_ntc_status_name(rt_uint8_t fault_status)
{
    if ((fault_status & CHARGER_FAULT_NTC_HOT) != 0U)
        return "NTC Hot";
    if ((fault_status & CHARGER_FAULT_NTC_COLD) != 0U)
        return "NTC Cold";
    return "Normal";
}

static rt_err_t charger_read_status(struct sgm41562b_rt_status *status)
{
    rt_err_t result;

    if (status == RT_NULL)
        return -RT_EINVAL;
    if (battery_charger == RT_NULL || battery_lock == RT_NULL)
        return -RT_ERROR;

    result = rt_mutex_take(battery_lock, RT_WAITING_FOREVER);
    if (result != RT_EOK)
        return result;

    rt_memset(status, 0, sizeof(*status));
    if (rt_device_read(battery_charger, 0, status, 1) != 1)
        result = -RT_EIO;

    rt_mutex_release(battery_lock);
    return result;
}

static rt_err_t charger_control(int command, void *args)
{
    rt_err_t result;

    if (battery_charger == RT_NULL || battery_lock == RT_NULL)
        return -RT_ERROR;

    result = rt_mutex_take(battery_lock, RT_WAITING_FOREVER);
    if (result != RT_EOK)
        return result;

    result = rt_device_control(battery_charger, command, args);
    rt_mutex_release(battery_lock);
    return result;
}
#endif

rt_err_t battery_service_init(void)
{
    rt_err_t result = RT_EOK;

    if (battery_initialized)
        return battery_init_result;

    battery_lock = rt_mutex_create("battery", RT_IPC_FLAG_PRIO);
    if (battery_lock == RT_NULL)
    {
        LOG_E("Failed to create battery mutex");
        return -RT_ENOMEM;
    }

    battery_adc = rt_device_find(BATTERY_ADC_DEVICE_NAME);
    if (battery_adc == RT_NULL)
    {
        LOG_E("ADC device '%s' not found", BATTERY_ADC_DEVICE_NAME);
        result = -RT_ERROR;
    }

#ifdef BATTERY_USING_SGM41562B_DEVICE
    battery_charger = rt_device_find(SGM41562B_RT_DEVICE_NAME);
    if (battery_charger == RT_NULL)
    {
        LOG_E("Charger device '%s' not found", SGM41562B_RT_DEVICE_NAME);
        result = -RT_ENOSYS;
    }
    else
    {
        rt_err_t open_result =
            rt_device_open(battery_charger, RT_DEVICE_OFLAG_RDWR);

        if (open_result != RT_EOK)
        {
            LOG_E("Open charger device failed: %d", open_result);
            battery_charger = RT_NULL;
            result = open_result;
        }
    }
#endif

    battery_initialized = true;
    battery_init_result = result;
    return battery_init_result;
}

void battery_service_deinit(void)
{
    if (!battery_initialized)
        return;

#ifdef BATTERY_USING_SGM41562B_DEVICE
    if (battery_charger != RT_NULL)
    {
        rt_device_close(battery_charger);
        battery_charger = RT_NULL;
    }
#endif

    if (battery_lock != RT_NULL)
    {
        rt_mutex_delete(battery_lock);
        battery_lock = RT_NULL;
    }

    battery_adc = RT_NULL;
    battery_initialized = false;
    battery_init_result = -RT_ERROR;
}

rt_err_t battery_service_read_voltage_mv(int32_t *voltage_mv)
{
    rt_err_t result;
    uint32_t raw;

    if (voltage_mv == RT_NULL)
        return -RT_EINVAL;
    if (!battery_initialized || battery_adc == RT_NULL ||
            battery_lock == RT_NULL)
        return -RT_ERROR;

    result = rt_mutex_take(battery_lock, RT_WAITING_FOREVER);
    if (result != RT_EOK)
        return result;

    result = rt_adc_enable((rt_adc_device_t)battery_adc,
                           BATTERY_ADC_CHANNEL);
    if (result == RT_EOK)
    {
        raw = rt_adc_read((rt_adc_device_t)battery_adc,
                          BATTERY_ADC_CHANNEL);
        *voltage_mv = (int32_t)(raw / 10U);
        rt_adc_disable((rt_adc_device_t)battery_adc,
                       BATTERY_ADC_CHANNEL);
    }

    rt_mutex_release(battery_lock);
    return result;
}

uint8_t battery_service_voltage_to_percent(int32_t voltage_mv)
{
    int32_t percent;

    if (voltage_mv >= 4200)
        return 100;
    if (voltage_mv < 3500)
        return 0;

    if (voltage_mv >= 4000)
        percent = 80 + (voltage_mv - 4000) * 20 / 200;
    else if (voltage_mv >= 3900)
        percent = 58 + (voltage_mv - 3900) * 22 / 100;
    else if (voltage_mv >= 3700)
        percent = 18 + (voltage_mv - 3700) * 40 / 200;
    else if (voltage_mv >= 3600)
        percent = 6 + (voltage_mv - 3600) * 12 / 100;
    else
        percent = (voltage_mv - 3500) * 6 / 100;

    return (uint8_t)(percent > 100 ? 100 : percent);
}

rt_err_t battery_service_get_percent(uint8_t *percent)
{
    int32_t voltage_mv;
    rt_err_t result;

    if (percent == RT_NULL)
        return -RT_EINVAL;

    result = battery_service_read_voltage_mv(&voltage_mv);
    if (result != RT_EOK)
        return result;

    *percent = battery_service_voltage_to_percent(voltage_mv);
    return RT_EOK;
}

rt_err_t battery_service_get_info(battery_info_t *info)
{
    rt_err_t result;

    if (info == RT_NULL)
        return -RT_EINVAL;

    memset(info, 0, sizeof(*info));
    info->charge_status = BATTERY_STATUS_UNAVAILABLE;
    info->power_status = BATTERY_STATUS_UNAVAILABLE;
    info->thermal_status = BATTERY_STATUS_UNAVAILABLE;
    info->vin_fault_status = BATTERY_STATUS_UNAVAILABLE;
    info->battery_fault_status = BATTERY_STATUS_UNAVAILABLE;
    info->ntc_status = BATTERY_STATUS_UNAVAILABLE;

    result = battery_service_read_voltage_mv(&info->voltage_mv);
    if (result == RT_EOK)
        info->percent = battery_service_voltage_to_percent(info->voltage_mv);

#ifdef BATTERY_USING_SGM41562B_DEVICE
    if (battery_charger != RT_NULL)
    {
        struct sgm41562b_rt_status status;

        info->charger_available = true;
        if (charger_read_status(&status) == RT_EOK)
        {
            info->charger_status_valid = true;
            info->usb_present = status.power_good != 0U;
            info->charging = status.charge_status == CHARGER_PRE_CHARGE ||
                             status.charge_status == CHARGER_FAST_CHARGE;
            info->charge_voltage_mv = (int16_t)charger_voltage_mv;
            info->charge_current_ma = (int16_t)charger_current_ma;
            info->charge_status =
                charger_charge_status_name(status.charge_status);
            info->power_status =
                status.power_good ? "Power Good" : "Power Fail";
            info->thermal_status =
                (status.system_status &
                 CHARGER_SYSTEM_THERMAL_REGULATION) != 0U
                    ? "In Thermal Regulation"
                    : "Normal";
            info->vin_fault_status =
                (status.fault_status & CHARGER_FAULT_VIN) != 0U
                    ? "Input Fault (OVP/Bad)"
                    : "Normal";
            info->battery_fault_status =
                (status.fault_status & CHARGER_FAULT_BATTERY) != 0U
                    ? "Battery OVP"
                    : "Normal";
            info->ntc_status = charger_ntc_status_name(status.fault_status);
        }
    }
#endif

    return result;
}

bool battery_service_is_charger_available(void)
{
#ifdef BATTERY_USING_SGM41562B_DEVICE
    return battery_charger != RT_NULL;
#else
    return false;
#endif
}

bool battery_service_is_charging(void)
{
#ifdef BATTERY_USING_SGM41562B_DEVICE
    struct sgm41562b_rt_status status;

    if (charger_read_status(&status) != RT_EOK)
        return false;

    return status.charge_status == CHARGER_PRE_CHARGE ||
           status.charge_status == CHARGER_FAST_CHARGE;
#else
    return false;
#endif
}

rt_err_t battery_service_set_charge_current(int16_t current_ma)
{
#ifdef BATTERY_USING_SGM41562B_DEVICE
    rt_err_t result;
    rt_uint16_t value;

    if (current_ma < 0)
        return -RT_EINVAL;

    value = (rt_uint16_t)current_ma;
    result = charger_control(SGM41562B_RT_CTRL_SET_CHARGE_CURRENT, &value);
    if (result == RT_EOK)
        charger_current_ma = value;
    return result;
#else
    (void)current_ma;
    return -RT_ENOSYS;
#endif
}

rt_err_t battery_service_set_switch_mode(bool enable)
{
#ifdef BATTERY_USING_SGM41562B_DEVICE
    (void)enable;
    return -RT_ENOSYS;
#else
    (void)enable;
    return -RT_ENOSYS;
#endif
}

rt_err_t battery_service_enter_shipping_mode(void)
{
#ifdef BATTERY_USING_SGM41562B_DEVICE
    return charger_control(SGM41562B_RT_CTRL_ENTER_SHIPPING, RT_NULL);
#else
    return -RT_ENOSYS;
#endif
}
