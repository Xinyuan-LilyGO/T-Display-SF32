#ifndef MENU_APP_BATTERY_SERVICE_H
#define MENU_APP_BATTERY_SERVICE_H

#include <stdbool.h>
#include <stdint.h>
#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    int32_t voltage_mv;
    uint8_t percent;
    bool charger_available;
    bool charger_status_valid;
    bool usb_present;
    bool charging;
    int16_t charge_voltage_mv;
    int16_t charge_current_ma;
    const char *charge_status;
    const char *power_status;
    const char *thermal_status;
    const char *vin_fault_status;
    const char *battery_fault_status;
    const char *ntc_status;
} battery_info_t;

rt_err_t battery_service_init(void);
void battery_service_deinit(void);

rt_err_t battery_service_read_voltage_mv(int32_t *voltage_mv);
uint8_t battery_service_voltage_to_percent(int32_t voltage_mv);
rt_err_t battery_service_get_percent(uint8_t *percent);
rt_err_t battery_service_get_info(battery_info_t *info);

bool battery_service_is_charger_available(void);
bool battery_service_is_charging(void);
rt_err_t battery_service_set_charge_current(int16_t current_ma);
rt_err_t battery_service_set_switch_mode(bool enable);
rt_err_t battery_service_enter_shipping_mode(void);

#ifdef __cplusplus
}
#endif

#endif /* MENU_APP_BATTERY_SERVICE_H */
