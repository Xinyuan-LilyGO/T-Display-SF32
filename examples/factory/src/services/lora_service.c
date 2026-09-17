#include "lora_service.h"

#include <string.h>

#include "RadioLib.h"
#include <drivers/pin.h>
#include <drivers/spi.h>

#ifdef RT_USING_FINSH
#include <finsh.h>
#endif

#define LOG_TAG "lora_srv"
#include <ulog.h>

#define LORA_SERVICE_DEFAULT_FREQUENCY_HZ 868000000U
#define LORA_SERVICE_DEFAULT_TX_TIMEOUT_MS 5000U
#define LORA_SERVICE_CMD_SET_DIO_IRQ_PARAMS 0x08U
#define LORA_SERVICE_CMD_CLEAR_IRQ_STATUS   0x02U
#define LORA_SERVICE_IRQ_ALL                0x03FFU

static Module_t lora_module;
static SX1262_t lora_radio;
static rt_mutex_t lora_lock;
static lora_service_config_t lora_active_config;
static int16_t lora_last_radiolib_state = RADIOLIB_ERR_NOT_INITIALIZED;
static bool lora_initialized;

static void lora_service_park_shared_spi(void)
{
#if defined(RT_USING_SPI_MSD) && defined(RT_USING_SPI_MSD_CS_PIN)
    struct rt_spi_device *sd_spi;
    struct rt_spi_message message;
    uint8_t idle_bytes[2] = {0xFFU, 0xFFU};
    uint8_t idle_response[2] = {0U, 0U};
    rt_uint32_t transferred;

    sd_spi = (struct rt_spi_device *)rt_device_find("sdcard");
    if (sd_spi == RT_NULL || sd_spi->bus == RT_NULL)
        return;

    rt_pin_write(lora_module.config.cs, PIN_HIGH);
    rt_pin_write(MSD_SPI_CS_PIN, PIN_HIGH);
    if (rt_spi_take_bus(sd_spi) != RT_EOK)
    {
        LOG_W("Cannot return shared SPI bus to SD card");
        return;
    }

    memset(&message, 0, sizeof(message));
    message.send_buf = idle_bytes;
    message.recv_buf = idle_response;
    message.length = sizeof(idle_bytes);
    message.cs_take = 0U;
    message.cs_release = 0U;
    transferred = sd_spi->bus->ops->xfer(sd_spi, &message);
    rt_spi_release_bus(sd_spi);
    if (transferred != sizeof(idle_bytes))
        LOG_W("SD SPI idle clock failed");
#endif
}

static int16_t lora_service_stop_receive_locked(void)
{
    uint8_t irq_clear[2] = {
        (uint8_t)(LORA_SERVICE_IRQ_ALL >> 8),
        (uint8_t)LORA_SERVICE_IRQ_ALL,
    };
    uint8_t irq_disable[8] = {0U};
    int16_t state;

    /* Complete the previous SD transaction in SD mode before changing the
     * shared controller back to the radio's SPI mode. */
    lora_service_park_shared_spi();
    state = SX126x_standby(&lora_radio);
    if (state != RADIOLIB_ERR_NONE)
        return state;
    state = Module_writeCommand(&lora_module,
                                LORA_SERVICE_CMD_CLEAR_IRQ_STATUS,
                                irq_clear, sizeof(irq_clear), true);
    if (state != RADIOLIB_ERR_NONE)
        return state;
    return Module_writeCommand(&lora_module,
                               LORA_SERVICE_CMD_SET_DIO_IRQ_PARAMS,
                               irq_disable, sizeof(irq_disable), true);
}

static rt_err_t lora_service_map_error(int16_t state)
{
    switch (state)
    {
    case RADIOLIB_ERR_NONE:
        return RT_EOK;
    case RADIOLIB_ERR_INVALID_ARGUMENT:
    case RADIOLIB_ERR_INVALID_BANDWIDTH:
    case RADIOLIB_ERR_INVALID_SPREADING_FACTOR:
    case RADIOLIB_ERR_INVALID_CODING_RATE:
    case RADIOLIB_ERR_INVALID_FREQUENCY:
    case RADIOLIB_ERR_INVALID_OUTPUT_POWER:
    case RADIOLIB_ERR_INVALID_PREAMBLE_LENGTH:
    case RADIOLIB_ERR_PACKET_TOO_LONG:
        return -RT_EINVAL;
    case RADIOLIB_ERR_TX_TIMEOUT:
    case RADIOLIB_ERR_RX_TIMEOUT:
    case RADIOLIB_ERR_BUSY_TIMEOUT:
        return -RT_ETIMEOUT;
    case RADIOLIB_ERR_MEMORY_ALLOCATION_FAILED:
        return -RT_ENOMEM;
    case RADIOLIB_ERR_CHIP_NOT_FOUND:
    case RADIOLIB_ERR_UNSUPPORTED:
        return -RT_ENOSYS;
    default:
        return -RT_ERROR;
    }
}

