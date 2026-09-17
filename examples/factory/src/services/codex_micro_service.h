#ifndef FACTORY_CODEX_MICRO_SERVICE_H
#define FACTORY_CODEX_MICRO_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CODEX_MICRO_AGENT_COUNT 6U

typedef enum
{
    CODEX_MICRO_AGENT_OFF = 0,
    CODEX_MICRO_AGENT_IDLE,
    CODEX_MICRO_AGENT_WORKING,
    CODEX_MICRO_AGENT_COMPLETE,
    CODEX_MICRO_AGENT_AWAITING_INPUT,
    CODEX_MICRO_AGENT_ERROR,
    CODEX_MICRO_AGENT_UNKNOWN,
} codex_micro_agent_state_t;

typedef enum
{
    CODEX_MICRO_KEY_AGENT_0 = 0,
    CODEX_MICRO_KEY_AGENT_1,
    CODEX_MICRO_KEY_AGENT_2,
    CODEX_MICRO_KEY_AGENT_3,
    CODEX_MICRO_KEY_AGENT_4,
    CODEX_MICRO_KEY_AGENT_5,
    CODEX_MICRO_KEY_FAST,
    CODEX_MICRO_KEY_APPROVE,
    CODEX_MICRO_KEY_DECLINE,
    CODEX_MICRO_KEY_SPLIT,
    CODEX_MICRO_KEY_MIC,
    CODEX_MICRO_KEY_MIC_SECONDARY,
    CODEX_MICRO_KEY_SEND,
} codex_micro_key_t;

typedef struct
{
    codex_micro_agent_state_t state;
    bool selected;
    uint32_t color;
} codex_micro_agent_t;

typedef struct
{
    bool initialized;
    bool stack_ready;
    bool advertising;
    bool connected;
    bool bonded;
    bool encrypted;
    bool protocol_ready;
    uint8_t last_disconnect_reason;
    uint16_t mtu;
    uint32_t rx_messages;
    uint32_t tx_messages;
    uint32_t protocol_errors;
    codex_micro_agent_t agents[CODEX_MICRO_AGENT_COUNT];
} codex_micro_status_t;

rt_err_t codex_micro_service_init(void);
rt_err_t codex_micro_service_start(void);
rt_err_t codex_micro_service_get_status(codex_micro_status_t *status);
rt_err_t codex_micro_service_send_key(codex_micro_key_t key, bool pressed);
rt_err_t codex_micro_service_send_key_pulse(codex_micro_key_t key);
rt_err_t codex_micro_service_send_encoder(bool clockwise);
rt_err_t codex_micro_service_send_radial(float angle, float distance);

#ifdef __cplusplus
}
#endif

#endif /* FACTORY_CODEX_MICRO_SERVICE_H */
