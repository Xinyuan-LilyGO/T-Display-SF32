#include "settings_service.h"

#include <ulog.h>

#ifdef RT_USING_LWIP
#include <lwip/ip4_addr.h>
#include <netif/ethernetif.h>
#endif

#include "audio_service.h"
#include "bluetooth_service.h"
#include "communication_service.h"
#include "home_page.h"
#include "input_service.h"
#include "keyboard_service.h"
#include "platform_power.h"
#include "ui_service.h"

#define SETTINGS_AUDIO_VOLUME_MAX 15U
#define SETTINGS_POWER_THREAD_STACK_SIZE 1024U
#define SETTINGS_POWER_THREAD_PRIORITY (RT_THREAD_PRIORITY_HIGH + 4U)
#define SETTINGS_POWER_DELAY_MS 250U

typedef struct
{
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} settings_rgb_color_t;

static const settings_rgb_color_t settings_rgb_colors[] = {
    {0x39U, 0xFFU, 0x88U},
    {0x43U, 0xB6U, 0xFFU},
    {0xF0U, 0xCFU, 0x61U},
    {0xFFU, 0x3BU, 0x5CU},
    {0xFFU, 0xFFU, 0xFFU},
};

static bool settings_power_action_pending;

rt_err_t settings_service_get_brightness(uint8_t *percent)
{
    return ui_service_get_brightness(percent);
}

rt_err_t settings_service_set_brightness(uint8_t percent)
{
    return ui_service_set_brightness(percent);
}

rt_err_t settings_service_get_volume(uint8_t *percent)
{
    int volume;

    if (percent == RT_NULL)
        return -RT_EINVAL;
    volume = audio_service_get_volume();
    if (volume < 0)
        return (rt_err_t)volume;
    *percent = (uint8_t)(((uint32_t)volume * 100U +
                          SETTINGS_AUDIO_VOLUME_MAX / 2U) /
                         SETTINGS_AUDIO_VOLUME_MAX);
    return RT_EOK;
}

rt_err_t settings_service_set_volume(uint8_t percent)
{
    int volume;

    if (percent > 100U)
        return -RT_EINVAL;
    volume = (int)(((uint32_t)percent * SETTINGS_AUDIO_VOLUME_MAX + 50U) /
                   100U);
    return audio_service_set_volume(volume);
}

rt_err_t settings_service_get_menu_style(uint8_t *style)
{
    if (style == RT_NULL)
        return -RT_EINVAL;
    *style = (uint8_t)home_page_get_menu_style();
    return RT_EOK;
}

rt_err_t settings_service_set_menu_style(uint8_t style)
{
    if (style >= (uint8_t)HOME_MENU_STYLE_COUNT)
        return -RT_EINVAL;
    return home_page_set_menu_style((home_menu_style_t)style);
}

rt_err_t settings_service_get_wifi_enabled(bool *enabled)
{
    return communication_service_get_wifi_enabled(enabled);
}

rt_err_t settings_service_set_wifi_enabled(bool enabled)
{
    return communication_service_set_wifi_enabled(enabled);
}

rt_err_t settings_service_refresh_wifi_status(void)
{
    return communication_service_refresh_wifi_status();
}

rt_err_t settings_service_scan_wifi(void)
{
    return communication_service_scan_wifi();
}

rt_err_t settings_service_connect_wifi(const char *ssid,
                                       const char *password)
{
    return communication_service_connect_wifi(ssid, password);
}

rt_err_t settings_service_get_wifi_status(
    communication_wifi_status_t *status)
{
    return communication_service_get_wifi_status(status);
}

rt_err_t settings_service_get_wifi_networks(
    communication_wifi_network_t *networks, size_t capacity,
    size_t *count)
{
    return communication_service_get_wifi_networks(networks, capacity,
                                                   count);
}

rt_err_t settings_service_get_bluetooth_enabled(bool *enabled)
{
    if (enabled == RT_NULL)
        return -RT_EINVAL;
    if (!bluetooth_service_is_ready())
        return -RT_ENOSYS;
    *enabled = bluetooth_service_is_enabled();
    return RT_EOK;
}

rt_err_t settings_service_set_bluetooth_enabled(bool enabled)
{
    return bluetooth_service_set_enabled(enabled);
}

rt_err_t settings_service_get_pan_enabled(bool *enabled)
{
    if (enabled == RT_NULL)
        return -RT_EINVAL;
    if (!bluetooth_service_is_ready())
        return -RT_ENOSYS;
    *enabled = bluetooth_service_is_pan_enabled();
    return RT_EOK;
}

rt_err_t settings_service_set_pan_enabled(bool enabled)
{
    return bluetooth_service_set_pan_enabled(enabled);
}

