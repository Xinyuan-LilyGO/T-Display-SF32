#include "ui_service.h"

#include <rtdevice.h>
#include <stdlib.h>
#include <string.h>
#include <ulog.h>

#include "device_error_page.h"
#include "calendar_page.h"
#include "charge_page.h"
#include "codex_page.h"
#include "cube_page.h"
#include "esp32c6_page.h"
#include "gps_page.h"
#include "home_page.h"
#include "input_page.h"
#include "infrared_page.h"
#include "lora_page.h"
#include "music_page.h"
#include "pomodoro_page.h"
#include "recorder_page.h"
#include "settings_page.h"
#include "snake_page.h"
#include "sensors_page.h"
#include "start_page.h"
#include "storage_page.h"
#include "tetris_page.h"
#include "ui_config.h"
#include "ui_context.h"
#include "ui_event.h"
#include "ui_manager.h"
#include "ui_navigation.h"
#include "weather_page.h"

#define UI_BRIGHTNESS_MAX 100U
static rt_device_t ui_display;
static rt_device_t ui_touch;
static rt_sem_t ui_startup_sem;
static bool ui_initialized;
static volatile bool ui_startup_complete;
static bool ui_screen_awake = true;
static uint8_t ui_screen_brightness = UI_BRIGHTNESS_MAX;

static void ui_service_startup_complete(void *user_data)
{
    (void)user_data;

    ui_startup_complete = true;
    if (ui_startup_sem != RT_NULL)
        rt_sem_release(ui_startup_sem);
}

