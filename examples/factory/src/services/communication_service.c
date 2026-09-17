#include "communication_service.h"

#include <rtdevice.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ulog.h>

#include "platform_psram.h"
#include "time_service.h"

#if defined(PKG_USING_UART_MANGER) && defined(PKG_USING_L76K) && \
    defined(L76K_USING_RT_SENSOR) && \
    defined(PKG_USING_ESP32C6_AT_COMMAND)
#define FACTORY_USING_COMMUNICATION_DEVICES
#include <sensor.h>
#include "esp32c6_at_cmd.h"
#include "esp32c6_at_uart.h"
#include "l76k.h"
#include "l76k_rt_sensor.h"
#include "uart_mux.h"
#include "xl9555.h"
#endif

#define COMM_UART_NAME              "uart2"
#define COMM_GPS_DEVICE_NAME        "gps_l76k"
#define COMM_UART_BAUDRATE          115200U
#define COMM_GPS_DETECT_TIMEOUT_MS  1500U
#define COMM_ESP_BOOT_WAIT_MS       2000U
#define COMM_ESP_PROBE_TIMEOUT_MS   2500U
#define COMM_DETECT_INTERVAL_MS     50U
#define COMM_ESP_PROBE_RESPONSE_ID  0x434F4D
#define COMM_ESP_WIFI_RESPONSE_ID   0x574946
#define COMM_ESP_WIFI_SCAN_OPT_ID   0x574947
#define COMM_ESP_WIFI_SCAN_ID       0x574948
#define COMM_ESP_WIFI_STATUS_ID     0x574949
#define COMM_ESP_WIFI_CONNECT_ID    0x57494A
#define COMM_ESP_TIME_CONFIG_ID     0x54494D
#define COMM_ESP_TIME_QUERY_ID      0x54494E
#define COMM_ESP_TIME_QUERY_DELAY_MS 2000U
#define COMM_ESP_TIME_RETRY_COUNT   5U

static uint32_t communication_init_errors;

#ifdef FACTORY_USING_COMMUNICATION_DEVICES
static rt_mutex_t communication_lock;
static rt_sem_t esp_probe_sem;
static rt_timer_t esp_time_sync_timer;
static rt_device_t gps_device;
static communication_esp_callback_t esp_user_callback;
static bool communication_initialized;
static bool uart_mux_initialized;
static bool gps_available;
static bool esp_available;
static bool esp_wifi_enabled;
static bool esp_wifi_requested_enabled;
static bool esp_probe_waiting;
static int esp_probe_result;
static communication_device_t preferred_device = COMM_DEVICE_ESP32C6;
static communication_wifi_status_t wifi_status;
static communication_wifi_network_t
    wifi_networks[COMM_WIFI_MAX_NETWORKS];
static char wifi_connect_ssid[COMM_WIFI_SSID_MAX_LEN + 1U];
static bool esp_time_sync_active;
static bool esp_time_synchronized;
static uint8_t esp_time_retry_count;

static rt_err_t communication_switch_locked(communication_device_t device);

static void communication_wifi_changed_locked(rt_err_t result)
{
    wifi_status.last_result = result;
    wifi_status.revision++;
}

static const char *communication_wifi_parse_quoted(const char *cursor,
                                                    char *output,
                                                    size_t output_size)
{
    size_t length = 0U;

    if (cursor == RT_NULL || output == RT_NULL || output_size == 0U ||
            *cursor != '"')
        return RT_NULL;
    cursor++;
    while (*cursor != '\0' && *cursor != '"')
    {
        char value = *cursor++;

        if (value == '\\' && *cursor != '\0')
            value = *cursor++;
        if (length + 1U < output_size)
            output[length++] = value;
    }
    if (*cursor != '"')
        return RT_NULL;
    output[length] = '\0';
    return cursor + 1;
}

static bool communication_wifi_parse_scan_line(
    const char *line, communication_wifi_network_t *network)
{
    const char *cursor;
    char *end;
    long value;

    if (line == RT_NULL || network == RT_NULL)
        return false;
    cursor = strstr(line, "+CWLAP:(");
    if (cursor == RT_NULL)
        return false;
    cursor += strlen("+CWLAP:(");
    value = strtol(cursor, &end, 10);
    if (end == cursor || value < 0 || value > 255)
        return false;
    network->encryption = (uint8_t)value;
    cursor = end;
    while (*cursor == ' ' || *cursor == ',')
        cursor++;
    cursor = communication_wifi_parse_quoted(
        cursor, network->ssid, sizeof(network->ssid));
    if (cursor == RT_NULL || network->ssid[0] == '\0')
        return false;
    while (*cursor == ' ' || *cursor == ',')
        cursor++;
    value = strtol(cursor, &end, 10);
    if (end == cursor || value < INT16_MIN || value > INT16_MAX)
        return false;
    network->rssi = (int16_t)value;
    return true;
}

