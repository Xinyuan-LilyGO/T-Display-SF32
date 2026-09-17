#ifndef FACTORY_SENSOR_SERVICE_H
#define FACTORY_SENSOR_SERVICE_H

#include <stdbool.h>
#include <stdint.h>
#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

enum
{
    SENSOR_DATA_TEMPERATURE   = (1UL << 0),
    SENSOR_DATA_HUMIDITY      = (1UL << 1),
    SENSOR_DATA_PRESSURE      = (1UL << 2),
    SENSOR_DATA_ACCELEROMETER = (1UL << 3),
    SENSOR_DATA_GYROSCOPE     = (1UL << 4),
    SENSOR_DATA_STEP_COUNTER  = (1UL << 5),
    SENSOR_DATA_ALL           = (1UL << 6) - 1UL,
};

enum
{
    SENSOR_INIT_ERROR_NONE     = 0,
    SENSOR_INIT_ERROR_BME280   = (1UL << 0),
    SENSOR_INIT_ERROR_BHI260AP = (1UL << 1),
};

typedef struct
{
    /* RT-Thread Sensor units: 0.1 C, 0.1 %RH, Pa, mg and mdps. */
    int32_t temperature;
    int32_t humidity;
    int32_t pressure;
    int32_t acceleration[3];
    int32_t gyroscope[3];
    uint32_t step_count;
    rt_tick_t sample_tick;
    uint32_t valid_mask;
} sensor_service_data_t;

rt_err_t sensor_service_init(void);
rt_err_t sensor_service_deinit(void);
bool sensor_service_is_ready(void);
uint32_t sensor_service_get_init_errors(void);

/* The caller must inspect valid_mask before using individual measurements. */
rt_err_t sensor_service_get_data(sensor_service_data_t *data);

#ifdef __cplusplus
}
#endif

#endif /* FACTORY_SENSOR_SERVICE_H */