rt_err_t ui_service_init(void)
{
    rt_err_t result;

    if (ui_initialized)
        return RT_EOK;

    ui_startup_sem = rt_sem_create("ui_boot", 0, RT_IPC_FLAG_FIFO);
    if (ui_startup_sem == RT_NULL)
        return -RT_ENOMEM;
    ui_startup_complete = false;
    start_page_set_complete_callback(ui_service_startup_complete, RT_NULL);

    ui_display = rt_device_find(UI_DISPLAY_DEVICE_NAME);
    if (ui_display == RT_NULL)
    {
        LOG_E("Display device '%s' not found", UI_DISPLAY_DEVICE_NAME);
        return -RT_ENOSYS;
    }
    ui_touch = rt_device_find("touch");

    result = start_page_register();
    if (result != RT_EOK)
    {
        LOG_E("Start page registration failed: %d", result);
        ui_display = RT_NULL;
        return result;
    }

    result = home_page_register();
    if (result != RT_EOK)
    {
        LOG_E("Home page registration failed: %d", result);
        ui_display = RT_NULL;
        return result;
    }

    result = device_error_page_register();
    if (result != RT_EOK)
    {
        LOG_E("Device error page registration failed: %d", result);
        ui_display = RT_NULL;
        return result;
    }

    result = music_page_register();
    if (result != RT_EOK)
    {
        LOG_E("Music page registration failed: %d", result);
        ui_display = RT_NULL;
        return result;
    }

    result = recorder_page_register();
    if (result != RT_EOK)
    {
        LOG_E("Recorder page registration failed: %d", result);
        ui_display = RT_NULL;
        return result;
    }

    result = lora_page_register();
    if (result != RT_EOK)
    {
        LOG_E("LoRa page registration failed: %d", result);
        ui_display = RT_NULL;
        return result;
    }

    result = codex_page_register();
    if (result != RT_EOK)
    {
        LOG_E("Codex Micro page registration failed: %d", result);
        ui_display = RT_NULL;
        return result;
    }

    result = weather_page_register();
    if (result != RT_EOK)
    {
        LOG_E("Weather page registration failed: %d", result);
        ui_display = RT_NULL;
        return result;
    }

    result = charge_page_register();
    if (result != RT_EOK)
    {
        LOG_E("Charge page registration failed: %d", result);
        ui_display = RT_NULL;
        return result;
    }

    result = storage_page_register();
    if (result != RT_EOK)
    {
        LOG_E("Storage page registration failed: %d", result);
        ui_display = RT_NULL;
        return result;
    }

    result = sensors_page_register();
    if (result != RT_EOK)
    {
        LOG_E("Sensors page registration failed: %d", result);
        ui_display = RT_NULL;
        return result;
    }

    result = gps_page_register();
    if (result != RT_EOK)
    {
        LOG_E("GPS page registration failed: %d", result);
        ui_display = RT_NULL;
        return result;
    }

#if defined(FACTORY_HW_USING_KEYBOARD)
    result = esp32c6_page_register();
    if (result != RT_EOK)
    {
        LOG_E("ESP32-C6 page registration failed: %d", result);
        ui_display = RT_NULL;
        return result;
    }
#endif

    result = pomodoro_page_register();
    if (result != RT_EOK)
    {
        LOG_E("Pomodoro page registration failed: %d", result);
        ui_display = RT_NULL;
        return result;
    }

    result = calendar_page_register();
    if (result != RT_EOK)
    {
        LOG_E("Calendar page registration failed: %d", result);
        ui_display = RT_NULL;
        return result;
    }

    result = cube_page_register();
    if (result != RT_EOK)
    {
        LOG_E("3D cube page registration failed: %d", result);
        ui_display = RT_NULL;
        return result;
    }

    result = snake_page_register();
    if (result != RT_EOK)
    {
        LOG_E("Snake page registration failed: %d", result);
        ui_display = RT_NULL;
        return result;
    }

    result = tetris_page_register();
    if (result != RT_EOK)
    {
        LOG_E("Tetris page registration failed: %d", result);
        ui_display = RT_NULL;
        return result;
    }

#if defined(FACTORY_HW_USING_KEYBOARD)
    result = infrared_page_register();
    if (result != RT_EOK)
    {
        LOG_E("NEC infrared page registration failed: %d", result);
        ui_display = RT_NULL;
        return result;
    }
#endif

    result = input_page_register();
    if (result != RT_EOK)
    {
        LOG_E("Input page registration failed: %d", result);
        ui_display = RT_NULL;
        return result;
    }

    result = settings_page_register();
    if (result != RT_EOK)
    {
        LOG_E("Settings page registration failed: %d", result);
        ui_display = RT_NULL;
        return result;
    }

    result = ui_manager_init();
    if (result != RT_EOK)
    {
        LOG_E("LVGL page manager initialization failed: %d", result);
        ui_display = RT_NULL;
        return result;
    }

    ui_initialized = true;
    LOG_I("UI service initialized on display '%s'", UI_DISPLAY_DEVICE_NAME);
    return RT_EOK;
}

bool ui_service_is_ready(void)
{
    return ui_initialized && ui_manager_is_ready();
}

rt_err_t ui_service_wait_startup(uint32_t timeout_ms)
{
    rt_err_t result;

    if (!ui_service_is_ready() || ui_startup_sem == RT_NULL)
        return -RT_ERROR;

    if (!ui_startup_complete)
    {
        result = rt_sem_take(ui_startup_sem,
                             rt_tick_from_millisecond(timeout_ms));
        if (result != RT_EOK)
            return result;
    }

    return RT_EOK;
}

rt_err_t ui_service_switch_page(uint16_t page_id)
{
    if (!ui_initialized)
        return -RT_ERROR;
    if (page_id >= (uint16_t)UI_PAGE_COUNT)
        return -RT_EINVAL;

    return ui_navigation_switch_async((ui_page_id_t)page_id);
}

rt_err_t ui_service_set_brightness(uint8_t percent)
{
    rt_err_t result;

    if (!ui_initialized || ui_display == RT_NULL)
        return -RT_ERROR;
    if (percent > UI_BRIGHTNESS_MAX)
        return -RT_EINVAL;

    if (!ui_screen_awake)
    {
        ui_screen_brightness = percent;
        return ui_context_set_brightness(percent);
    }

    ui_manager_lock();
    result = rt_device_control(ui_display,
                               RTGRAPHIC_CTRL_SET_BRIGHTNESS,
                               &percent);
    ui_manager_unlock();

    if (result != RT_EOK)
        LOG_E("Set display brightness failed: %d", result);
    else
    {
        ui_screen_brightness = percent;
        ui_context_set_brightness(percent);
    }
    return result;
}