static void communication_wifi_add_network_locked(
    const communication_wifi_network_t *network)
{
    uint32_t index;
    uint32_t weakest = 0U;

    for (index = 0U; index < wifi_status.network_count; index++)
    {
        if (strcmp(wifi_networks[index].ssid, network->ssid) == 0)
        {
            if (network->rssi > wifi_networks[index].rssi)
                wifi_networks[index] = *network;
            return;
        }
        if (wifi_networks[index].rssi < wifi_networks[weakest].rssi)
            weakest = index;
    }
    if (wifi_status.network_count < COMM_WIFI_MAX_NETWORKS)
    {
        wifi_networks[wifi_status.network_count++] = *network;
    }
    else if (network->rssi > wifi_networks[weakest].rssi)
    {
        wifi_networks[weakest] = *network;
    }
}

static void communication_wifi_parse_scan_locked(const char *response)
{
    const char *line = response;

    wifi_status.network_count = 0U;
    memset(wifi_networks, 0, sizeof(wifi_networks));
    while (line != RT_NULL && *line != '\0')
    {
        const char *newline = strchr(line, '\n');
        char text[256];
        size_t length = newline == RT_NULL ? strlen(line) :
                        (size_t)(newline - line);
        communication_wifi_network_t network;

        if (length >= sizeof(text))
            length = sizeof(text) - 1U;
        memcpy(text, line, length);
        text[length] = '\0';
        memset(&network, 0, sizeof(network));
        if (communication_wifi_parse_scan_line(text, &network))
            communication_wifi_add_network_locked(&network);
        line = newline == RT_NULL ? RT_NULL : newline + 1;
    }

    for (uint32_t left = 0U; left < wifi_status.network_count; left++)
    {
        for (uint32_t right = left + 1U;
             right < wifi_status.network_count; right++)
        {
            if (wifi_networks[right].rssi > wifi_networks[left].rssi)
            {
                communication_wifi_network_t swap = wifi_networks[left];
                wifi_networks[left] = wifi_networks[right];
                wifi_networks[right] = swap;
            }
        }
    }
}

static bool communication_wifi_parse_status_locked(const char *response)
{
    const char *line = strstr(response, "+CWJAP:");
    const char *cursor;
    char bssid[24];
    char *end;
    long rssi;

    if (line == RT_NULL)
        return false;
    cursor = strchr(line, '"');
    cursor = communication_wifi_parse_quoted(
        cursor, wifi_status.ssid, sizeof(wifi_status.ssid));
    if (cursor == RT_NULL)
        return false;
    while (*cursor == ' ' || *cursor == ',')
        cursor++;
    cursor = communication_wifi_parse_quoted(cursor, bssid,
                                             sizeof(bssid));
    if (cursor == RT_NULL)
        return false;
    while (*cursor == ' ' || *cursor == ',')
        cursor++;
    (void)strtol(cursor, &end, 10);
    if (end == cursor)
        return false;
    cursor = end;
    while (*cursor == ' ' || *cursor == ',')
        cursor++;
    rssi = strtol(cursor, &end, 10);
    if (end == cursor || rssi < INT16_MIN || rssi > INT16_MAX)
        return false;
    wifi_status.rssi = (int16_t)rssi;
    wifi_status.connected = true;
    return true;
}

static rt_err_t communication_wifi_send_locked(const char *command,
                                               int response_id)
{
    rt_err_t result;

    if (!communication_initialized || !esp_available)
        return -RT_ENOSYS;
    if (at_async_is_busy())
        return -RT_EBUSY;
    preferred_device = COMM_DEVICE_ESP32C6;
    result = communication_switch_locked(COMM_DEVICE_ESP32C6);
    if (result == RT_EOK)
    {
        result = esp32_at_send((const uint8_t *)command,
                               strlen(command), response_id);
    }
    return result;
}

static void communication_esp_time_reset_locked(void)
{
    if (esp_time_sync_timer != RT_NULL)
        rt_timer_stop(esp_time_sync_timer);
    esp_time_sync_active = false;
    esp_time_synchronized = false;
    esp_time_retry_count = 0U;
}

static void communication_esp_time_schedule_locked(void)
{
    if (esp_time_sync_timer == RT_NULL)
    {
        esp_time_sync_active = false;
        return;
    }
    rt_timer_stop(esp_time_sync_timer);
    rt_timer_start(esp_time_sync_timer);
}

static void communication_esp_time_begin_locked(void)
{
    static const char command[] =
        "AT+CIPSNTPCFG=1,8,\"cn.ntp.org.cn\",\"ntp1.aliyun.com\"\r\n";

    if (!wifi_status.connected || esp_time_sync_active ||
            esp_time_synchronized)
    {
        return;
    }
    if (esp32_at_send((const uint8_t *)command, sizeof(command) - 1U,
                      COMM_ESP_TIME_CONFIG_ID) == RT_EOK)
    {
        esp_time_sync_active = true;
        esp_time_retry_count = 0U;
    }
    else
    {
        LOG_W("Start ESP32-C6 SNTP configuration failed");
    }
}

static int communication_esp_time_month(const char *name)
{
    static const char *months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    };
    uint32_t index;

    for (index = 0U; index < sizeof(months) / sizeof(months[0]); index++)
    {
        if (strcmp(name, months[index]) == 0)
            return (int)index + 1;
    }
    return 0;
}

