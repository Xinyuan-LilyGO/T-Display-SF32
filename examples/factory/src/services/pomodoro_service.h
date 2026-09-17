#ifndef FACTORY_POMODORO_SERVICE_H
#define FACTORY_POMODORO_SERVICE_H

#include <stdbool.h>
#include <stdint.h>
#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    POMODORO_STATE_READY = 0,
    POMODORO_STATE_RUNNING,
    POMODORO_STATE_PAUSED,
    POMODORO_STATE_FINISHED,
} pomodoro_state_t;

typedef struct
{
    pomodoro_state_t state;
    uint32_t duration_seconds;
    uint32_t remaining_seconds;
} pomodoro_status_t;

rt_err_t pomodoro_service_init(void);
rt_err_t pomodoro_service_set_duration(uint8_t minutes);
rt_err_t pomodoro_service_start(void);
rt_err_t pomodoro_service_pause(void);
rt_err_t pomodoro_service_reset(void);
rt_err_t pomodoro_service_get_status(pomodoro_status_t *status);
bool pomodoro_service_take_completion(void);

#ifdef __cplusplus
}
#endif

#endif /* FACTORY_POMODORO_SERVICE_H */
