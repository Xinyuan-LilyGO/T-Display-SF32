#ifndef FACTORY_INFRARED_SERVICE_H
#define FACTORY_INFRARED_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    INFRARED_SERVICE_STATE_UNAVAILABLE = 0,
    INFRARED_SERVICE_STATE_READY,
    INFRARED_SERVICE_STATE_SENDING,
    INFRARED_SERVICE_STATE_SENT,
    INFRARED_SERVICE_STATE_ERROR,
} infrared_service_state_t;

rt_err_t infrared_service_init(void);
bool infrared_service_is_ready(void);
rt_err_t infrared_service_send_nec(uint8_t address, uint8_t command);
rt_err_t infrared_service_send_nec_repeat(void);
infrared_service_state_t infrared_service_get_state(void);

#ifdef __cplusplus
}
#endif

#endif /* FACTORY_INFRARED_SERVICE_H */