static bool communication_esp_time_parse(const char *response,
                                         int *year, int *month, int *day,
                                         int *hour, int *minute, int *second)
{
    const char *cursor;
    char weekday[4];
    char month_name[4];

    if (response == RT_NULL)
        return false;
    cursor = strstr(response, "+CIPSNTPTIME:");
    if (cursor == RT_NULL)
        return false;
    if (sscanf(cursor, "+CIPSNTPTIME:%3s %3s %d %d:%d:%d %d",
               weekday, month_name, day, hour, minute, second, year) != 7)
    {
        return false;
    }
    *month = communication_esp_time_month(month_name);
    return *month != 0;
}

static void communication_esp_time_retry_locked(void)
{
    esp_time_retry_count++;
    if (esp_time_retry_count >= COMM_ESP_TIME_RETRY_COUNT)
    {
        esp_time_sync_active = false;
        LOG_W("ESP32-C6 SNTP time query failed after %u attempts",
              (unsigned int)esp_time_retry_count);
        return;
    }
    communication_esp_time_schedule_locked();
}

static void communication_esp_time_timer_callback(void *parameter)
{
    rt_err_t result;

    (void)parameter;
    if (communication_lock == RT_NULL)
        return;
    if (rt_mutex_take(communication_lock, RT_WAITING_FOREVER) != RT_EOK)
        return;

    if (!esp_time_sync_active || !wifi_status.connected)
    {
        communication_esp_time_reset_locked();
        rt_mutex_release(communication_lock);
        return;
    }
    result = communication_wifi_send_locked(
        "AT+CIPSNTPTIME?\r\n", COMM_ESP_TIME_QUERY_ID);
    if (result != RT_EOK)
        communication_esp_time_retry_locked();
    rt_mutex_release(communication_lock);
}

static size_t communication_wifi_escape(char *output, size_t output_size,
                                        const char *input)
{
    size_t length = 0U;

    while (*input != '\0' && length + 1U < output_size)
    {
        if ((*input == '"' || *input == '\\') &&
                length + 2U < output_size)
            output[length++] = '\\';
        if (length + 1U >= output_size)
            break;
        output[length++] = *input++;
    }
    output[length] = '\0';
    return length;
}

static rt_err_t communication_switch_locked(communication_device_t device)
{
    uart_mux_device_t mux_device;

    if (!uart_mux_initialized)
        return -RT_ERROR;
    if (device == COMM_DEVICE_GPS)
    {
        if (!gps_available)
            return -RT_ENOSYS;
        if (esp_available && at_async_is_busy())
            return -RT_EBUSY;
        mux_device = UART_MUX_DEVICE_GPS;
    }
    else if (device == COMM_DEVICE_ESP32C6)
    {
        if (!esp_available)
            return -RT_ENOSYS;
        mux_device = UART_MUX_DEVICE_ESP32C6;
    }
    else
    {
        return -RT_EINVAL;
    }

    return uart_mux_switch_to(mux_device, COMM_UART_BAUDRATE);
}

static void communication_wifi_response_locked(int response_id,
                                                const char *response,
                                                int result)
{
    static const char scan_command[] = "AT+CWLAP\r\n";
    static const char status_command[] = "AT+CWJAP?\r\n";
    rt_err_t operation_result = result == AT_RESULT_OK ?
                                RT_EOK : -RT_ERROR;

    if (response_id == -1 && result == AT_RESULT_URC)
    {
        if (strstr(response, "WIFI DISCONNECT") != RT_NULL)
        {
            wifi_status.connected = false;
            wifi_status.connecting = false;
            wifi_status.ssid[0] = '\0';
            wifi_status.rssi = 0;
            communication_esp_time_reset_locked();
            communication_wifi_changed_locked(-RT_ERROR);
        }
        return;
    }

    switch (response_id)
    {
    case COMM_ESP_WIFI_RESPONSE_ID:
        if (result == AT_RESULT_OK)
        {
            esp_wifi_enabled = esp_wifi_requested_enabled;
            wifi_status.enabled = esp_wifi_requested_enabled;
            if (!esp_wifi_requested_enabled)
            {
                wifi_status.connected = false;
                wifi_status.scanning = false;
                wifi_status.connecting = false;
                wifi_status.ssid[0] = '\0';
                wifi_status.rssi = 0;
                wifi_status.network_count = 0U;
                communication_esp_time_reset_locked();
            }
            else
            {
                (void)esp32_at_send((const uint8_t *)status_command,
                                    sizeof(status_command) - 1U,
                                    COMM_ESP_WIFI_STATUS_ID);
            }
        }
        else
        {
            esp_wifi_enabled = !esp_wifi_requested_enabled;
            wifi_status.enabled = esp_wifi_enabled;
        }
        communication_wifi_changed_locked(operation_result);
        break;
    case COMM_ESP_WIFI_SCAN_OPT_ID:
        if (result == AT_RESULT_OK &&
                esp32_at_send((const uint8_t *)scan_command,
                              sizeof(scan_command) - 1U,
                              COMM_ESP_WIFI_SCAN_ID) == RT_EOK)
            break;
        wifi_status.scanning = false;
        communication_wifi_changed_locked(-RT_ERROR);
        break;
    case COMM_ESP_WIFI_SCAN_ID:
        if (result == AT_RESULT_OK)
            communication_wifi_parse_scan_locked(response);
        wifi_status.scanning = false;
        communication_wifi_changed_locked(operation_result);
        break;
    case COMM_ESP_WIFI_STATUS_ID:
        if (result != AT_RESULT_OK ||
                !communication_wifi_parse_status_locked(response))
        {
            wifi_status.connected = false;
            wifi_status.ssid[0] = '\0';
            wifi_status.rssi = 0;
            communication_esp_time_reset_locked();
        }
        else
            communication_esp_time_begin_locked();
        communication_wifi_changed_locked(operation_result);
        break;
    case COMM_ESP_WIFI_CONNECT_ID:
        wifi_status.connecting = false;
        if (result == AT_RESULT_OK)
        {
            wifi_status.connected = true;
            wifi_status.rssi = 0;
            rt_strncpy(wifi_status.ssid, wifi_connect_ssid,
                       sizeof(wifi_status.ssid));
            (void)esp32_at_send((const uint8_t *)status_command,
                                sizeof(status_command) - 1U,
                                COMM_ESP_WIFI_STATUS_ID);
        }
        else
        {
            wifi_status.connected = false;
            wifi_status.ssid[0] = '\0';
            wifi_status.rssi = 0;
            communication_esp_time_reset_locked();
        }
        communication_wifi_changed_locked(operation_result);
        break;
    case COMM_ESP_TIME_CONFIG_ID:
        if (result == AT_RESULT_OK)
            communication_esp_time_schedule_locked();
        else
        {
            esp_time_sync_active = false;
            LOG_W("ESP32-C6 SNTP configuration failed");
        }
        break;
    case COMM_ESP_TIME_QUERY_ID:
        if (result == AT_RESULT_OK)
        {
            int year;
            int month;
            int day;
            int hour;
            int minute;
            int second;

            if (communication_esp_time_parse(
                    response, &year, &month, &day,
                    &hour, &minute, &second) &&
                    time_service_set_local_calendar(
                        TIME_SYNC_SOURCE_ESP32C6, year, month, day,
                        hour, minute, second) == RT_EOK)
            {
                esp_time_sync_active = false;
                esp_time_synchronized = true;
                esp_time_retry_count = 0U;
                break;
            }
        }
        communication_esp_time_retry_locked();
        break;
    default:
        break;
    }
}