static SX126xBandwidth_t lora_service_map_bandwidth(
    lora_service_bandwidth_t bandwidth)
{
    switch (bandwidth)
    {
    case LORA_SERVICE_BW_250:
        return RADIOLIB_SX126X_BW_250;
    case LORA_SERVICE_BW_500:
        return RADIOLIB_SX126X_BW_500;
    case LORA_SERVICE_BW_125:
    default:
        return RADIOLIB_SX126X_BW_125;
    }
}

static void lora_service_build_radio_config(
    const lora_service_config_t *service_config,
    SX1262Config_t *radio_config)
{
    SX1262_configDefault(radio_config);
    radio_config->frequency_hz = service_config->frequency_hz;
    radio_config->bandwidth =
        lora_service_map_bandwidth(service_config->bandwidth);
    radio_config->spreading_factor = service_config->spreading_factor;
    radio_config->coding_rate = service_config->coding_rate;
    radio_config->sync_word = service_config->sync_word;
    radio_config->output_power_dbm = service_config->output_power_dbm;
    radio_config->preamble_length = service_config->preamble_length;
    radio_config->tx_timeout_ms = service_config->tx_timeout_ms;
    radio_config->crc_enabled = service_config->crc_enabled;
    radio_config->invert_iq = service_config->invert_iq;
}

static int16_t lora_service_recover_radio_locked(void)
{
    SX1262Config_t radio_config;
    int16_t state;

    LOG_W("SX1262 BUSY timeout, reset and restore radio configuration");
    rt_pin_write(lora_module.config.cs, PIN_HIGH);
#if defined(RT_USING_SPI_MSD) && defined(RT_USING_SPI_MSD_CS_PIN)
    rt_pin_write(MSD_SPI_CS_PIN, PIN_HIGH);
#endif
    lora_service_build_radio_config(&lora_active_config, &radio_config);
    state = SX1262_beginConfig(&lora_radio, &lora_module, &radio_config);
    if (state != RADIOLIB_ERR_NONE)
        LOG_E("SX1262 recovery failed: %d", state);
    return state;
}

static rt_err_t lora_service_validate_config(
    const lora_service_config_t *config)
{
    if (config == RT_NULL)
        return -RT_EINVAL;
    if (config->frequency_hz < 150000000U ||
            config->frequency_hz > 960000000U)
        return -RT_EINVAL;
    if (config->bandwidth > LORA_SERVICE_BW_500)
        return -RT_EINVAL;
    if (config->spreading_factor < 5U ||
            config->spreading_factor > 12U)
        return -RT_EINVAL;
    if (config->coding_rate < 5U || config->coding_rate > 8U)
        return -RT_EINVAL;
    if (config->output_power_dbm < -9 || config->output_power_dbm > 22)
        return -RT_EINVAL;
    if (config->preamble_length == 0U || config->tx_timeout_ms == 0U)
        return -RT_EINVAL;
    return RT_EOK;
}

static int16_t lora_service_apply_config(
    const lora_service_config_t *config)
{
    int16_t state;

    state = SX126x_setFrequency(&lora_radio, config->frequency_hz);
    if (state != RADIOLIB_ERR_NONE)
        return state;
    state = SX126x_setBandwidth(
        &lora_radio, lora_service_map_bandwidth(config->bandwidth));
    if (state != RADIOLIB_ERR_NONE)
        return state;
    state = SX126x_setSpreadingFactor(
        &lora_radio, config->spreading_factor);
    if (state != RADIOLIB_ERR_NONE)
        return state;
    state = SX126x_setCodingRate(&lora_radio, config->coding_rate);
    if (state != RADIOLIB_ERR_NONE)
        return state;
    state = SX126x_setOutputPower(
        &lora_radio, config->output_power_dbm);
    if (state != RADIOLIB_ERR_NONE)
        return state;
    state = SX126x_setPreambleLength(
        &lora_radio, config->preamble_length);
    if (state != RADIOLIB_ERR_NONE)
        return state;
    state = SX126x_setSyncWord(&lora_radio, config->sync_word);
    if (state != RADIOLIB_ERR_NONE)
        return state;
    state = SX126x_setCRC(&lora_radio, config->crc_enabled);
    if (state != RADIOLIB_ERR_NONE)
        return state;
    return SX126x_setInvertIQ(&lora_radio, config->invert_iq);
}