rt_err_t ui_service_get_brightness(uint8_t *percent)
{
    rt_err_t result;

    if (percent == RT_NULL)
        return -RT_EINVAL;
    if (!ui_initialized || ui_display == RT_NULL)
        return -RT_ERROR;

    if (!ui_screen_awake)
    {
        *percent = ui_screen_brightness;
        return RT_EOK;
    }

    ui_manager_lock();
    result = rt_device_control(ui_display,
                               RTGRAPHIC_CTRL_GET_BRIGHTNESS,
                               percent);
    ui_manager_unlock();

    if (result != RT_EOK)
        LOG_E("Get display brightness failed: %d", result);
    else
    {
        ui_screen_brightness = *percent;
        ui_context_set_brightness(*percent);
    }
    return result;
}

bool ui_service_is_screen_awake(void)
{
    return ui_screen_awake;
}

rt_err_t ui_service_set_screen_awake(bool awake)
{
    uint8_t brightness;
    rt_err_t result;
    rt_err_t brightness_result;
    rt_err_t touch_result;

    if (!ui_initialized || ui_display == RT_NULL)
        return -RT_ERROR;
    if (ui_screen_awake == awake)
        return RT_EOK;

    if (!awake)
    {
        result = ui_manager_set_suspended(true);
        if (result != RT_EOK)
            return result;

        ui_manager_lock();
        brightness = ui_screen_brightness;
        brightness_result = rt_device_control(
            ui_display, RTGRAPHIC_CTRL_GET_BRIGHTNESS, &brightness);
        if (brightness_result == RT_EOK &&
                brightness > 0U && brightness <= UI_BRIGHTNESS_MAX)
        {
            ui_screen_brightness = brightness;
            (void)ui_context_set_brightness(brightness);
        }
        else if (brightness_result != RT_EOK)
        {
            LOG_W("Read display brightness before screen off failed: %d",
                  brightness_result);
        }
        if (ui_touch != RT_NULL)
        {
            touch_result = rt_device_control(ui_touch,
                                             RTGRAPHIC_CTRL_POWEROFF,
                                             RT_NULL);
            if (touch_result != RT_EOK)
                LOG_W("Power off touch failed: %d", touch_result);
        }
        result = rt_device_control(ui_display, RTGRAPHIC_CTRL_POWEROFF,
                                   RT_NULL);
        ui_manager_unlock();
        if (result != RT_EOK)
        {
            LOG_E("Power off display failed: %d", result);
            ui_manager_lock();
            if (ui_touch != RT_NULL)
            {
                (void)rt_device_control(ui_touch,
                                        RTGRAPHIC_CTRL_POWERON,
                                        RT_NULL);
            }
            ui_manager_unlock();
            (void)ui_manager_set_suspended(false);
            return result;
        }
        ui_screen_awake = false;
        LOG_I("Screen off: saved brightness=%u", ui_screen_brightness);
        return RT_EOK;
    }

    ui_manager_lock();
    result = rt_device_control(ui_display, RTGRAPHIC_CTRL_POWERON, RT_NULL);
    if (result == RT_EOK)
    {
        brightness = ui_screen_brightness;
        result = rt_device_control(ui_display,
                                   RTGRAPHIC_CTRL_SET_BRIGHTNESS,
                                   &brightness);
    }
    if (ui_touch != RT_NULL)
    {
        touch_result = rt_device_control(ui_touch,
                                         RTGRAPHIC_CTRL_POWERON,
                                         RT_NULL);
        if (touch_result != RT_EOK)
            LOG_W("Power on touch failed: %d", touch_result);
    }
    ui_manager_unlock();
    if (result != RT_EOK)
    {
        LOG_E("Power on display failed: %d", result);
        return result;
    }

    ui_screen_awake = true;
    result = ui_manager_set_suspended(false);
    if (result == RT_EOK)
        LOG_I("Screen on: brightness=%u", ui_screen_brightness);
    return result;
}