static void communication_esp_response(int response_id,
                                       const char *response,
                                       int result)
{
    communication_esp_callback_t callback = RT_NULL;

    if (communication_lock == RT_NULL)
        return;
    if (rt_mutex_take(communication_lock, RT_WAITING_FOREVER) != RT_EOK)
        return;

    if (esp_probe_waiting && response_id == COMM_ESP_PROBE_RESPONSE_ID)
    {
        esp_probe_result = result;
        esp_probe_waiting = false;
        rt_sem_release(esp_probe_sem);
    }

    communication_wifi_response_locked(response_id, response, result);

    if (result != AT_RESULT_URC &&
            preferred_device == COMM_DEVICE_GPS && gps_available)
    {
        rt_err_t switch_result;

        switch_result = uart_mux_switch_to(UART_MUX_DEVICE_GPS,
                                           COMM_UART_BAUDRATE);
        if (switch_result != RT_EOK)
            LOG_E("Switch UART2 to requested L76K failed: %d",
                  switch_result);
    }
    callback = esp_user_callback;
    rt_mutex_release(communication_lock);

    if (callback != RT_NULL)
        callback(response_id, response, result);
}

static rt_err_t communication_init_gps(void)
{
    rt_tick_t deadline;
    uint32_t initial_rx_bytes;
    rt_err_t result;

    gps_device = rt_device_find(COMM_GPS_DEVICE_NAME);
    if (gps_device == RT_NULL)
    {
        LOG_E("GPS sensor device '%s' not found", COMM_GPS_DEVICE_NAME);
        return -RT_ENOSYS;
    }

    result = rt_device_open(gps_device, RT_DEVICE_FLAG_RDONLY);
    if (result != RT_EOK)
    {
        LOG_E("Open GPS sensor failed: %d", result);
        gps_device = RT_NULL;
        return result;
    }

    result = rt_device_control(gps_device, RT_SENSOR_CTRL_SET_MODE,
                               (void *)(rt_ubase_t)RT_SENSOR_MODE_POLLING);
    if (result == RT_EOK)
    {
        result = rt_device_control(gps_device, RT_SENSOR_CTRL_SET_POWER,
                                   (void *)(rt_ubase_t)RT_SENSOR_POWER_NORMAL);
    }
    if (result != RT_EOK)
    {
        LOG_E("Configure L76K sensor failed: %d", result);
        rt_device_close(gps_device);
        gps_device = RT_NULL;
        return result;
    }

    initial_rx_bytes = l76k_get_rx_bytes();
    deadline = rt_tick_get() +
               rt_tick_from_millisecond(COMM_GPS_DETECT_TIMEOUT_MS);
    while ((rt_int32_t)(deadline - rt_tick_get()) > 0)
    {
        if (l76k_get_rx_bytes() != initial_rx_bytes)
        {
            LOG_I("L76K data stream detected");
            return RT_EOK;
        }
        rt_thread_mdelay(COMM_DETECT_INTERVAL_MS);
    }

    LOG_E("L76K data stream detection timed out");
    rt_device_close(gps_device);
    gps_device = RT_NULL;
    return -RT_ETIMEOUT;
}