void lora_service_config_default(lora_service_config_t *config)
{
    if (config == RT_NULL)
        return;

    memset(config, 0, sizeof(*config));
    config->frequency_hz = LORA_SERVICE_DEFAULT_FREQUENCY_HZ;
    config->bandwidth = LORA_SERVICE_BW_125;
    config->spreading_factor = 5;
    config->coding_rate = 5;
    config->sync_word = 0x12;
    config->output_power_dbm = 22;
    config->preamble_length = 12;
    config->tx_timeout_ms = LORA_SERVICE_DEFAULT_TX_TIMEOUT_MS;
    config->crc_enabled = false;
    config->invert_iq = false;
}

rt_err_t lora_service_init(void)
{
    ModuleConfig_t module_config;
    SX1262Config_t radio_config;
    lora_service_config_t service_config;
    int16_t state;

    if (lora_initialized)
        return RT_EOK;

    lora_lock = rt_mutex_create("lora_srv", RT_IPC_FLAG_PRIO);
    if (lora_lock == RT_NULL)
        return -RT_ENOMEM;

    Module_configDefault(&module_config);
    module_config.board_init = RadioLib_boardInitTDisplaySF32;
    state = Module_init(&lora_module, &module_config);
    if (state != RADIOLIB_ERR_NONE)
        goto init_failed;

    lora_service_config_default(&service_config);
    lora_service_build_radio_config(&service_config, &radio_config);

    state = SX1262_beginConfig(&lora_radio, &lora_module, &radio_config);
    if (state != RADIOLIB_ERR_NONE)
    {
        Module_deinit(&lora_module);
        goto init_failed;
    }

    lora_active_config = service_config;
    lora_last_radiolib_state = RADIOLIB_ERR_NONE;
    lora_initialized = true;
    lora_service_park_shared_spi();
    LOG_I("SX1262 ready: %u Hz, SF%u, CR4/%u, %d dBm",
          (unsigned int)service_config.frequency_hz,
          service_config.spreading_factor,
          service_config.coding_rate,
          service_config.output_power_dbm);
    return RT_EOK;

init_failed:
    lora_last_radiolib_state = state;
    rt_mutex_delete(lora_lock);
    lora_lock = RT_NULL;
    LOG_E("SX1262 initialization failed: %d", state);
    return lora_service_map_error(state);
}

void lora_service_deinit(void)
{
    rt_mutex_t lock;

    if (!lora_initialized || lora_lock == RT_NULL)
        return;
    lock = lora_lock;
    if (rt_mutex_take(lock, RT_WAITING_FOREVER) != RT_EOK)
        return;

    lora_initialized = false;
    lora_lock = RT_NULL;
    SX126x_clearDio1Action(&lora_radio);
    Module_deinit(&lora_module);
    memset(&lora_radio, 0, sizeof(lora_radio));
    memset(&lora_active_config, 0, sizeof(lora_active_config));
    lora_last_radiolib_state = RADIOLIB_ERR_NOT_INITIALIZED;
    rt_mutex_release(lock);
    rt_mutex_delete(lock);
}

bool lora_service_is_ready(void)
{
    return lora_initialized;
}

rt_err_t lora_service_configure(const lora_service_config_t *config)
{
    rt_err_t result;
    int16_t state;

    result = lora_service_validate_config(config);
    if (result != RT_EOK)
        return result;
    if (!lora_initialized || lora_lock == RT_NULL)
        return -RT_ERROR;
    if (rt_mutex_take(lora_lock, RT_WAITING_FOREVER) != RT_EOK)
        return -RT_ERROR;

    lora_service_park_shared_spi();
    state = lora_service_apply_config(config);
    if (state == RADIOLIB_ERR_BUSY_TIMEOUT &&
            lora_service_recover_radio_locked() == RADIOLIB_ERR_NONE)
    {
        state = lora_service_apply_config(config);
    }
    lora_last_radiolib_state = state;
    if (state == RADIOLIB_ERR_NONE)
    {
        lora_radio.config.tx_timeout_ms = config->tx_timeout_ms;
        lora_active_config = *config;
    }
    lora_service_park_shared_spi();
    rt_mutex_release(lora_lock);
    return lora_service_map_error(state);
}

rt_err_t lora_service_get_status(lora_service_status_t *status)
{
    if (status == RT_NULL)
        return -RT_EINVAL;
    if (!lora_initialized || lora_lock == RT_NULL)
        return -RT_ERROR;
    if (rt_mutex_take(lora_lock, RT_WAITING_FOREVER) != RT_EOK)
        return -RT_ERROR;

    memset(status, 0, sizeof(*status));
    status->initialized = true;
    status->config = lora_active_config;
    status->packet_rssi_dbm = SX126x_getRSSI(&lora_radio);
    status->packet_snr_db = SX126x_getSNR(&lora_radio);
    status->frequency_error_hz = SX126x_getFrequencyError(&lora_radio);
    status->last_radiolib_state = lora_last_radiolib_state;
    lora_service_park_shared_spi();
    rt_mutex_release(lora_lock);
    return RT_EOK;
}

