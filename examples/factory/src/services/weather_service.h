#ifndef FACTORY_WEATHER_SERVICE_H
#define FACTORY_WEATHER_SERVICE_H

#include <stdbool.h>
#include <stdint.h>
#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WEATHER_SERVICE_FORECAST_DAYS 5U
#define WEATHER_SERVICE_CITY_MAX      40U
#define WEATHER_SERVICE_DATE_MAX      11U
#define WEATHER_SERVICE_TEXT_MAX      24U

typedef enum
{
    WEATHER_SERVICE_IDLE = 0,
    WEATHER_SERVICE_LOADING,
    WEATHER_SERVICE_READY,
    WEATHER_SERVICE_NO_PAN,
    WEATHER_SERVICE_NETWORK_ERROR,
    WEATHER_SERVICE_DATA_ERROR,
} weather_service_state_t;

typedef struct
{
    char date[WEATHER_SERVICE_DATE_MAX];
    char condition[WEATHER_SERVICE_TEXT_MAX];
    int16_t high_c;
    int16_t low_c;
} weather_service_day_t;

typedef struct
{
    weather_service_state_t state;
    char city[WEATHER_SERVICE_CITY_MAX];
    weather_service_day_t days[WEATHER_SERVICE_FORECAST_DAYS];
    uint8_t day_count;
} weather_service_snapshot_t;

rt_err_t weather_service_init(void);
rt_err_t weather_service_request(void);
rt_err_t weather_service_get_snapshot(weather_service_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif

#endif /* FACTORY_WEATHER_SERVICE_H */