static rt_err_t communication_init_esp(void)
{
    rt_err_t result;

    result = at_async_set_allocator(t_sf32_psram_calloc,
                                    t_sf32_psram_free);
    if (result != RT_EOK)
    {
        LOG_E("Configure ESP32-C6 PSRAM allocator failed: %d", result);
        return result;
    }
    at_async_register_callback(communication_esp_response);
    result = at_async_init(COMM_UART_NAME, COMM_UART_BAUDRATE);
    if (result != RT_EOK)
    {
        LOG_E("ESP32-C6 AT transport initialization failed: %d", result);
        return result;
    }

    /* ESP-AT can ignore commands while its firmware is still booting after
     * reset. Keep UART2 selected so startup data is drained by the AT parser. */
    rt_thread_mdelay(COMM_ESP_BOOT_WAIT_MS);

    esp_probe_result = AT_RESULT_ERROR;
    esp_probe_waiting = true;
    result = esp32_at_test(COMM_ESP_PROBE_RESPONSE_ID);
    if (result == RT_EOK)
    {
        result = rt_sem_take(
            esp_probe_sem,
            rt_tick_from_millisecond(COMM_ESP_PROBE_TIMEOUT_MS));
    }
    if (result == RT_EOK && esp_probe_result == AT_RESULT_OK)
    {
        LOG_I("ESP32-C6 AT response detected");
        return RT_EOK;
    }

    esp_probe_waiting = false;
    if (result == RT_EOK)
        result = -RT_ERROR;
    LOG_E("ESP32-C6 AT probe failed: %d", result);
    at_async_deinit();

    /* A missing ESP cannot complete the driver's pending command. Reclaim
     * UART2 for a working GPS after the bounded probe has expired. */
    if (gps_available)
        uart_mux_switch_to(UART_MUX_DEVICE_GPS, COMM_UART_BAUDRATE);
    return result;
}
#endif /* FACTORY_USING_COMMUNICATION_DEVICES */

rt_err_t communication_service_init(void)
{
#ifdef FACTORY_USING_COMMUNICATION_DEVICES
    rt_err_t result;

    if (communication_initialized)
        return (gps_available || esp_available) ? RT_EOK : -RT_ENOSYS;

    communication_init_errors = COMM_INIT_ERROR_NONE;
    memset(&wifi_status, 0, sizeof(wifi_status));
    memset(wifi_networks, 0, sizeof(wifi_networks));
    wifi_status.last_result = RT_EOK;
    result = xl9555_init();
    if (result != RT_EOK)
    {
        communication_init_errors |= COMM_INIT_ERROR_UART_MUX |
                                     COMM_INIT_ERROR_L76K |
                                     COMM_INIT_ERROR_ESP32C6;
        LOG_W("Communication hardware unavailable: XL9555 probe failed: %d",
              result);
        return result;
    }
    if (rt_device_find(COMM_UART_NAME) == RT_NULL)
    {
        LOG_E("Shared UART device '%s' not found", COMM_UART_NAME);
        communication_init_errors |= COMM_INIT_ERROR_UART2;
        return -RT_ENOSYS;
    }

    communication_lock = rt_mutex_create("comm", RT_IPC_FLAG_PRIO);
    esp_probe_sem = rt_sem_create("esp_probe", 0, RT_IPC_FLAG_FIFO);
    esp_time_sync_timer = rt_timer_create(
        "esp_time", communication_esp_time_timer_callback, RT_NULL,
        rt_tick_from_millisecond(COMM_ESP_TIME_QUERY_DELAY_MS),
        RT_TIMER_FLAG_ONE_SHOT | RT_TIMER_FLAG_SOFT_TIMER);
    if (communication_lock == RT_NULL || esp_probe_sem == RT_NULL ||
            esp_time_sync_timer == RT_NULL)
    {
        if (communication_lock != RT_NULL)
            rt_mutex_delete(communication_lock);
        if (esp_probe_sem != RT_NULL)
            rt_sem_delete(esp_probe_sem);
        if (esp_time_sync_timer != RT_NULL)
            rt_timer_delete(esp_time_sync_timer);
        communication_lock = RT_NULL;
        esp_probe_sem = RT_NULL;
        esp_time_sync_timer = RT_NULL;
        return -RT_ENOMEM;
    }

    result = uart_mux_init(COMM_UART_NAME);
    if (result != RT_EOK)
    {
        LOG_E("UART2 mux initialization failed: %d", result);
        communication_init_errors |= COMM_INIT_ERROR_UART_MUX;
        rt_timer_delete(esp_time_sync_timer);
        rt_sem_delete(esp_probe_sem);
        rt_mutex_delete(communication_lock);
        esp_time_sync_timer = RT_NULL;
        esp_probe_sem = RT_NULL;
        communication_lock = RT_NULL;
        return result;
    }
    uart_mux_initialized = true;

    result = communication_init_gps();
    if (result == RT_EOK)
        gps_available = true;
    else
        communication_init_errors |= COMM_INIT_ERROR_L76K;

    result = communication_init_esp();
    if (result == RT_EOK)
        esp_available = true;
    else
        communication_init_errors |= COMM_INIT_ERROR_ESP32C6;

    if (esp_available)
    {
        preferred_device = COMM_DEVICE_ESP32C6;
        result = uart_mux_switch_to(UART_MUX_DEVICE_ESP32C6,
                                    COMM_UART_BAUDRATE);
        if (result != RT_EOK)
        {
            LOG_E("Select ESP32-C6 as default UART2 device failed: %d",
                  result);
            communication_init_errors |= COMM_INIT_ERROR_UART_MUX;
        }
    }
    else if (gps_available)
    {
        preferred_device = COMM_DEVICE_GPS;
        result = uart_mux_switch_to(UART_MUX_DEVICE_GPS,
                                    COMM_UART_BAUDRATE);
        if (result != RT_EOK)
        {
            LOG_E("Select fallback L76K UART2 device failed: %d", result);
            communication_init_errors |= COMM_INIT_ERROR_UART_MUX;
        }
    }

    communication_initialized = true;
    if (communication_init_errors == COMM_INIT_ERROR_NONE)
    {
        LOG_I("Communication service initialized: L76K and ESP32-C6");
    }
    else
    {
        LOG_W("Communication service running in degraded mode, errors=0x%02x",
              (unsigned int)communication_init_errors);
    }
    return (gps_available || esp_available) ? RT_EOK : -RT_ENOSYS;
#else
    communication_init_errors = COMM_INIT_ERROR_UART_MUX |
                                COMM_INIT_ERROR_L76K |
                                COMM_INIT_ERROR_ESP32C6;
    LOG_E("UART mux, L76K RT-Sensor and ESP32-C6 AT drivers must be enabled");
    return -RT_ENOSYS;
#endif
}

