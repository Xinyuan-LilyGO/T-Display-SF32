#ifndef FACTORY_COMMUNICATION_SERVICE_H
#define FACTORY_COMMUNICATION_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

enum
{
    COMM_INIT_ERROR_NONE     = 0,
    COMM_INIT_ERROR_UART2    = (1UL << 0),
    COMM_INIT_ERROR_UART_MUX = (1UL << 1),
    COMM_INIT_ERROR_L76K     = (1UL << 2),
    COMM_INIT_ERROR_ESP32C6  = (1UL << 3),
};

typedef enum
{
    COMM_DEVICE_GPS = 0,
    COMM_DEVICE_ESP32C6,
} communication_device_t;

typedef struct
{
    bool initialized;
    bool gps_available;
    bool esp_available;
    bool esp_busy;
    bool wifi_enabled;
    bool uart_active;
    communication_device_t active_device;
} communication_service_status_t;

enum
{
    COMM_ESP_RESULT_OK = 0,
    COMM_ESP_RESULT_ERROR,
    COMM_ESP_RESULT_URC,
};

typedef struct
{
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
} communication_gps_time_t;

typedef struct
{
    double latitude;
    double longitude;
    double altitude;
    double speed;
    double course;
    double hdop;
    uint32_t satellites;
    bool valid;
    communication_gps_time_t time;
} communication_gps_info_t;

#define COMM_WIFI_MAX_NETWORKS 12U
#define COMM_WIFI_SSID_MAX_LEN 32U

typedef struct
{
    char ssid[COMM_WIFI_SSID_MAX_LEN + 1U];
    int16_t rssi;
    uint8_t encryption;
} communication_wifi_network_t;

typedef struct
{
    bool enabled;
    bool connected;
    bool scanning;
    bool connecting;
    char ssid[COMM_WIFI_SSID_MAX_LEN + 1U];
    int16_t rssi;
    uint8_t network_count;
    rt_err_t last_result;
    uint32_t revision;
} communication_wifi_status_t;

typedef void (*communication_esp_callback_t)(int response_id,
                                              const char *response,
                                              int result);

rt_err_t communication_service_init(void);
bool communication_service_is_ready(void);
uint32_t communication_service_get_init_errors(void);
rt_err_t communication_service_get_status(
    communication_service_status_t *status);

rt_err_t communication_service_select_device(communication_device_t device);
rt_err_t communication_service_get_gps_info(communication_gps_info_t *info);
rt_err_t communication_service_get_gps_rx_char_count(uint32_t *count);
rt_err_t communication_service_send_esp_at(const uint8_t *data,
                                            size_t length,
                                            int response_id);
rt_err_t communication_service_set_esp_callback(
    communication_esp_callback_t callback);
rt_err_t communication_service_set_wifi_enabled(bool enabled);
rt_err_t communication_service_get_wifi_enabled(bool *enabled);
rt_err_t communication_service_refresh_wifi_status(void);
rt_err_t communication_service_scan_wifi(void);
rt_err_t communication_service_connect_wifi(const char *ssid,
                                             const char *password);
rt_err_t communication_service_get_wifi_status(
    communication_wifi_status_t *status);
rt_err_t communication_service_get_wifi_networks(
    communication_wifi_network_t *networks, size_t capacity,
    size_t *count);

#ifdef __cplusplus
}
#endif

#endif /* FACTORY_COMMUNICATION_SERVICE_H */