rt_err_t settings_service_get_pan_info(bool *connected,
                                       char *ipv4_address,
                                       size_t address_size)
{
    bluetooth_service_status_t status;
    rt_err_t result;

    if (connected == RT_NULL || ipv4_address == RT_NULL ||
            address_size == 0U)
        return -RT_EINVAL;

    *connected = false;
    rt_snprintf(ipv4_address, address_size, "--");
    result = bluetooth_service_get_status(&status);
    if (result != RT_EOK)
        return result;
    *connected = status.pan_connected;
    if (!status.pan_connected)
        return RT_EOK;

#ifdef RT_USING_LWIP
    {
        rt_device_t device = rt_device_find("b0");
        struct eth_device *ethernet = (struct eth_device *)device;

        if (ethernet != RT_NULL && ethernet->netif != RT_NULL &&
                !ip4_addr_isany_val(*netif_ip4_addr(ethernet->netif)))
        {
            if (ip4addr_ntoa_r(netif_ip4_addr(ethernet->netif),
                               ipv4_address, (int)address_size) == RT_NULL)
            {
                rt_snprintf(ipv4_address, address_size, "--");
            }
        }
    }
#endif
    return RT_EOK;
}

rt_err_t settings_service_get_keyboard_led(uint8_t *percent)
{
    return keyboard_service_get_led_brightness(percent);
}

rt_err_t settings_service_set_keyboard_led(uint8_t percent)
{
    return keyboard_service_set_led_brightness(percent);
}

rt_err_t settings_service_get_motor_enabled(bool *enabled)
{
    return keyboard_service_get_motor_enabled(enabled);
}

rt_err_t settings_service_set_motor_enabled(bool enabled)
{
    return keyboard_service_set_motor_enabled(enabled);
}

rt_err_t settings_service_preview_motor(uint8_t amplitude_percent,
                                        uint8_t effect)
{
    if (effect >= (uint8_t)KEYBOARD_MOTOR_EFFECT_COUNT)
        return -RT_EINVAL;
    return keyboard_service_preview_motor(
        amplitude_percent, (keyboard_motor_effect_t)effect);
}

static uint8_t settings_service_find_rgb_color(uint8_t red, uint8_t green,
                                               uint8_t blue)
{
    uint32_t index;
    uint32_t nearest = 0U;
    uint32_t nearest_distance = UINT32_MAX;

    for (index = 0U;
         index < sizeof(settings_rgb_colors) / sizeof(settings_rgb_colors[0]);
         index++)
    {
        int32_t red_delta = (int32_t)red - settings_rgb_colors[index].red;
        int32_t green_delta =
            (int32_t)green - settings_rgb_colors[index].green;
        int32_t blue_delta = (int32_t)blue - settings_rgb_colors[index].blue;
        uint32_t distance = (uint32_t)(red_delta * red_delta +
                                       green_delta * green_delta +
                                       blue_delta * blue_delta);

        if (distance < nearest_distance)
        {
            nearest_distance = distance;
            nearest = index;
        }
    }
    return (uint8_t)nearest;
}

rt_err_t settings_service_get_rgb(bool *enabled, uint8_t *brightness,
                                  uint8_t *color_index)
{
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    rt_err_t result;

    if (enabled == RT_NULL || brightness == RT_NULL ||
        color_index == RT_NULL)
        return -RT_EINVAL;
    result = input_service_get_joypad_rgb(enabled, brightness,
                                          &red, &green, &blue);
    if (result == RT_EOK)
        *color_index = settings_service_find_rgb_color(red, green, blue);
    return result;
}

rt_err_t settings_service_set_rgb(bool enabled, uint8_t brightness,
                                  uint8_t color_index)
{
    const settings_rgb_color_t *color;

    if (color_index >=
        sizeof(settings_rgb_colors) / sizeof(settings_rgb_colors[0]))
        return -RT_EINVAL;
    color = &settings_rgb_colors[color_index];
    return input_service_set_joypad_rgb(enabled, brightness,
                                        color->red, color->green,
                                        color->blue);
}

static void settings_service_power_thread(void *parameter)
{
    settings_power_action_t action =
        (settings_power_action_t)(uintptr_t)parameter;

    rt_thread_mdelay(SETTINGS_POWER_DELAY_MS);
    switch (action)
    {
    case SETTINGS_POWER_OFF:
        platform_power_shutdown();
        break;
    case SETTINGS_POWER_RESTART:
        platform_power_restart();
        break;
    default:
        break;
    }
    settings_power_action_pending = false;
}

rt_err_t settings_service_request_power_action(
    settings_power_action_t action)
{
    rt_thread_t thread;
    rt_err_t result;

    if (action > SETTINGS_POWER_RESTART)
        return -RT_EINVAL;
    if (settings_power_action_pending)
        return -RT_EBUSY;

    thread = rt_thread_create(
        "set_power", settings_service_power_thread,
        (void *)(uintptr_t)action, SETTINGS_POWER_THREAD_STACK_SIZE,
        SETTINGS_POWER_THREAD_PRIORITY, 10U);
    if (thread == RT_NULL)
        return -RT_ENOMEM;

    settings_power_action_pending = true;
    result = rt_thread_startup(thread);
    if (result != RT_EOK)
    {
        settings_power_action_pending = false;
        rt_thread_delete(thread);
    }
    return result;
}