bool communication_service_is_ready(void)
{
#ifdef FACTORY_USING_COMMUNICATION_DEVICES
    return communication_initialized && (gps_available || esp_available);
#else
    return false;
#endif
}

uint32_t communication_service_get_init_errors(void)
{
    return communication_init_errors;
}

rt_err_t communication_service_get_status(
    communication_service_status_t *status)
{
    if (status == RT_NULL)
        return -RT_EINVAL;

    memset(status, 0, sizeof(*status));
#ifdef FACTORY_USING_COMMUNICATION_DEVICES
    uart_mux_device_t active_device;

    if (!communication_initialized || communication_lock == RT_NULL)
        return -RT_ERROR;
    if (rt_mutex_take(communication_lock, RT_WAITING_FOREVER) != RT_EOK)
        return -RT_ERROR;

    status->initialized = true;
    status->gps_available = gps_available;
    status->esp_available = esp_available;
    status->esp_busy = esp_available && at_async_is_busy();
    status->wifi_enabled = esp_wifi_enabled;
    active_device = uart_mux_get_current_device();
    status->uart_active = active_device != UART_MUX_DEVICE_NONE;
    status->active_device = active_device == UART_MUX_DEVICE_ESP32C6 ?
            COMM_DEVICE_ESP32C6 : COMM_DEVICE_GPS;
    rt_mutex_release(communication_lock);
    return RT_EOK;
#else
    return -RT_ENOSYS;
#endif
}

rt_err_t communication_service_select_device(communication_device_t device)
{
#ifdef FACTORY_USING_COMMUNICATION_DEVICES
    communication_device_t previous_device;
    rt_err_t result;

    if (!communication_initialized || communication_lock == RT_NULL)
        return -RT_ERROR;
    result = rt_mutex_take(communication_lock, RT_WAITING_FOREVER);
    if (result != RT_EOK)
        return result;

    if ((device == COMM_DEVICE_GPS && !gps_available) ||
            (device == COMM_DEVICE_ESP32C6 && !esp_available))
    {
        rt_mutex_release(communication_lock);
        return -RT_ENOSYS;
    }
    if (device != COMM_DEVICE_GPS && device != COMM_DEVICE_ESP32C6)
    {
        rt_mutex_release(communication_lock);
        return -RT_EINVAL;
    }

    previous_device = preferred_device;
    preferred_device = device;
    result = communication_switch_locked(device);
    if (result != RT_EOK && result != -RT_EBUSY)
        preferred_device = previous_device;
    rt_mutex_release(communication_lock);
    return result;
#else
    (void)device;
    return -RT_ENOSYS;
#endif
}

rt_err_t communication_service_get_gps_info(communication_gps_info_t *info)
{
#ifdef FACTORY_USING_COMMUNICATION_DEVICES
    GPSInfo driver_info;
    rt_err_t result;

    if (info == RT_NULL)
        return -RT_EINVAL;
    if (!communication_initialized || communication_lock == RT_NULL)
        return -RT_ERROR;

    result = rt_mutex_take(communication_lock, RT_WAITING_FOREVER);
    if (result != RT_EOK)
        return result;
    if (!gps_available || gps_device == RT_NULL)
    {
        result = -RT_ENOSYS;
    }
    else if (uart_mux_get_current_device() != UART_MUX_DEVICE_GPS)
    {
        /* Status polling may read the cached fix, but must never take UART2
         * away from the device selected by the active page. */
        result = -RT_EBUSY;
    }
    else
    {
        /* RT-Sensor handles only its standard control commands and rejects
         * driver-specific commands before they reach the L76K adapter.  The
         * L76K cache accessor is mutex-protected and returns the same complete
         * snapshot used by the adapter. */
        result = l76k_get_gps_info(&driver_info) == 0 ? RT_EOK : -RT_ERROR;
    }
    if (result == RT_EOK)
    {
        info->latitude = driver_info.latitude;
        info->longitude = driver_info.longitude;
        info->altitude = driver_info.altitude;
        info->speed = driver_info.speed;
        info->course = driver_info.course;
        info->hdop = driver_info.hdop;
        info->satellites = driver_info.satellites;
        info->valid = driver_info.valid;
        info->time.year = driver_info.time.year;
        info->time.month = driver_info.time.month;
        info->time.day = driver_info.time.day;
        info->time.hour = driver_info.time.hour;
        info->time.minute = driver_info.time.minute;
        info->time.second = driver_info.time.second;
    }
    rt_mutex_release(communication_lock);
    return result;
#else
    (void)info;
    return -RT_ENOSYS;
#endif
}

