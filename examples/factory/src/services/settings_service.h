#ifndef FACTORY_SETTINGS_SERVICE_H
#define FACTORY_SETTINGS_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <rtthread.h>

#include "communication_service.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    SETTINGS_POWER_OFF = 0,
    SETTINGS_POWER_RESTART,
} settings_power_action_t;

rt_err_t settings_service_get_brightness(uint8_t *percent);
rt_err_t settings_service_set_brightness(uint8_t percent);
rt_err_t settings_service_get_volume(uint8_t *percent);
rt_err_t settings_service_set_volume(uint8_t percent);
rt_err_t settings_service_get_menu_style(uint8_t *style);
rt_err_t settings_service_set_menu_style(uint8_t style);

rt_err_t settings_service_get_wifi_enabled(bool *enabled);
rt_err_t settings_service_set_wifi_enabled(bool enabled);
rt_err_t settings_service_refresh_wifi_status(void);
rt_err_t settings_service_scan_wifi(void);
rt_err_t settings_service_connect_wifi(const char *ssid,
                                       const char *password);
rt_err_t settings_service_get_wifi_status(
    communication_wifi_status_t *status);
rt_err_t settings_service_get_wifi_networks(
    communication_wifi_network_t *networks, size_t capacity,
    size_t *count);
rt_err_t settings_service_get_bluetooth_enabled(bool *enabled);
rt_err_t settings_service_set_bluetooth_enabled(bool enabled);
rt_err_t settings_service_get_pan_enabled(bool *enabled);
rt_err_t settings_service_set_pan_enabled(bool enabled);
rt_err_t settings_service_get_pan_info(bool *connected,
                                       char *ipv4_address,
                                       size_t address_size);

rt_err_t settings_service_get_keyboard_led(uint8_t *percent);
rt_err_t settings_service_set_keyboard_led(uint8_t percent);
rt_err_t settings_service_get_motor_enabled(bool *enabled);
rt_err_t settings_service_set_motor_enabled(bool enabled);
rt_err_t settings_service_get_motor_config(uint8_t *amplitude_percent,
                                           uint8_t *effect);
rt_err_t settings_service_preview_motor(uint8_t amplitude_percent,
                                        uint8_t effect);

rt_err_t settings_service_get_rgb(bool *enabled, uint8_t *brightness,
                                  uint8_t *color_index);
rt_err_t settings_service_set_rgb(bool enabled, uint8_t brightness,
                                  uint8_t color_index);

rt_err_t settings_service_request_power_action(
    settings_power_action_t action);

#ifdef __cplusplus
}
#endif

#endif /* FACTORY_SETTINGS_SERVICE_H */
