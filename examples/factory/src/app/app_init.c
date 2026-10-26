#include "app_init.h"

#include <time.h>
#include <ulog.h>

#include "audio_service.h"
#include "battery_service.h"
#include "bluetooth_service.h"
#include "communication_service.h"
#include "codex_micro_service.h"
#include "haptic_service.h"
#include "input_service.h"
#include "infrared_service.h"
#include "keyboard_service.h"
#include "lora_service.h"
#include "platform_power.h"
#include "pomodoro_service.h"
#include "sensor_service.h"
#include "storage_service.h"
#include "time_service.h"
#include "ui_service.h"
#include "weather_service.h"

#define UI_STARTUP_TIMEOUT_MS 5000U
#define APP_SCREEN_BUTTON_PIN 33
#define APP_VOLUME_DOWN_PIN   35
#define APP_VOLUME_UP_PIN     34
#define APP_AUDIO_VOLUME_MAX  15
#define APP_LOW_BATTERY_VOLTAGE_MV 3550
#define APP_LOW_BATTERY_TIMEOUT_MS 30000U
#define APP_LOW_BATTERY_LOG_PERIOD_SECONDS 10U

static rt_uint32_t init_errors;
static uint8_t battery_poll_ticks;
static int last_battery_percent = -1;
static int last_battery_charging = -1;
static int last_wifi_enabled = -1;
static int last_wifi_connected = -1;
static int last_gps_available = -1;
static int last_gps_fixed = -1;
static int last_time_hour = -1;
static int last_time_minute = -1;
static bool gps_time_synchronized;
static bool low_battery_shutdown_pending;
static rt_tick_t low_battery_start_tick;
static uint32_t low_battery_last_log_seconds;

static void app_cancel_low_battery_shutdown(const char *reason,
                                            int32_t voltage_mv)
{
    rt_err_t result;

    if (!low_battery_shutdown_pending)
        return;

    LOG_I("Low battery shutdown cancelled: %s, voltage=%ld mV",
          reason, (long)voltage_mv);
    low_battery_shutdown_pending = false;
    low_battery_last_log_seconds = 0U;
    if (ui_service_is_ready())
    {
        result = ui_service_publish_low_battery(voltage_mv, 0U, false);
        if (result != RT_EOK)
            LOG_W("Hide low battery popup failed: %d", result);
    }
}

