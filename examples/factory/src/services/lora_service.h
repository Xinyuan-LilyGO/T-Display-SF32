#ifndef FACTORY_LORA_SERVICE_H
#define FACTORY_LORA_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    LORA_SERVICE_BW_125 = 0,
    LORA_SERVICE_BW_250,
    LORA_SERVICE_BW_500,
} lora_service_bandwidth_t;

typedef struct
{
    uint32_t frequency_hz;
    lora_service_bandwidth_t bandwidth;
    uint8_t spreading_factor;
    uint8_t coding_rate;
    uint8_t sync_word;
    int8_t output_power_dbm;
    uint16_t preamble_length;
    uint32_t tx_timeout_ms;
    bool crc_enabled;
    bool invert_iq;
} lora_service_config_t;

typedef struct
{
    bool initialized;
    lora_service_config_t config;
    int16_t packet_rssi_dbm;
    int8_t packet_snr_db;
    int32_t frequency_error_hz;
    int16_t last_radiolib_state;
} lora_service_status_t;

void lora_service_config_default(lora_service_config_t *config);
rt_err_t lora_service_init(void);
void lora_service_deinit(void);
bool lora_service_is_ready(void);
rt_err_t lora_service_configure(const lora_service_config_t *config);
rt_err_t lora_service_get_status(lora_service_status_t *status);
rt_err_t lora_service_send(const uint8_t *data, rt_size_t length);
rt_err_t lora_service_receive(uint8_t *data, rt_size_t *length,
                              uint32_t timeout_ms);
rt_err_t lora_service_stop_receive(void);

#ifdef __cplusplus
}
#endif

#endif /* FACTORY_LORA_SERVICE_H */
