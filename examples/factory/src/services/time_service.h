#ifndef FACTORY_TIME_SERVICE_H
#define FACTORY_TIME_SERVICE_H

#include <stdbool.h>
#include <stdint.h>
#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    TIME_SYNC_SOURCE_NONE = 0,
    TIME_SYNC_SOURCE_BLUETOOTH_PAN,
    TIME_SYNC_SOURCE_ESP32C6,
    TIME_SYNC_SOURCE_GPS,
} time_sync_source_t;

rt_err_t time_service_init(void);
bool time_service_is_valid(void);

rt_err_t time_service_set_local_calendar(time_sync_source_t source,
                                         int year, int month, int day,
                                         int hour, int minute, int second);
rt_err_t time_service_set_utc_calendar(time_sync_source_t source,
                                       int year, int month, int day,
                                       int hour, int minute, int second);
rt_err_t time_service_request_ntp(time_sync_source_t source);

#ifdef __cplusplus
}
#endif

#endif /* FACTORY_TIME_SERVICE_H */