static void app_check_low_battery_shutdown(void)
{
    battery_info_t info;
    rt_tick_t now;
    rt_tick_t elapsed_ticks;
    rt_tick_t timeout_ticks;
    uint32_t elapsed_seconds;
    uint32_t remaining_seconds;
    rt_err_t result;

    result = battery_service_get_info(&info);
    if (result != RT_EOK || !info.charger_available ||
            !info.charger_status_valid)
    {
        if (low_battery_shutdown_pending)
        {
            LOG_W("Low battery shutdown cancelled: battery or USB status "
                  "unavailable (%d)", result);
            low_battery_shutdown_pending = false;
            low_battery_last_log_seconds = 0U;
            if (ui_service_is_ready())
            {
                result = ui_service_publish_low_battery(
                    info.voltage_mv, 0U, false);
                if (result != RT_EOK)
                    LOG_W("Hide low battery popup failed: %d", result);
            }
        }
        return;
    }

    if (info.usb_present)
    {
        app_cancel_low_battery_shutdown("USB power connected",
                                        info.voltage_mv);
        return;
    }

    if (info.voltage_mv >= APP_LOW_BATTERY_VOLTAGE_MV)
    {
        app_cancel_low_battery_shutdown("battery voltage recovered",
                                        info.voltage_mv);
        return;
    }

    now = rt_tick_get();
    if (!low_battery_shutdown_pending)
    {
        low_battery_shutdown_pending = true;
        low_battery_start_tick = now;
        low_battery_last_log_seconds = 0U;
        LOG_W("Low battery: %ld mV, shutdown in %u seconds unless USB "
              "power is connected",
              (long)info.voltage_mv,
              APP_LOW_BATTERY_TIMEOUT_MS / 1000U);
        if (ui_service_is_ready())
        {
            if (!ui_service_is_screen_awake())
            {
                result = ui_service_set_screen_awake(true);
                if (result != RT_EOK)
                    LOG_W("Wake screen for low battery warning failed: %d",
                          result);
            }
            result = ui_service_publish_low_battery(
                info.voltage_mv,
                (uint8_t)(APP_LOW_BATTERY_TIMEOUT_MS / 1000U), true);
            if (result != RT_EOK)
                LOG_W("Show low battery popup failed: %d", result);
        }
        return;
    }

    timeout_ticks = rt_tick_from_millisecond(APP_LOW_BATTERY_TIMEOUT_MS);
    elapsed_ticks = (rt_tick_t)(now - low_battery_start_tick);
    if (elapsed_ticks >= timeout_ticks)
    {
        battery_info_t final_info;

        result = battery_service_get_info(&final_info);
        if (result != RT_EOK || !final_info.charger_available ||
                !final_info.charger_status_valid)
        {
            LOG_W("Low battery shutdown cancelled at final check: battery "
                  "or USB status unavailable (%d)", result);
            low_battery_shutdown_pending = false;
            low_battery_last_log_seconds = 0U;
            if (ui_service_is_ready())
            {
                result = ui_service_publish_low_battery(
                    final_info.voltage_mv, 0U, false);
                if (result != RT_EOK)
                    LOG_W("Hide low battery popup failed: %d", result);
            }
            return;
        }
        if (final_info.usb_present)
        {
            app_cancel_low_battery_shutdown("USB power connected",
                                            final_info.voltage_mv);
            return;
        }
        if (final_info.voltage_mv >= APP_LOW_BATTERY_VOLTAGE_MV)
        {
            app_cancel_low_battery_shutdown("battery voltage recovered",
                                            final_info.voltage_mv);
            return;
        }

        LOG_E("Low battery shutdown: voltage=%ld mV, USB power absent",
              (long)final_info.voltage_mv);
        platform_power_shutdown();
        return;
    }

    elapsed_seconds = (uint32_t)(elapsed_ticks / RT_TICK_PER_SECOND);
    remaining_seconds = (APP_LOW_BATTERY_TIMEOUT_MS / 1000U) -
                        elapsed_seconds;
    if (ui_service_is_ready())
    {
        result = ui_service_publish_low_battery(
            info.voltage_mv, (uint8_t)remaining_seconds, true);
        if (result != RT_EOK)
            LOG_W("Update low battery popup failed: %d", result);
    }
    if (elapsed_seconds >= low_battery_last_log_seconds +
                           APP_LOW_BATTERY_LOG_PERIOD_SECONDS)
    {
        low_battery_last_log_seconds = elapsed_seconds;
        LOG_W("Low battery: %ld mV, shutdown in %u seconds",
              (long)info.voltage_mv, remaining_seconds);
    }
}

static void app_adjust_volume(bool increase)
{
    int volume = audio_service_get_volume();
    uint8_t percent;
    rt_err_t result;

    if (volume < 0)
        return;
    volume += increase ? 1 : -1;
    if (volume < 0)
        volume = 0;
    else if (volume > APP_AUDIO_VOLUME_MAX)
        volume = APP_AUDIO_VOLUME_MAX;

    result = audio_service_set_volume(volume);
    if (result != RT_EOK)
    {
        LOG_W("Set global volume failed: %d", result);
        return;
    }
    percent = (uint8_t)(((uint32_t)volume * 100U +
                         APP_AUDIO_VOLUME_MAX / 2U) /
                        APP_AUDIO_VOLUME_MAX);
    result = ui_service_publish_volume(percent);
    if (result != RT_EOK)
        LOG_W("Queue volume popup failed: %d", result);
}

