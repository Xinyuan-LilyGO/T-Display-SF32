#include "RadioLib.h"

#include <rtthread.h>

#define LOG_TAG "sx1262.tx"
#include <ulog.h>

#define RADIO_SEND_INTERVAL_MS 3000U

static Module_t radio_module;
static SX1262_t radio;

static int16_t radio_init(void)
{
    ModuleConfig_t module_config;
    SX1262Config_t radio_config;
    int16_t state;

    Module_configDefault(&module_config);
    module_config.board_init = RadioLib_boardInitTDisplaySF32;

    state = Module_init(&radio_module, &module_config);
    if (state != RADIOLIB_ERR_NONE)
    {
        return state;
    }

    SX1262_configDefault(&radio_config);
    radio_config.output_power_dbm = 22;

    state = SX1262_beginConfig(&radio, &radio_module, &radio_config);
    if (state != RADIOLIB_ERR_NONE)
    {
        Module_deinit(&radio_module);
    }
    return state;
}

int main(void)
{
    char payload[64];
    rt_uint32_t sequence = 0;
    int payload_length;
    int16_t state;

    state = radio_init();
    if (state != RADIOLIB_ERR_NONE)
    {
        LOG_E("SX1262 init failed: %d", state);
        return state;
    }

    state = SX126x_setFrequency(&radio, 915000000);
    if (state != RADIOLIB_ERR_NONE)
    {
        LOG_E("SX1262 set freq failed: %d", state);
        return state;
    }
    state = SX126x_setBandwidth(&radio, RADIOLIB_SX126X_BW_250);
    if (state != RADIOLIB_ERR_NONE)
    {
        LOG_E("SX1262 set bw failed: %d", state);
        return state;
    }

    LOG_I("SX1262 ready, start transmitting");
    while (1)
    {
        payload_length = rt_snprintf(payload, sizeof(payload),
                                     "T-Display-SF32 LoRa #%lu",
                                     (unsigned long)sequence++);
        if ((payload_length <= 0) ||
            ((rt_size_t)payload_length >= sizeof(payload)))
        {
            LOG_E("payload formatting failed");
            rt_thread_mdelay(RADIO_SEND_INTERVAL_MS);
            continue;
        }

        state = SX126x_transmit(&radio, (const uint8_t *)payload,
                                (size_t)payload_length);
        if (state == RADIOLIB_ERR_NONE)
        {
            LOG_I("sent: %s", payload);
        }
        else
        {
            LOG_E("transmit failed: %d", state);
        }

        rt_thread_mdelay(RADIO_SEND_INTERVAL_MS);
    }
}
