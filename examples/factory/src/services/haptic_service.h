#ifndef FACTORY_HAPTIC_SERVICE_H
#define FACTORY_HAPTIC_SERVICE_H

#include <stdbool.h>
#include <stdint.h>
#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    HAPTIC_EFFECT_SHORT = 0,
    HAPTIC_EFFECT_DOUBLE,
    HAPTIC_EFFECT_LONG,
    HAPTIC_EFFECT_HEARTBEAT,
    HAPTIC_EFFECT_COUNT,
} haptic_effect_t;

rt_err_t haptic_service_init(void);
bool haptic_service_is_ready(void);
rt_err_t haptic_service_set_enabled(bool enabled);
rt_err_t haptic_service_get_config(bool *enabled, uint8_t *amplitude_percent,
                                   haptic_effect_t *effect);
rt_err_t haptic_service_preview(uint8_t amplitude_percent,
                                haptic_effect_t effect);
rt_err_t haptic_service_play_key_feedback(void);
rt_err_t haptic_service_play_alert(uint32_t duration_ms);

#ifdef __cplusplus
}
#endif

#endif /* FACTORY_HAPTIC_SERVICE_H */