static void app_bluetooth_status_callback(
    bluetooth_service_event_t event,
    const bluetooth_service_status_t *status,
    void *user_data)
{
    rt_err_t result;

    (void)user_data;
    if (status == RT_NULL)
        return;
    if (event == BLUETOOTH_SERVICE_EVENT_PAN_CHANGED &&
            status->pan_connected)
    {
        result = time_service_request_ntp(
            TIME_SYNC_SOURCE_BLUETOOTH_PAN);
        if (result != RT_EOK && result != -RT_EBUSY)
            LOG_W("Start Bluetooth PAN time sync failed: %d", result);
    }
    if (event != BLUETOOTH_SERVICE_EVENT_READY &&
            event != BLUETOOTH_SERVICE_EVENT_POWER_CHANGED &&
            event != BLUETOOTH_SERVICE_EVENT_PHONE_CHANGED)
        return;

    result = ui_service_publish_bluetooth(
        status->enabled, status->phone_connected);
    if (result != RT_EOK)
        LOG_W("Queue Bluetooth status event failed: %d", result);
}

void app_update_status_bar(void)
{
    time_t now;
    struct tm *local_time;
    rt_err_t result;
    bool gps_available;
    communication_gps_info_t gps_info;
    communication_wifi_status_t wifi_status;

    app_check_low_battery_shutdown();

    if (!ui_service_is_ready())
        return;

    if (pomodoro_service_take_completion())
    {
        LOG_I("Pomodoro completed; dispatching UI and vibration alerts");
        if (!ui_service_is_screen_awake())
        {
            result = ui_service_set_screen_awake(true);
            if (result != RT_EOK)
                LOG_W("Wake screen for Pomodoro alert failed: %d", result);
        }
        result = ui_service_publish_pomodoro_complete();
        if (result != RT_EOK)
            LOG_W("Queue Pomodoro completion popup failed: %d", result);
#ifdef PKG_USING_AW86224
        result = haptic_service_play_alert(10000U);
        if (result != RT_EOK)
            LOG_W("Start Pomodoro vibration failed: %d", result);
        else
            LOG_I("Pomodoro vibration started");
#endif
    }

    if (time_service_is_valid())
    {
        now = time(RT_NULL);
        local_time = localtime(&now);
        if (local_time != RT_NULL &&
                (local_time->tm_hour != last_time_hour ||
                 local_time->tm_min != last_time_minute))
        {
            result = ui_service_publish_time((uint8_t)local_time->tm_hour,
                                             (uint8_t)local_time->tm_min);
            if (result == RT_EOK)
            {
                last_time_hour = local_time->tm_hour;
                last_time_minute = local_time->tm_min;
            }
        }
    }

    if (battery_poll_ticks == 0U)
    {
        uint8_t percent;
        bool charging;

        result = battery_service_get_percent(&percent);
        if (result == RT_EOK)
        {
            charging = battery_service_is_charging();
            if ((int)percent != last_battery_percent ||
                    (int)charging != last_battery_charging)
            {
                result = ui_service_publish_battery(percent, charging);
                if (result == RT_EOK)
                {
                    last_battery_percent = percent;
                    last_battery_charging = charging;
                }
            }
        }
    }
    battery_poll_ticks = (uint8_t)((battery_poll_ticks + 1U) % 10U);

    result = communication_service_get_wifi_status(&wifi_status);
    if (result == RT_EOK &&
            ((int)wifi_status.enabled != last_wifi_enabled ||
             (int)wifi_status.connected != last_wifi_connected))
    {
        result = ui_service_publish_wifi(wifi_status.enabled,
                                         wifi_status.connected);
        if (result == RT_EOK)
        {
            last_wifi_enabled = wifi_status.enabled;
            last_wifi_connected = wifi_status.connected;
        }
    }
    else if (result != RT_EOK && last_wifi_enabled < 0)
    {
        if (ui_service_publish_wifi(false, false) == RT_EOK)
        {
            last_wifi_enabled = false;
            last_wifi_connected = false;
        }
    }

    gps_available = communication_service_is_ready() &&
        (communication_service_get_init_errors() & COMM_INIT_ERROR_L76K) == 0U;
    result = communication_service_get_gps_info(&gps_info);
    if (result == RT_EOK)
    {
        if (!gps_time_synchronized && gps_info.valid)
        {
            result = time_service_set_utc_calendar(
                TIME_SYNC_SOURCE_GPS, gps_info.time.year,
                gps_info.time.month, gps_info.time.day,
                gps_info.time.hour, gps_info.time.minute,
                gps_info.time.second);
            if (result == RT_EOK)
                gps_time_synchronized = true;
            else if (result != -RT_EINVAL)
                LOG_W("Set time from GPS failed: %d", result);
        }
        if ((int)gps_available != last_gps_available ||
                (int)gps_info.valid != last_gps_fixed)
        {
            result = ui_service_publish_gps(gps_available, gps_info.valid);
            if (result == RT_EOK)
            {
                last_gps_available = gps_available;
                last_gps_fixed = gps_info.valid;
            }
        }
    }
    else if (result != -RT_EBUSY && last_gps_available < 0)
    {
        if (ui_service_publish_gps(gps_available, false) == RT_EOK)
        {
            last_gps_available = gps_available;
            last_gps_fixed = false;
        }
    }
}