rt_err_t lora_service_send(const uint8_t *data, rt_size_t length)
{
    int16_t state;

    if (data == RT_NULL || length == 0U || length > 255U)
        return -RT_EINVAL;
    if (!lora_initialized || lora_lock == RT_NULL)
        return -RT_ERROR;
    if (rt_mutex_take(lora_lock, RT_WAITING_FOREVER) != RT_EOK)
        return -RT_ERROR;

    lora_service_park_shared_spi();
    state = SX126x_transmit(&lora_radio, data, (size_t)length);
    if (state == RADIOLIB_ERR_BUSY_TIMEOUT &&
            lora_service_recover_radio_locked() == RADIOLIB_ERR_NONE)
    {
        state = SX126x_transmit(&lora_radio, data, (size_t)length);
    }
    lora_last_radiolib_state = state;
    lora_service_park_shared_spi();
    rt_mutex_release(lora_lock);
    if (state != RADIOLIB_ERR_NONE)
        LOG_E("SX1262 transmit failed: %d", state);
    return lora_service_map_error(state);
}

rt_err_t lora_service_receive(uint8_t *data, rt_size_t *length,
                              uint32_t timeout_ms)
{
    size_t received_length;
    int16_t state;

    if (data == RT_NULL || length == RT_NULL || *length == 0U ||
            *length > 255U)
        return -RT_EINVAL;
    if (!lora_initialized || lora_lock == RT_NULL)
        return -RT_ERROR;
    if (rt_mutex_take(lora_lock, RT_WAITING_FOREVER) != RT_EOK)
        return -RT_ERROR;

    lora_service_park_shared_spi();
    received_length = (size_t)*length;
    state = SX126x_receive(&lora_radio, data, &received_length, timeout_ms);
    if (state == RADIOLIB_ERR_BUSY_TIMEOUT &&
            lora_service_recover_radio_locked() == RADIOLIB_ERR_NONE)
    {
        received_length = (size_t)*length;
        state = SX126x_receive(&lora_radio, data, &received_length,
                               timeout_ms);
    }
    lora_last_radiolib_state = state;
    if (state == RADIOLIB_ERR_NONE || state == RADIOLIB_ERR_PACKET_TOO_LONG)
        *length = (rt_size_t)received_length;
    lora_service_park_shared_spi();
    rt_mutex_release(lora_lock);
    return lora_service_map_error(state);
}

rt_err_t lora_service_stop_receive(void)
{
    int16_t state;

    if (!lora_initialized || lora_lock == RT_NULL)
        return -RT_ERROR;
    if (rt_mutex_take(lora_lock, RT_WAITING_FOREVER) != RT_EOK)
        return -RT_ERROR;

    state = lora_service_stop_receive_locked();
    if (state == RADIOLIB_ERR_BUSY_TIMEOUT &&
            lora_service_recover_radio_locked() == RADIOLIB_ERR_NONE)
    {
        state = lora_service_stop_receive_locked();
    }
    lora_last_radiolib_state = state;
    lora_service_park_shared_spi();
    rt_mutex_release(lora_lock);
    if (state != RADIOLIB_ERR_NONE)
        LOG_E("SX1262 stop receive failed: %d", state);
    return lora_service_map_error(state);
}

#ifdef RT_USING_FINSH
static int lora_send(int argc, char **argv)
{
    char message[256];
    rt_size_t length = 0U;
    rt_err_t result;
    int index;

    if (argc < 2)
    {
        rt_kprintf("Usage: lora_send <message>\n");
        return -RT_EINVAL;
    }

    for (index = 1; index < argc; index++)
    {
        rt_size_t argument_length = (rt_size_t)strlen(argv[index]);
        rt_size_t separator_length = (index > 1) ? 1U : 0U;

        if (argument_length == 0U)
            continue;
        if (length + separator_length + argument_length > 255U)
        {
            rt_kprintf("LoRa message is too long (max 255 bytes)\n");
            return -RT_EINVAL;
        }
        if (separator_length != 0U)
            message[length++] = ' ';
        memcpy(&message[length], argv[index], argument_length);
        length += argument_length;
    }

    if (length == 0U)
    {
        rt_kprintf("LoRa message cannot be empty\n");
        return -RT_EINVAL;
    }
    message[length] = '\0';

    if (!lora_service_is_ready())
    {
        rt_kprintf("LoRa radio is not ready\n");
        return -RT_ERROR;
    }

    result = lora_service_send((const uint8_t *)message, length);
    if (result == RT_EOK)
        rt_kprintf("LoRa sent: %s\n", message);
    else
        rt_kprintf("LoRa send failed: %d\n", (int)result);
    return (int)result;
}
MSH_CMD_EXPORT(lora_send, send a LoRa text packet);
#endif