rt_err_t communication_service_get_gps_rx_char_count(uint32_t *count)
{
    if (count == RT_NULL)
        return -RT_EINVAL;

#ifdef FACTORY_USING_COMMUNICATION_DEVICES
    rt_err_t result;

    if (!communication_initialized || communication_lock == RT_NULL)
        return -RT_ERROR;

    result = rt_mutex_take(communication_lock, RT_WAITING_FOREVER);
    if (result != RT_EOK)
        return result;
    if (!gps_available)
        result = -RT_ENOSYS;
    else
    {
        *count = l76k_get_rx_bytes();
        result = RT_EOK;
    }
    rt_mutex_release(communication_lock);
    return result;
#else
    *count = 0U;
    return -RT_ENOSYS;
#endif
}

rt_err_t communication_service_send_esp_at(const uint8_t *data,
                                            size_t length,
                                            int response_id)
{
#ifdef FACTORY_USING_COMMUNICATION_DEVICES
    rt_err_t result;

    if (data == RT_NULL || length == 0U)
        return -RT_EINVAL;
    if (!communication_initialized || communication_lock == RT_NULL)
        return -RT_ERROR;

    result = rt_mutex_take(communication_lock, RT_WAITING_FOREVER);
    if (result != RT_EOK)
        return result;
    if (!esp_available)
    {
        result = -RT_ENOSYS;
    }
    else if (at_async_is_busy())
    {
        result = -RT_EBUSY;
    }
    else
    {
        result = uart_mux_switch_to(UART_MUX_DEVICE_ESP32C6,
                                    COMM_UART_BAUDRATE);
        if (result == RT_EOK)
            result = esp32_at_send(data, length, response_id);
        if (result != RT_EOK && preferred_device == COMM_DEVICE_GPS &&
                gps_available)
        {
            uart_mux_switch_to(UART_MUX_DEVICE_GPS, COMM_UART_BAUDRATE);
        }
    }
    rt_mutex_release(communication_lock);
    return result;
#else
    (void)data;
    (void)length;
    (void)response_id;
    return -RT_ENOSYS;
#endif
}

rt_err_t communication_service_set_esp_callback(
    communication_esp_callback_t callback)
{
#ifdef FACTORY_USING_COMMUNICATION_DEVICES
    rt_err_t result;

    if (!communication_initialized || communication_lock == RT_NULL)
        return -RT_ERROR;
    result = rt_mutex_take(communication_lock, RT_WAITING_FOREVER);
    if (result != RT_EOK)
        return result;
    esp_user_callback = callback;
    rt_mutex_release(communication_lock);
    return RT_EOK;
#else
    (void)callback;
    return -RT_ENOSYS;
#endif
}

rt_err_t communication_service_set_wifi_enabled(bool enabled)
{
#ifdef FACTORY_USING_COMMUNICATION_DEVICES
    const char *command = enabled ? "AT+CWMODE=1\r\n" :
                                    "AT+CWMODE=0\r\n";
    rt_err_t result;

    if (!communication_initialized || communication_lock == RT_NULL)
        return -RT_ERROR;
    result = rt_mutex_take(communication_lock, RT_WAITING_FOREVER);
    if (result != RT_EOK)
        return result;
    result = communication_wifi_send_locked(command,
                                             COMM_ESP_WIFI_RESPONSE_ID);
    if (result == RT_EOK)
    {
        esp_wifi_requested_enabled = enabled;
        esp_wifi_enabled = enabled;
        wifi_status.enabled = enabled;
        if (!enabled)
        {
            wifi_status.connected = false;
            wifi_status.scanning = false;
            wifi_status.connecting = false;
            wifi_status.ssid[0] = '\0';
            wifi_status.rssi = 0;
            wifi_status.network_count = 0U;
        }
        communication_wifi_changed_locked(RT_EOK);
    }
    rt_mutex_release(communication_lock);
    return result;
#else
    (void)enabled;
    return -RT_ENOSYS;
#endif
}

rt_err_t communication_service_get_wifi_enabled(bool *enabled)
{
    if (enabled == RT_NULL)
        return -RT_EINVAL;
#ifdef FACTORY_USING_COMMUNICATION_DEVICES
    rt_err_t result;

    if (!communication_initialized || !esp_available)
        return -RT_ENOSYS;
    result = rt_mutex_take(communication_lock, RT_WAITING_FOREVER);
    if (result != RT_EOK)
        return result;
    *enabled = esp_wifi_enabled;
    rt_mutex_release(communication_lock);
    return RT_EOK;
#else
    return -RT_ENOSYS;
#endif
}