// #if defined(FACTORY_HW_USING_KEYBOARD) || \
//     defined(FACTORY_HW_USING_JOYPAD)
static void app_lvgl_input_callback(const input_event_t *event,
                                    void *user_data)
{
    rt_err_t result;
    bool accepted = false;

    (void)user_data;
    if (event == RT_NULL)
        return;

    if (event->source == INPUT_SOURCE_BUTTON &&
            event->code == APP_SCREEN_BUTTON_PIN)
    {
        if (event->action == INPUT_ACTION_PRESSED)
        {
            result = ui_service_set_screen_awake(
                !ui_service_is_screen_awake());
            if (result != RT_EOK)
                LOG_W("Toggle screen power failed: %d", result);
        }
        return;
    }

    /* PA33 is the only input accepted while the screen is off. */
    if (!ui_service_is_screen_awake())
        return;

    result = ui_service_publish_input((uint8_t)event->source,
                                      (uint8_t)event->action,
                                      (uint16_t)event->code);
    if (result != RT_EOK)
        LOG_W("Queue raw input event for UI failed: %d", result);

    if (event->source == INPUT_SOURCE_BUTTON &&
            (event->code == APP_VOLUME_DOWN_PIN ||
             event->code == APP_VOLUME_UP_PIN) &&
            (event->action == INPUT_ACTION_CLICKED ||
             event->action == INPUT_ACTION_LONG_PRESSED))
    {
        app_adjust_volume(event->code == APP_VOLUME_UP_PIN);
        return;
    }

#if defined(FACTORY_HW_USING_KEYBOARD)
    accepted = event->source == INPUT_SOURCE_KEYBOARD &&
               (event->action == INPUT_ACTION_CLICKED ||
                event->action == INPUT_ACTION_LONG_PRESSED);
    if (accepted)
    {
        result = haptic_service_play_key_feedback();
        if (result != RT_EOK && result != -RT_ENOSYS)
            LOG_W("Play keyboard feedback failed: %d", result);
    }
#elif defined(FACTORY_HW_USING_JOYPAD)
    if (event->source == INPUT_SOURCE_JOYPAD_KEY &&
            event->action == INPUT_ACTION_PRESSED)
    {
        result = haptic_service_play_key_feedback();
        if (result != RT_EOK && result != -RT_ENOSYS)
            LOG_W("Play joypad key feedback failed: %d", result);
    }
    accepted = (event->source == INPUT_SOURCE_JOYPAD_KEY &&
                (event->code == INPUT_JOYPAD_ESC_PIN ||
                 event->code == INPUT_JOYPAD_HOME_PIN ||
                 event->code == INPUT_JOYPAD_ENTER_PIN) &&
                event->action == INPUT_ACTION_PRESSED) ||
                (event->source == INPUT_SOURCE_JOYPAD_AXIS &&
                 event->action == INPUT_ACTION_CLICKED);
#endif
    if (!accepted)
        return;

    result = ui_service_post_key_action(
        (uint32_t)event->code,
        event->action == INPUT_ACTION_LONG_PRESSED);
    if (result != RT_EOK)
        LOG_W("Queue input event for LVGL failed: %d", result);
}
// #endif