rt_err_t ui_service_publish_init_errors(uint32_t error_mask)
{
    const ui_event_t event = {
        .type = UI_EVENT_INIT_STATUS_CHANGED,
        .data = (uintptr_t)error_mask,
    };
    rt_err_t result;

    if (!ui_service_is_ready())
        return -RT_ERROR;

    result = ui_context_set_init_status(error_mask, true);
    if (result != RT_EOK)
        return result;

    result = ui_event_post(&event);
    if (result != RT_EOK)
        return result;

    return ui_navigation_switch_async(error_mask != 0U
                                          ? UI_PAGE_DEVICE_ERROR
                                          : UI_PAGE_HOME);
}

rt_err_t ui_service_post_key(uint32_t key_code)
{
    return ui_service_post_key_action(key_code, false);
}

rt_err_t ui_service_post_key_action(uint32_t key_code, bool long_pressed)
{
    const ui_event_t event = {
        .type = UI_EVENT_KEY,
        .data = (uintptr_t)(key_code & UI_EVENT_KEY_CODE_MASK) |
                (long_pressed ? UI_EVENT_KEY_LONG_PRESSED : 0U),
    };

    if (!ui_service_is_ready())
        return -RT_ERROR;

    return ui_event_post(&event);
}

static rt_err_t ui_service_publish_status(ui_event_type_t type,
                                          uintptr_t data)
{
    const ui_event_t event = {
        .type = type,
        .data = data,
    };

    if (!ui_service_is_ready())
        return -RT_ERROR;
    return ui_event_post(&event);
}

rt_err_t ui_service_publish_input(uint8_t source, uint8_t action,
                                  uint16_t code)
{
    uintptr_t data = ((uintptr_t)source << UI_EVENT_INPUT_SOURCE_SHIFT) |
                     ((uintptr_t)action << UI_EVENT_INPUT_ACTION_SHIFT) |
                     ((uintptr_t)code & UI_EVENT_INPUT_CODE_MASK);

    return ui_service_publish_status(UI_EVENT_INPUT_CHANGED, data);
}

rt_err_t ui_service_publish_volume(uint8_t percent)
{
    if (percent > 100U)
        return -RT_EINVAL;
    return ui_service_publish_status(UI_EVENT_VOLUME_CHANGED,
                                     (uintptr_t)percent);
}

rt_err_t ui_service_publish_battery(uint8_t percent, bool charging)
{
    uintptr_t data;

    if (percent > 100U)
        return -RT_EINVAL;
    data = (uintptr_t)percent;
    if (charging)
        data |= UI_EVENT_BATTERY_CHARGING;
    return ui_service_publish_status(UI_EVENT_BATTERY_CHANGED, data);
}

rt_err_t ui_service_publish_bluetooth(bool enabled, bool connected)
{
    ui_status_state_t state = UI_STATUS_STATE_OFF;

    if (enabled)
        state = connected ? UI_STATUS_STATE_ACTIVE : UI_STATUS_STATE_IDLE;
    return ui_service_publish_status(UI_EVENT_BLUETOOTH_CHANGED,
                                     (uintptr_t)state);
}

rt_err_t ui_service_publish_wifi(bool enabled, bool connected)
{
    ui_status_state_t state = UI_STATUS_STATE_OFF;

    if (enabled)
        state = connected ? UI_STATUS_STATE_ACTIVE : UI_STATUS_STATE_IDLE;
    return ui_service_publish_status(UI_EVENT_WIFI_CHANGED,
                                     (uintptr_t)state);
}