rt_err_t communication_service_refresh_wifi_status(void)
{
#ifdef FACTORY_USING_COMMUNICATION_DEVICES
    rt_err_t result;

    if (communication_lock == RT_NULL)
        return -RT_ERROR;
    result = rt_mutex_take(communication_lock, RT_WAITING_FOREVER);
    if (result == RT_EOK)
    {
        if (!wifi_status.enabled)
            result = -RT_ERROR;
        else
            result = communication_wifi_send_locked(
                "AT+CWJAP?\r\n", COMM_ESP_WIFI_STATUS_ID);
        rt_mutex_release(communication_lock);
    }
    return result;
#else
    return -RT_ENOSYS;
#endif
}

rt_err_t communication_service_scan_wifi(void)
{
#ifdef FACTORY_USING_COMMUNICATION_DEVICES
    rt_err_t result;

    if (communication_lock == RT_NULL)
        return -RT_ERROR;
    result = rt_mutex_take(communication_lock, RT_WAITING_FOREVER);
    if (result == RT_EOK)
    {
        if (!wifi_status.enabled)
            result = -RT_ERROR;
        else
            result = communication_wifi_send_locked(
                "AT+CWLAPOPT=1,7\r\n", COMM_ESP_WIFI_SCAN_OPT_ID);
        if (result == RT_EOK)
        {
            wifi_status.scanning = true;
            wifi_status.network_count = 0U;
            memset(wifi_networks, 0, sizeof(wifi_networks));
            communication_wifi_changed_locked(RT_EOK);
        }
        rt_mutex_release(communication_lock);
    }
    return result;
#else
    return -RT_ENOSYS;
#endif
}

rt_err_t communication_service_connect_wifi(const char *ssid,
                                             const char *password)
{
#ifdef FACTORY_USING_COMMUNICATION_DEVICES
    char escaped_ssid[(COMM_WIFI_SSID_MAX_LEN * 2U) + 1U];
    char escaped_password[129];
    char command[224];
    rt_err_t result;

    if (ssid == RT_NULL || password == RT_NULL || ssid[0] == '\0' ||
            strlen(ssid) > COMM_WIFI_SSID_MAX_LEN || strlen(password) > 64U)
        return -RT_EINVAL;
    communication_wifi_escape(escaped_ssid, sizeof(escaped_ssid), ssid);
    communication_wifi_escape(escaped_password, sizeof(escaped_password),
                              password);
    rt_snprintf(command, sizeof(command), "AT+CWJAP=\"%s\",\"%s\"\r\n",
                escaped_ssid, escaped_password);

    if (communication_lock == RT_NULL)
        return -RT_ERROR;
    result = rt_mutex_take(communication_lock, RT_WAITING_FOREVER);
    if (result == RT_EOK)
    {
        if (!wifi_status.enabled)
            result = -RT_ERROR;
        else
            result = communication_wifi_send_locked(
                command, COMM_ESP_WIFI_CONNECT_ID);
        if (result == RT_EOK)
        {
            rt_strncpy(wifi_connect_ssid, ssid,
                       sizeof(wifi_connect_ssid));
            wifi_status.connecting = true;
            communication_wifi_changed_locked(RT_EOK);
        }
        rt_mutex_release(communication_lock);
    }
    return result;
#else
    (void)ssid;
    (void)password;
    return -RT_ENOSYS;
#endif
}

rt_err_t communication_service_get_wifi_status(
    communication_wifi_status_t *status)
{
    if (status == RT_NULL)
        return -RT_EINVAL;
#ifdef FACTORY_USING_COMMUNICATION_DEVICES
    rt_err_t result;

    if (!communication_initialized || !esp_available ||
            communication_lock == RT_NULL)
        return -RT_ENOSYS;
    result = rt_mutex_take(communication_lock, RT_WAITING_FOREVER);
    if (result == RT_EOK)
    {
        *status = wifi_status;
        rt_mutex_release(communication_lock);
    }
    return result;
#else
    memset(status, 0, sizeof(*status));
    return -RT_ENOSYS;
#endif
}

rt_err_t communication_service_get_wifi_networks(
    communication_wifi_network_t *networks, size_t capacity,
    size_t *count)
{
    if (count == RT_NULL || (capacity > 0U && networks == RT_NULL))
        return -RT_EINVAL;
#ifdef FACTORY_USING_COMMUNICATION_DEVICES
    size_t copy_count;
    rt_err_t result;

    if (!communication_initialized || !esp_available ||
            communication_lock == RT_NULL)
        return -RT_ENOSYS;
    result = rt_mutex_take(communication_lock, RT_WAITING_FOREVER);
    if (result == RT_EOK)
    {
        copy_count = wifi_status.network_count;
        if (copy_count > capacity)
            copy_count = capacity;
        if (copy_count > 0U)
        {
            memcpy(networks, wifi_networks,
                   copy_count * sizeof(*networks));
        }
        *count = copy_count;
        rt_mutex_release(communication_lock);
    }
    return result;
#else
    *count = 0U;
    return -RT_ENOSYS;
#endif
}