rt_uint32_t app_get_init_errors(void)
{
    return init_errors;
}

rt_err_t app_init(void)
{
    rt_err_t result;
#if defined(FACTORY_HW_USING_KEYBOARD)
    bool keyboard_extension_available;
#endif

    init_errors = 0;

    result = time_service_init();
    if (result != RT_EOK)
        LOG_E("Time service initialization failed: %d", result);

    result = pomodoro_service_init();
    if (result != RT_EOK)
        LOG_E("Pomodoro service initialization failed: %d", result);

    /* 1. Initialize the board power rails required by all devices. */
    result = platform_power_init();
    if (result != RT_EOK)
    {
        LOG_E("Platform power initialization failed: %d", result);
        return result;
    }

    /* 2. Show the start page before running slow device probes. */
    result = ui_service_init();
    if (result != RT_EOK)
    {
        LOG_E("UI service initialization failed");
        init_errors |= APP_INIT_ERROR_UI;
    }
    else
    {
        result = ui_service_wait_startup(UI_STARTUP_TIMEOUT_MS);
        if (result != RT_EOK)
            LOG_W("UI startup animation wait timed out: %d", result);
    }

    /* 3. Probe the battery ADC and charger RT-Thread devices. */
    result = battery_service_init();
    if (result != RT_EOK)
    {
        LOG_E("Battery service initialization failed");
        init_errors |= APP_INIT_ERROR_BATTERY;
    }

    /* 4. Probe the TF card and mount its file system. */
    result = storage_service_init();
    if (result != RT_EOK)
    {
        LOG_E("Storage service initialization failed");
        init_errors |= APP_INIT_ERROR_STORAGE;
    }

    /* 5. Probe and open the registered input devices. */
    result = input_service_init();
    if (result != RT_EOK)
        LOG_E("Input service initialization failed: %d", result);

#ifdef PKG_USING_AW86224
    result = haptic_service_init();
    if (result != RT_EOK)
    {
        LOG_E("Haptic service initialization failed: %d", result);
        init_errors |= APP_INIT_ERROR_AW86224;
    }
#endif

// #if defined(FACTORY_HW_USING_KEYBOARD) || \
//     defined(FACTORY_HW_USING_JOYPAD)
    if (input_service_is_ready())
    {
        result = input_service_set_event_callback(
            app_lvgl_input_callback, RT_NULL);
        if (result != RT_EOK)
            LOG_E("Register LVGL input callback failed: %d", result);
    }
// #endif

    if ((input_service_get_init_errors() & INPUT_INIT_ERROR_BUTTONS) != 0U)
        init_errors |= APP_INIT_ERROR_BUTTONS;
#if defined(FACTORY_HW_USING_KEYBOARD)
    keyboard_extension_available =
        (input_service_get_init_errors() & INPUT_INIT_ERROR_XL9555) == 0U;
    if (!keyboard_extension_available)
        init_errors |= APP_INIT_ERROR_XL9555;
    if ((input_service_get_init_errors() & INPUT_INIT_ERROR_TCA8418) != 0U)
        init_errors |= APP_INIT_ERROR_TCA8418;
#elif defined(FACTORY_HW_USING_JOYPAD)
    if ((input_service_get_init_errors() & INPUT_INIT_ERROR_XL9555) != 0U)
        init_errors |= APP_INIT_ERROR_XL9555;
    if ((input_service_get_init_errors() & INPUT_INIT_ERROR_JOYPAD_KEYS) != 0U)
        init_errors |= APP_INIT_ERROR_JOYPAD_KEYS;
    if ((input_service_get_init_errors() & INPUT_INIT_ERROR_ADS1115) != 0U)
        init_errors |= APP_INIT_ERROR_ADS1115;
    if ((input_service_get_init_errors() & INPUT_INIT_ERROR_WS2812) != 0U)
        init_errors |= APP_INIT_ERROR_WS2812;
#else
    LOG_I("No extension hardware profile selected; GPIO buttons only");
#endif
    if (result != RT_EOK && input_service_get_init_errors() == 0U)
        init_errors |= APP_INIT_ERROR_BUTTONS;

    /* 6. Initialize peripherals found only on the keyboard extension. */
#if defined(FACTORY_HW_USING_KEYBOARD)
    result = keyboard_service_init();
    if (result != RT_EOK)
        LOG_E("Keyboard peripheral initialization failed: %d", result);

    if ((keyboard_service_get_init_errors() &
            KEYBOARD_INIT_ERROR_AW21009) != 0U)
        init_errors |= APP_INIT_ERROR_AW21009;
    if ((keyboard_service_get_init_errors() &
            KEYBOARD_INIT_ERROR_INFRARED) != 0U)
        init_errors |= APP_INIT_ERROR_INFRARED;

    if ((keyboard_service_get_init_errors() &
            KEYBOARD_INIT_ERROR_INFRARED) == 0U)
    {
        result = infrared_service_init();
        if (result != RT_EOK)
            LOG_E("NEC infrared service initialization failed: %d",
                  result);
    }
#endif

    /* 7. Probe the common BHI260AP and optional keyboard BME280. */
    result = sensor_service_init();
    if (result != RT_EOK)
        LOG_E("Sensor service initialization failed: %d", result);

#if defined(FACTORY_HW_USING_KEYBOARD)
    if ((sensor_service_get_init_errors() & SENSOR_INIT_ERROR_BME280) != 0U)
    {
        init_errors |= APP_INIT_ERROR_BME280;
    }
#endif
    if ((sensor_service_get_init_errors() & SENSOR_INIT_ERROR_BHI260AP) != 0U)
        init_errors |= APP_INIT_ERROR_BHI260AP;
    if (result != RT_EOK && sensor_service_get_init_errors() == 0U)
        init_errors |= APP_INIT_ERROR_BHI260AP;

    /* 8. Initialize the SX1262 through the RadioLib service. */
    result = lora_service_init();
    if (result != RT_EOK)
    {
        LOG_E("LoRa service initialization failed: %d", result);
        init_errors |= APP_INIT_ERROR_SX1262;
    }

    /* Start continuous Joypad I2C traffic only after BHI260AP firmware and
     * the radio have completed their timing-sensitive initialization. */
    result = input_service_start_background();
    if (result != RT_EOK)
    {
        LOG_E("Input background service start failed: %d", result);
#if defined(FACTORY_HW_USING_JOYPAD)
        init_errors |= APP_INIT_ERROR_ADS1115;
#endif
    }
#if defined(FACTORY_HW_USING_JOYPAD)
    if ((input_service_get_init_errors() & INPUT_INIT_ERROR_WS2812) != 0U)
        init_errors |= APP_INIT_ERROR_WS2812;
#endif

    /* 9. Probe the audio processor, codec and high-level manager. */
    result = audio_service_init();
    if (result != RT_EOK)
        LOG_E("Audio service initialization failed: %d", result);

    if ((audio_service_get_init_errors() & AUDIO_INIT_ERROR_AUDPRC) != 0U)
        init_errors |= APP_INIT_ERROR_AUDPRC;
    if ((audio_service_get_init_errors() & AUDIO_INIT_ERROR_AUDCODEC) != 0U)
        init_errors |= APP_INIT_ERROR_AUDCODEC;
    if ((audio_service_get_init_errors() & AUDIO_INIT_ERROR_MANAGER) != 0U)
        init_errors |= APP_INIT_ERROR_AUDIO_MGR;
    if (result != RT_EOK && audio_service_get_init_errors() == 0U)
        init_errors |= APP_INIT_ERROR_AUDIO_MGR;

    /* Register the vendor HID service before enabling the shared BLE stack. */
    result = codex_micro_service_init();
    if (result != RT_EOK)
        LOG_W("Codex Micro service initialization failed: %d", result);

    /* 10. Start the shared Bluetooth stack used by Settings, PAN and Codex. */
    result = bluetooth_service_init();
    if (result != RT_EOK)
    {
        LOG_W("Bluetooth service initialization failed: %d", result);
        ui_service_publish_bluetooth(false, false);
    }
    else
    {
        bluetooth_service_status_t bluetooth_status;

        result = bluetooth_service_set_event_callback(
            app_bluetooth_status_callback, RT_NULL);
        if (result != RT_EOK)
        {
            LOG_W("Register Bluetooth UI callback failed: %d", result);
        }
        if (bluetooth_service_get_status(&bluetooth_status) == RT_EOK)
        {
            app_bluetooth_status_callback(BLUETOOTH_SERVICE_EVENT_READY,
                                          &bluetooth_status, RT_NULL);
        }

        bluetooth_service_set_enabled(true);
    }

    result = weather_service_init();
    if (result != RT_EOK)
        LOG_W("Weather service initialization failed: %d", result);

    /* 11. P0_0..P0_6 are joypad keys on the joypad board, but control the
     * GPS/ESP32-C6 hardware on the keyboard board. Never drive both roles. */
#if defined(FACTORY_HW_USING_KEYBOARD)
    if (keyboard_extension_available)
    {
        result = communication_service_init();
        if (result != RT_EOK)
        {
            LOG_E("Communication service initialization failed: %d", result);
        }

        if ((communication_service_get_init_errors() &
                COMM_INIT_ERROR_UART2) != 0U)
            init_errors |= APP_INIT_ERROR_UART2;
        if ((communication_service_get_init_errors() &
                COMM_INIT_ERROR_UART_MUX) != 0U)
            init_errors |= APP_INIT_ERROR_UART_MUX;
        if ((communication_service_get_init_errors() &
                COMM_INIT_ERROR_L76K) != 0U)
            init_errors |= APP_INIT_ERROR_L76K;
        if ((communication_service_get_init_errors() &
                COMM_INIT_ERROR_ESP32C6) != 0U)
            init_errors |= APP_INIT_ERROR_ESP32C6;
        if (result != RT_EOK &&
                communication_service_get_init_errors() == 0U)
        {
            init_errors |= APP_INIT_ERROR_UART_MUX;
        }

        if ((communication_service_get_init_errors() &
                COMM_INIT_ERROR_ESP32C6) == 0U)
        {
            uint8_t retry;

            for (retry = 0U; retry < 10U; retry++)
            {
                result = communication_service_set_wifi_enabled(true);
                if (result != -RT_EBUSY)
                    break;
                rt_thread_mdelay(20U);
            }
            if (result != RT_EOK)
                LOG_W("Enable Wi-Fi by default failed: %d", result);
        }
    }
    else
    {
        LOG_W("GPS/ESP32-C6 services skipped: XL9555 is unavailable");
    }
#elif defined(FACTORY_HW_USING_JOYPAD)
    LOG_I("Communication service skipped for joypad hardware");
#else
    LOG_I("Communication service skipped: no extension hardware profile");
#endif

    /* 12. Publish the complete result, then select Error or Home. */
    if (ui_service_is_ready())
    {
        result = ui_service_publish_init_errors(init_errors);
        if (result != RT_EOK)
            LOG_E("Publish initialization status to UI failed: %d", result);
    }

    if (init_errors != APP_INIT_ERROR_NONE)
    {
        LOG_W("Initialization completed with device errors: 0x%08x",
              init_errors);
    }

    app_update_status_bar();

    return (init_errors & APP_INIT_ERROR_UI) != 0U ? -RT_ERROR : RT_EOK;
}
