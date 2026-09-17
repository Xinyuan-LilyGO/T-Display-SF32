#ifndef FACTORY_UI_SERVICE_H
#define FACTORY_UI_SERVICE_H

#include <stdbool.h>
#include <stdint.h>
#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

rt_err_t ui_service_init(void);
bool ui_service_is_ready(void);
rt_err_t ui_service_wait_startup(uint32_t timeout_ms);

/* Page changes are queued and executed by the LVGL thread. */
rt_err_t ui_service_switch_page(uint16_t page_id);

rt_err_t ui_service_set_brightness(uint8_t percent);
rt_err_t ui_service_get_brightness(uint8_t *percent);
bool ui_service_is_screen_awake(void);
rt_err_t ui_service_set_screen_awake(bool awake);
rt_err_t ui_service_publish_init_errors(uint32_t error_mask);
rt_err_t ui_service_post_key(uint32_t key_code);
rt_err_t ui_service_post_key_action(uint32_t key_code, bool long_pressed);
rt_err_t ui_service_publish_input(uint8_t source, uint8_t action,
                                  uint16_t code);
rt_err_t ui_service_publish_volume(uint8_t percent);
rt_err_t ui_service_publish_battery(uint8_t percent, bool charging);
rt_err_t ui_service_publish_bluetooth(bool enabled, bool connected);
rt_err_t ui_service_publish_wifi(bool enabled, bool connected);
rt_err_t ui_service_publish_gps(bool available, bool fixed);
rt_err_t ui_service_publish_time(uint8_t hour, uint8_t minute);
rt_err_t ui_service_publish_pomodoro_complete(void);
rt_err_t ui_service_publish_low_battery(int32_t voltage_mv,
                                        uint8_t remaining_seconds,
                                        bool active);

#ifdef __cplusplus
}
#endif

#endif /* FACTORY_UI_SERVICE_H */