rt_err_t ui_service_publish_gps(bool available, bool fixed)
{
    ui_status_state_t state = UI_STATUS_STATE_OFF;

    if (available)
        state = fixed ? UI_STATUS_STATE_ACTIVE : UI_STATUS_STATE_IDLE;
    return ui_service_publish_status(UI_EVENT_GPS_CHANGED,
                                     (uintptr_t)state);
}

rt_err_t ui_service_publish_time(uint8_t hour, uint8_t minute)
{
    uintptr_t data;

    if (hour > 23U || minute > 59U)
        return -RT_EINVAL;
    data = ((uintptr_t)hour << UI_EVENT_TIME_HOUR_SHIFT) | minute;
    return ui_service_publish_status(UI_EVENT_TIME_CHANGED, data);
}

rt_err_t ui_service_publish_pomodoro_complete(void)
{
    return ui_service_publish_status(UI_EVENT_POMODORO_COMPLETE, 0U);
}

rt_err_t ui_service_publish_low_battery(int32_t voltage_mv,
                                        uint8_t remaining_seconds,
                                        bool active)
{
    uintptr_t data;

    if (voltage_mv < 0 || voltage_mv > 0xFFFF)
        return -RT_EINVAL;

    data = (uintptr_t)((uint32_t)voltage_mv &
                       UI_EVENT_LOW_BATTERY_VOLTAGE_MASK);
    data |= (uintptr_t)remaining_seconds <<
            UI_EVENT_LOW_BATTERY_SECONDS_SHIFT;
    if (active)
        data |= UI_EVENT_LOW_BATTERY_ACTIVE;
    return ui_service_publish_status(UI_EVENT_LOW_BATTERY_CHANGED, data);
}

#ifdef RT_USING_FINSH
static int lowbat_ui(int argc, char **argv)
{
    long voltage_mv = 3500L;
    long remaining_seconds = 30L;
    char *end;
    rt_err_t result;

    if (!ui_service_is_ready())
    {
        rt_kprintf("UI service is not ready\n");
        return -RT_ERROR;
    }

    if (argc == 2 && strcmp(argv[1], "off") == 0)
    {
        result = ui_service_publish_low_battery(0, 0U, false);
        if (result == RT_EOK)
            rt_kprintf("Low battery debug popup hidden\n");
        else
            rt_kprintf("Hide low battery popup failed: %d\n", result);
        return (int)result;
    }

    if (argc > 3)
    {
        rt_kprintf("Usage: lowbat_ui [voltage_mv] [seconds]\n");
        rt_kprintf("       lowbat_ui off\n");
        return -RT_EINVAL;
    }
    if (argc >= 2)
    {
        end = RT_NULL;
        voltage_mv = strtol(argv[1], &end, 10);
        if (end == argv[1] || *end != '\0' ||
                voltage_mv < 0L || voltage_mv > 65535L)
        {
            rt_kprintf("Invalid voltage: %s\n", argv[1]);
            return -RT_EINVAL;
        }
    }
    if (argc >= 3)
    {
        end = RT_NULL;
        remaining_seconds = strtol(argv[2], &end, 10);
        if (end == argv[2] || *end != '\0' ||
                remaining_seconds < 0L || remaining_seconds > 255L)
        {
            rt_kprintf("Invalid seconds: %s\n", argv[2]);
            return -RT_EINVAL;
        }
    }

    if (!ui_service_is_screen_awake())
    {
        result = ui_service_set_screen_awake(true);
        if (result != RT_EOK)
        {
            rt_kprintf("Wake screen failed: %d\n", result);
            return (int)result;
        }
    }
    result = ui_service_publish_low_battery(
        (int32_t)voltage_mv, (uint8_t)remaining_seconds, true);
    if (result == RT_EOK)
    {
        rt_kprintf("Low battery debug popup: %ld mV, %ld seconds\n",
                   voltage_mv, remaining_seconds);
    }
    else
    {
        rt_kprintf("Show low battery popup failed: %d\n", result);
    }
    return (int)result;
}
MSH_CMD_EXPORT(lowbat_ui, show or hide the low battery debug popup);
#endif
