#include "lora_page.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <lvgl.h>
#include <ulog.h>

#include "ui_config.h"
#include "lora_service.h"
#include "ui_navigation.h"
#include "ui_status_bar.h"
#include "ui_theme.h"

#define LORA_SIDE_PADDING 16
#define LORA_HEADER_Y (UI_STATUS_BAR_HEIGHT + 4)
#define LORA_HEADER_HEIGHT 48
#define LORA_CONTENT_Y (LORA_HEADER_Y + LORA_HEADER_HEIGHT + 4)
#define LORA_CONTENT_WIDTH (UI_SCREEN_WIDTH - LORA_SIDE_PADDING * 2)
#define LORA_SETTING_ROW_HEIGHT 50
#define LORA_PARAM_COUNT 6U
#define LORA_OPTION_MAX_COUNT 12U
#define LORA_OPTION_ROW_HEIGHT 46
#define LORA_KEYBOARD_Y 20
#define LORA_KEYBOARD_HEIGHT 292
#define LORA_PAGE_REFRESH_MS 100U
#define LORA_RX_TIMEOUT_MS 100U
#define LORA_WORK_QUEUE_DEPTH 6U
#define LORA_WORKER_STACK_SIZE 3072U
#define LORA_WORKER_PRIORITY (RT_THREAD_PRIORITY_MIDDLE - 1U)
#define LORA_WORKER_TICK 10U
#define LORA_MESSAGE_MAX_LENGTH 96U
#define LORA_PACKET_MAX_LENGTH 255U

LV_IMAGE_DECLARE(lora_icon);

typedef enum
{
    LORA_PARAM_FREQUENCY = 0,
    LORA_PARAM_BW,
    LORA_PARAM_SF,
    LORA_PARAM_CR,
    LORA_PARAM_POWER,
    LORA_PARAM_INTERVAL,
} lora_param_id_t;

typedef struct
{
    uint8_t frequency;
    uint8_t bw;
    uint8_t sf;
    uint8_t cr;
    uint8_t power;
    uint8_t interval;
} lora_ui_parameters_t;

typedef enum
{
    LORA_WORK_SET_ACTIVE = 0,
    LORA_WORK_SEND,
    LORA_WORK_CONFIGURE,
    LORA_WORK_SET_PERIODIC,
    LORA_WORK_SET_LISTEN,
    LORA_WORK_STOP,
} lora_work_type_t;

typedef struct
{
    lora_work_type_t type;
    bool enabled;
    uint32_t interval_ms;
    lora_service_config_t config;
    char payload[LORA_MESSAGE_MAX_LENGTH + 1U];
} lora_work_t;

typedef enum
{
    LORA_RESULT_NONE = 0,
    LORA_RESULT_READY,
    LORA_RESULT_TX_OK,
    LORA_RESULT_TX_FAILED,
    LORA_RESULT_RX_OK,
    LORA_RESULT_RX_FAILED,
    LORA_RESULT_CONFIG_OK,
    LORA_RESULT_CONFIG_FAILED,
} lora_result_type_t;

typedef struct
{
    lora_result_type_t result_type;
    rt_err_t result;
    lora_service_config_t config;
    uint32_t revision;
    uint32_t tx_count;
    uint32_t rx_count;
    int16_t rssi_dbm;
    int8_t snr_db;
    char last_tx[LORA_MESSAGE_MAX_LENGTH + 1U];
    char last_rx[LORA_MESSAGE_MAX_LENGTH + 1U];
} lora_page_snapshot_t;

static const uint32_t lora_frequency_hz_values[] = {
    433000000U,
    868000000U,
    915000000U,
    923000000U,
};
static const int8_t lora_power_dbm_values[] = {
    -9,
    -6,
    -3,
    0,
    3,
    6,
    9,
    12,
    14,
    17,
    20,
    22,
};
static const uint32_t lora_interval_ms_values[] = {
    1000U,
    2000U,
    5000U,
    10000U,
    15000U,
    30000U,
    60000U,
};
static const char *const lora_frequency_values[] = {
    "433 MHz",
    "868 MHz",
    "915 MHz",
    "923 MHz",
};

static const char *const lora_bw_values[] = {
    "125 kHz",
    "250 kHz",
    "500 kHz",
};
static const char *const lora_sf_values[] = {
    "SF5",
    "SF6",
    "SF7",
    "SF8",
    "SF9",
    "SF10",
    "SF11",
    "SF12",
};
static const char *const lora_cr_values[] = {
    "4/5",
    "4/6",
    "4/7",
    "4/8",
};
static const char *const lora_power_values[] = {
    "-9 dBm",
    "-6 dBm",
    "-3 dBm",
    "0 dBm",
    "+3 dBm",
    "+6 dBm",
    "+9 dBm",
    "+12 dBm",
    "+14 dBm",
    "+17 dBm",
    "+20 dBm",
    "+22 dBm",
};
static const char *const lora_interval_values[] = {
    "1 s",
    "2 s",
    "5 s",
    "10 s",
    "15 s",
    "30 s",
    "60 s",
};
static const char *const lora_parameter_titles[LORA_PARAM_COUNT] = {
    "Frequency",
    "Bandwidth",
    "Spreading factor",
    "Coding rate",
    "Transmit power",
    "Send interval",
};

static lora_ui_parameters_t lora_saved_parameters = {
    .frequency = 0U,
    .bw = 0U,
    .sf = 0U,
    .cr = 0U,
    .power = 11U,
    .interval = 0U,
};
static lora_ui_parameters_t lora_edit_parameters;

static ui_page_t *lora_page_owner;
static lv_obj_t *lora_main_view;
static lv_obj_t *lora_settings_view;
static lv_obj_t *lora_main_back;
static lv_obj_t *lora_settings_button;
static lv_obj_t *lora_config_panel;
static lv_obj_t *lora_log_panel;
static lv_obj_t *lora_message_input;
static lv_obj_t *lora_send_button;
static lv_obj_t *lora_timed_button;
static lv_obj_t *lora_timed_label;
static lv_obj_t *lora_listen_button;
static lv_obj_t *lora_listen_label;
static lv_obj_t *lora_status_label;
static lv_obj_t *lora_config_label;
static lv_obj_t *lora_radio_state_label;
static lv_obj_t *lora_rx_meta_label;
static lv_obj_t *lora_rx_message_label;
static lv_obj_t *lora_tx_message_label;
static lv_obj_t *lora_keyboard;
static lv_obj_t *lora_settings_back;
static lv_obj_t *lora_save_button;
static lv_obj_t *lora_parameter_controls[LORA_PARAM_COUNT];
static lv_obj_t *lora_parameter_value_labels[LORA_PARAM_COUNT];
static lv_obj_t *lora_option_view;
static lv_obj_t *lora_option_back;
static lv_obj_t *lora_option_title;
static lv_obj_t *lora_option_body;
static lv_obj_t *lora_option_buttons[LORA_OPTION_MAX_COUNT];
static lv_obj_t *lora_option_labels[LORA_OPTION_MAX_COUNT];
static lora_param_id_t lora_active_parameter;
static uint8_t lora_active_option_count;
static bool lora_timed_enabled;
static bool lora_listen_enabled;
static bool lora_snapshot_lock_initialized;
static struct rt_mutex lora_snapshot_lock;
static rt_mq_t lora_work_queue;
static rt_thread_t lora_worker_thread;
static rt_sem_t lora_worker_stop_sem;
static volatile bool lora_worker_stopping;
static bool lora_worker_restart_pending;
static lora_page_snapshot_t lora_snapshot;
static uint32_t lora_displayed_revision;

static void lora_page_add_main_group_objects(ui_page_t *page);
static void lora_page_add_settings_group_objects(ui_page_t *page);
static void lora_page_hide_option_view(void);
static void lora_page_create_keyboard(ui_page_t *page);
static void lora_page_create_settings_view(ui_page_t *page);

static uint8_t lora_page_find_frequency(uint32_t frequency_hz)
{
    uint8_t index;

    for (index = 0U; index < (uint8_t)(sizeof(lora_frequency_hz_values) /
                                       sizeof(lora_frequency_hz_values[0]));
         index++)
    {
        if (lora_frequency_hz_values[index] == frequency_hz)
            return index;
    }
    return 0U;
}

static uint8_t lora_page_find_power(int8_t power_dbm)
{
    uint8_t index;

    for (index = 0U; index < (uint8_t)(sizeof(lora_power_dbm_values) /
                                       sizeof(lora_power_dbm_values[0]));
         index++)
    {
        if (lora_power_dbm_values[index] == power_dbm)
            return index;
    }
    return 11U;
}

static void lora_page_parameters_from_config(
    const lora_service_config_t *config)
{
    if (config == RT_NULL)
        return;

    lora_saved_parameters.frequency =
        lora_page_find_frequency(config->frequency_hz);
    if (config->bandwidth <= LORA_SERVICE_BW_500)
        lora_saved_parameters.bw = (uint8_t)config->bandwidth;
    if (config->spreading_factor >= 5U &&
        config->spreading_factor <= 12U)
    {
        lora_saved_parameters.sf =
            (uint8_t)(config->spreading_factor - 5U);
    }
    if (config->coding_rate >= 5U && config->coding_rate <= 8U)
        lora_saved_parameters.cr = (uint8_t)(config->coding_rate - 5U);
    lora_saved_parameters.power =
        lora_page_find_power(config->output_power_dbm);
}

static uint8_t lora_page_get_parameter_index(
    const lora_ui_parameters_t *parameters, lora_param_id_t parameter)
{
    switch (parameter)
    {
    case LORA_PARAM_FREQUENCY:
        return parameters->frequency;
    case LORA_PARAM_BW:
        return parameters->bw;
    case LORA_PARAM_SF:
        return parameters->sf;
    case LORA_PARAM_CR:
        return parameters->cr;
    case LORA_PARAM_POWER:
        return parameters->power;
    case LORA_PARAM_INTERVAL:
    default:
        return parameters->interval;
    }
}

static void lora_page_set_parameter_index(
    lora_ui_parameters_t *parameters, lora_param_id_t parameter,
    uint8_t index)
{
    switch (parameter)
    {
    case LORA_PARAM_FREQUENCY:
        parameters->frequency = index;
        break;
    case LORA_PARAM_BW:
        parameters->bw = index;
        break;
    case LORA_PARAM_SF:
        parameters->sf = index;
        break;
    case LORA_PARAM_CR:
        parameters->cr = index;
        break;
    case LORA_PARAM_POWER:
        parameters->power = index;
        break;
    case LORA_PARAM_INTERVAL:
    default:
        parameters->interval = index;
        break;
    }
}

static const char *const *lora_page_get_parameter_values(
    lora_param_id_t parameter, uint8_t *count)
{
    switch (parameter)
    {
    case LORA_PARAM_FREQUENCY:
        *count = (uint8_t)(sizeof(lora_frequency_values) /
                           sizeof(lora_frequency_values[0]));
        return lora_frequency_values;
    case LORA_PARAM_BW:
        *count = (uint8_t)(sizeof(lora_bw_values) /
                           sizeof(lora_bw_values[0]));
        return lora_bw_values;
    case LORA_PARAM_SF:
        *count = (uint8_t)(sizeof(lora_sf_values) /
                           sizeof(lora_sf_values[0]));
        return lora_sf_values;
    case LORA_PARAM_CR:
        *count = (uint8_t)(sizeof(lora_cr_values) /
                           sizeof(lora_cr_values[0]));
        return lora_cr_values;
    case LORA_PARAM_POWER:
        *count = (uint8_t)(sizeof(lora_power_values) /
                           sizeof(lora_power_values[0]));
        return lora_power_values;
    case LORA_PARAM_INTERVAL:
    default:
        *count = (uint8_t)(sizeof(lora_interval_values) /
                           sizeof(lora_interval_values[0]));
        return lora_interval_values;
    }
}

static void lora_page_snapshot_update(lora_result_type_t type,
                                      rt_err_t result,
                                      const char *payload,
                                      const lora_service_status_t *status)
{
    if (!lora_snapshot_lock_initialized)
        return;
    if (rt_mutex_take(&lora_snapshot_lock,
                      RT_WAITING_FOREVER) != RT_EOK)
        return;

    lora_snapshot.result_type = type;
    lora_snapshot.result = result;
    if (status != RT_NULL)
    {
        lora_snapshot.config = status->config;
        lora_snapshot.rssi_dbm = status->packet_rssi_dbm;
        lora_snapshot.snr_db = status->packet_snr_db;
    }
    if (payload != RT_NULL)
    {
        if (type == LORA_RESULT_TX_OK || type == LORA_RESULT_TX_FAILED)
        {
            rt_strncpy(lora_snapshot.last_tx, payload,
                       sizeof(lora_snapshot.last_tx) - 1U);
            lora_snapshot.last_tx[sizeof(lora_snapshot.last_tx) - 1U] = '\0';
            if (type == LORA_RESULT_TX_OK)
                lora_snapshot.tx_count++;
        }
        else if (type == LORA_RESULT_RX_OK)
        {
            rt_strncpy(lora_snapshot.last_rx, payload,
                       sizeof(lora_snapshot.last_rx) - 1U);
            lora_snapshot.last_rx[sizeof(lora_snapshot.last_rx) - 1U] = '\0';
            lora_snapshot.rx_count++;
        }
    }
    lora_snapshot.revision++;
    rt_mutex_release(&lora_snapshot_lock);
}

static void lora_page_make_display_payload(char *output, size_t output_size,
                                           const uint8_t *data,
                                           rt_size_t length)
{
    rt_size_t index;
    rt_size_t copy_length;

    if (output == RT_NULL || output_size == 0U)
        return;
    copy_length = length;
    if (copy_length >= output_size)
        copy_length = output_size - 1U;
    for (index = 0U; index < copy_length; index++)
    {
        output[index] = (data[index] >= 0x20U && data[index] <= 0x7EU) ? (char)data[index] : '.';
    }
    output[copy_length] = '\0';
}

static void lora_page_worker_send(const char *payload)
{
    lora_service_status_t status;
    rt_err_t result;

    result = lora_service_send((const uint8_t *)payload,
                               (rt_size_t)strlen(payload));
    memset(&status, 0, sizeof(status));
    if (result == RT_EOK)
    {
        (void)lora_service_get_status(&status);
        rt_kprintf("Lora Sent: %s\n", payload);
    }
    lora_page_snapshot_update(result == RT_EOK ? LORA_RESULT_TX_OK : LORA_RESULT_TX_FAILED,
                              result, payload,
                              result == RT_EOK ? &status : RT_NULL);
}

static void lora_page_worker_entry(void *parameter)
{
    lora_work_t work;
    bool active = false;
    bool periodic = false;
    bool listening = false;
    uint32_t periodic_interval_ms = lora_interval_ms_values[0];
    uint32_t periodic_count = 0U;
    rt_tick_t next_periodic_tick = 0U;
    char periodic_payload[LORA_MESSAGE_MAX_LENGTH + 1U] = {0};

    (void)parameter;
    while (!lora_worker_stopping)
    {
        rt_int32_t wait = RT_WAITING_FOREVER;

        if (active && listening)
        {
            wait = 0;
        }
        else if (active && periodic)
        {
            rt_tick_t now = rt_tick_get();
            rt_int32_t ticks_until_send =
                (rt_int32_t)(next_periodic_tick - now);

            wait = ticks_until_send > 0 ? ticks_until_send : 0;
        }

        if (rt_mq_recv(lora_work_queue, &work, sizeof(work), wait) == RT_EOK)
        {
            if (work.type == LORA_WORK_STOP)
            {
                break;
            }
            else if (work.type == LORA_WORK_SET_ACTIVE)
            {
                lora_service_status_t status;

                active = work.enabled;
                if (!active)
                {
                    periodic = false;
                    listening = false;
                    (void)lora_service_stop_receive();
                }
                memset(&status, 0, sizeof(status));
                if (active && lora_service_get_status(&status) == RT_EOK)
                {
                    lora_page_snapshot_update(LORA_RESULT_READY, RT_EOK,
                                              RT_NULL, &status);
                }
            }
            else if (work.type == LORA_WORK_SEND)
            {
                lora_page_worker_send(work.payload);
            }
            else if (work.type == LORA_WORK_CONFIGURE)
            {
                lora_service_status_t status;
                rt_err_t result = lora_service_configure(&work.config);

                memset(&status, 0, sizeof(status));
                if (result == RT_EOK)
                    (void)lora_service_get_status(&status);
                lora_page_snapshot_update(
                    result == RT_EOK ? LORA_RESULT_CONFIG_OK : LORA_RESULT_CONFIG_FAILED,
                    result, RT_NULL,
                    result == RT_EOK ? &status : RT_NULL);
            }
            else if (work.type == LORA_WORK_SET_PERIODIC)
            {
                periodic = work.enabled;
                periodic_interval_ms = work.interval_ms;
                periodic_count = 0U;
                rt_strncpy(periodic_payload, work.payload,
                           sizeof(periodic_payload) - 1U);
                periodic_payload[sizeof(periodic_payload) - 1U] = '\0';
                next_periodic_tick = rt_tick_get();
            }
            else if (work.type == LORA_WORK_SET_LISTEN)
            {
                listening = work.enabled;
                if (!listening)
                    (void)lora_service_stop_receive();
            }
            continue;
        }

        if (!active)
            continue;

        if (periodic && (rt_int32_t)(rt_tick_get() - next_periodic_tick) >= 0)
        {
            char payload[LORA_MESSAGE_MAX_LENGTH + 1U];

            periodic_count++;
            if (periodic_payload[0] != '\0')
            {
                rt_strncpy(payload, periodic_payload, sizeof(payload) - 1U);
                payload[sizeof(payload) - 1U] = '\0';
            }
            else
            {
                rt_snprintf(payload, sizeof(payload), "T-SF32 Display LoRa test #%lu",
                            (unsigned long)periodic_count);
            }
            lora_page_worker_send(payload);
            next_periodic_tick = rt_tick_get() +
                                 rt_tick_from_millisecond(periodic_interval_ms);
        }
        else if (listening)
        {
            uint8_t data[LORA_PACKET_MAX_LENGTH];
            rt_size_t length = sizeof(data);
            rt_err_t result = lora_service_receive(data, &length,
                                                   LORA_RX_TIMEOUT_MS);

            if (result == RT_EOK)
            {
                char payload[LORA_MESSAGE_MAX_LENGTH + 1U];
                lora_service_status_t status;

                lora_page_make_display_payload(payload, sizeof(payload),
                                               data, length);
                memset(&status, 0, sizeof(status));
                (void)lora_service_get_status(&status);
                lora_page_snapshot_update(LORA_RESULT_RX_OK, result,
                                          payload, &status);
            }
            else if (result != -RT_ETIMEOUT)
            {
                lora_page_snapshot_update(LORA_RESULT_RX_FAILED, result,
                                          RT_NULL, RT_NULL);
                rt_thread_mdelay(LORA_RX_TIMEOUT_MS);
            }
        }
    }

    (void)lora_service_stop_receive();
    if (lora_worker_stop_sem != RT_NULL)
        rt_sem_release(lora_worker_stop_sem);
}

static void lora_page_worker_cleanup(void)
{
    lora_worker_thread = RT_NULL;
    if (lora_work_queue != RT_NULL)
    {
        rt_mq_delete(lora_work_queue);
        lora_work_queue = RT_NULL;
    }
    if (lora_worker_stop_sem != RT_NULL)
    {
        rt_sem_delete(lora_worker_stop_sem);
        lora_worker_stop_sem = RT_NULL;
    }
    lora_worker_stopping = false;
}

static rt_err_t lora_page_worker_start(void)
{
    rt_err_t result;

    if (lora_worker_stopping)
    {
        result = rt_sem_take(lora_worker_stop_sem, 0);
        if (result != RT_EOK)
            return -RT_EBUSY;
        lora_page_worker_cleanup();
    }
    if (lora_worker_thread != RT_NULL)
        return RT_EOK;
    lora_worker_stop_sem = rt_sem_create("lora_off", 0,
                                         RT_IPC_FLAG_FIFO);
    if (lora_worker_stop_sem == RT_NULL)
        return -RT_ENOMEM;
    lora_work_queue = rt_mq_create("lora_ui", sizeof(lora_work_t),
                                   LORA_WORK_QUEUE_DEPTH,
                                   RT_IPC_FLAG_FIFO);
    if (lora_work_queue == RT_NULL)
    {
        rt_sem_delete(lora_worker_stop_sem);
        lora_worker_stop_sem = RT_NULL;
        return -RT_ENOMEM;
    }
    lora_worker_stopping = false;
    lora_worker_thread = rt_thread_create(
        "lora_ui", lora_page_worker_entry, RT_NULL,
        LORA_WORKER_STACK_SIZE, LORA_WORKER_PRIORITY, LORA_WORKER_TICK);
    if (lora_worker_thread == RT_NULL)
    {
        rt_mq_delete(lora_work_queue);
        rt_sem_delete(lora_worker_stop_sem);
        lora_work_queue = RT_NULL;
        lora_worker_stop_sem = RT_NULL;
        return -RT_ENOMEM;
    }
    result = rt_thread_startup(lora_worker_thread);
    if (result != RT_EOK)
    {
        rt_thread_delete(lora_worker_thread);
        rt_mq_delete(lora_work_queue);
        rt_sem_delete(lora_worker_stop_sem);
        lora_worker_thread = RT_NULL;
        lora_work_queue = RT_NULL;
        lora_worker_stop_sem = RT_NULL;
    }
    return result;
}

static rt_err_t lora_page_worker_stop(void)
{
    lora_work_t work;
    rt_err_t result;

    if (lora_worker_thread == RT_NULL)
        return RT_EOK;
    if (lora_work_queue == RT_NULL || lora_worker_stop_sem == RT_NULL)
        return -RT_ERROR;

    if (!lora_worker_stopping)
    {
        lora_worker_stopping = true;
        memset(&work, 0, sizeof(work));
        work.type = LORA_WORK_STOP;
        result = rt_mq_send(lora_work_queue, &work, sizeof(work));
        if (result != RT_EOK)
        {
            lora_worker_stopping = false;
            return result;
        }
    }

    result = rt_sem_take(lora_worker_stop_sem, 0);
    if (result != RT_EOK)
        return result;

    lora_page_worker_cleanup();
    return RT_EOK;
}

static rt_err_t lora_page_queue_work(const lora_work_t *work)
{
    if (work == RT_NULL || lora_work_queue == RT_NULL)
        return -RT_ERROR;
    return rt_mq_send(lora_work_queue, (void *)work, sizeof(*work));
}

static lv_obj_t *lora_page_create_label(lv_obj_t *parent,
                                        const char *text,
                                        const lv_font_t *font,
                                        uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_label_set_text(label, text);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(label, 0, 0);
    lv_obj_set_style_pad_all(label, 0, 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_opa(label, LV_OPA_COVER, 0);
    lv_obj_set_style_text_letter_space(label, 0, 0);
    return label;
}

static void lora_page_style_container(lv_obj_t *object,
                                      uint32_t color,
                                      lv_opa_t opacity)
{
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(object, opacity, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_radius(object, 4, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_shadow_width(object, 0, 0);
}

static void lora_page_style_button(lv_obj_t *button,
                                   uint32_t color)
{
    lora_page_style_container(button, color, LV_OPA_COVER);
    ui_theme_apply_focus_style(button);
}

static lv_obj_t *lora_page_create_header_button(lv_obj_t *parent,
                                                lv_coord_t x,
                                                const char *symbol,
                                                lv_event_cb_t callback)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *icon;

    lv_obj_set_size(button, 48, 44);
    lv_obj_set_pos(button, x, LORA_HEADER_Y);
    lora_page_style_button(button, UI_COLOR_BACKGROUND);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, RT_NULL);
    icon = lora_page_create_label(button, symbol,
                                  &lv_font_montserrat_24,
                                  UI_COLOR_TEXT);
    lv_obj_center(icon);
    return button;
}

static void lora_page_update_parameter_labels(void)
{
    if (lora_config_label != RT_NULL)
    {
        lv_label_set_text_fmt(
            lora_config_label, "%s  %s  %s  CR %s  %s",
            lora_frequency_values[lora_saved_parameters.frequency],
            lora_bw_values[lora_saved_parameters.bw],
            lora_sf_values[lora_saved_parameters.sf],
            lora_cr_values[lora_saved_parameters.cr],
            lora_power_values[lora_saved_parameters.power]);
    }
    if (lora_timed_label != RT_NULL && !lora_timed_enabled)
    {
        lv_label_set_text_fmt(
            lora_timed_label, LV_SYMBOL_REFRESH "  Timed %s",
            lora_interval_values[lora_saved_parameters.interval]);
    }
}

static void lora_page_hide_keyboard(void)
{
    if (lora_keyboard == RT_NULL)
        return;

    lv_obj_add_flag(lora_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(lora_message_input, LORA_SIDE_PADDING, 304);
    lv_obj_set_size(lora_message_input, 340, 56);
    lv_obj_remove_flag(lora_config_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(lora_log_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(lora_send_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(lora_timed_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(lora_listen_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(lora_status_label, LV_OBJ_FLAG_HIDDEN);
    if (lora_page_owner != RT_NULL && lora_page_owner->group != RT_NULL)
    {
        lv_group_set_editing(lora_page_owner->group, false);
        lora_page_add_main_group_objects(lora_page_owner);
    }
}

static void lora_page_show_keyboard(void)
{
    if (lora_keyboard == RT_NULL && lora_page_owner != RT_NULL)
        lora_page_create_keyboard(lora_page_owner);
    if (lora_keyboard == RT_NULL)
        return;

    lv_obj_add_flag(lora_config_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(lora_log_panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(lora_send_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(lora_timed_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(lora_listen_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(lora_status_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(lora_message_input, LORA_SIDE_PADDING, 78);
    lv_obj_set_size(lora_message_input, LORA_CONTENT_WIDTH, 42);
    lv_obj_remove_flag(lora_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(lora_keyboard);
    if (lora_page_owner != RT_NULL && lora_page_owner->group != RT_NULL)
    {
        lv_group_remove_all_objs(lora_page_owner->group);
        lv_group_add_obj(lora_page_owner->group, lora_keyboard);
        lv_group_focus_obj(lora_keyboard);
        lv_group_set_editing(lora_page_owner->group, true);
    }
}

static void lora_page_keyboard_event(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);

    if (code == LV_EVENT_READY)
    {
        lora_page_hide_keyboard();
        if (lora_page_owner != RT_NULL &&
            lora_page_owner->group != RT_NULL &&
            lora_send_button != RT_NULL)
        {
            lv_group_focus_obj(lora_send_button);
        }
    }
    else if (code == LV_EVENT_CANCEL)
    {
        lora_page_hide_keyboard();
    }
}

static void lora_page_input_clicked(lv_event_t *event)
{
    (void)event;
    lora_page_show_keyboard();
}

static void lora_page_input_value_changed(lv_event_t *event)
{
    lv_obj_t *input = lv_event_get_target_obj(event);

    /* LVGL keeps the cursor visible by scrolling both axes internally. */
    lv_obj_scroll_to_y(input, 0, LV_ANIM_OFF);
}

static void lora_page_add_main_group_objects(ui_page_t *page)
{
    if (page == RT_NULL || page->group == RT_NULL)
        return;

    lv_group_remove_all_objs(page->group);
    lv_group_add_obj(page->group, lora_main_back);
    lv_group_add_obj(page->group, lora_settings_button);
    lv_group_add_obj(page->group, lora_message_input);
    lv_group_add_obj(page->group, lora_send_button);
    lv_group_add_obj(page->group, lora_timed_button);
    lv_group_add_obj(page->group, lora_listen_button);
    lv_group_focus_obj(lora_message_input);
}

static void lora_page_add_settings_group_objects(ui_page_t *page)
{
    uint32_t index;

    if (page == RT_NULL || page->group == RT_NULL)
        return;

    lv_group_remove_all_objs(page->group);
    lv_group_add_obj(page->group, lora_settings_back);
    for (index = 0U; index < LORA_PARAM_COUNT; index++)
        lv_group_add_obj(page->group, lora_parameter_controls[index]);
    lv_group_add_obj(page->group, lora_save_button);
    lv_group_focus_obj(lora_parameter_controls[LORA_PARAM_FREQUENCY]);
}

static void lora_page_add_option_group_objects(ui_page_t *page)
{
    uint8_t index;
    uint8_t selected;

    if (page == RT_NULL || page->group == RT_NULL)
        return;

    lv_group_remove_all_objs(page->group);
    lv_group_add_obj(page->group, lora_option_back);
    for (index = 0U; index < lora_active_option_count; index++)
        lv_group_add_obj(page->group, lora_option_buttons[index]);
    selected = lora_page_get_parameter_index(&lora_edit_parameters,
                                              lora_active_parameter);
    if (selected >= lora_active_option_count)
        selected = 0U;
    lv_group_focus_obj(lora_option_buttons[selected]);
    lv_obj_scroll_to_view_recursive(lora_option_buttons[selected],
                                    LV_ANIM_OFF);
}

static void lora_page_show_main(ui_page_t *page)
{
    lora_page_hide_keyboard();
    if (lora_option_view != RT_NULL)
        lv_obj_add_flag(lora_option_view, LV_OBJ_FLAG_HIDDEN);
    if (lora_settings_view != RT_NULL)
        lv_obj_add_flag(lora_settings_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(lora_main_view, LV_OBJ_FLAG_HIDDEN);
    lora_page_update_parameter_labels();
    lora_page_add_main_group_objects(page);
}

static void lora_page_show_settings(ui_page_t *page)
{
    uint8_t index;
    uint8_t count;
    const char *const *values;

    if (lora_settings_view == RT_NULL)
        lora_page_create_settings_view(page);
    if (lora_settings_view == RT_NULL || lora_option_view == RT_NULL)
        return;

    lora_edit_parameters = lora_saved_parameters;
    for (index = 0U; index < LORA_PARAM_COUNT; index++)
    {
        values = lora_page_get_parameter_values((lora_param_id_t)index,
                                                &count);
        lv_label_set_text(
            lora_parameter_value_labels[index],
            values[lora_page_get_parameter_index(&lora_edit_parameters,
                                                  (lora_param_id_t)index)]);
    }

    lora_page_hide_keyboard();
    lv_obj_add_flag(lora_main_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(lora_option_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(lora_settings_view, LV_OBJ_FLAG_HIDDEN);
    lora_page_add_settings_group_objects(page);
}

static void lora_page_back_clicked(lv_event_t *event)
{
    (void)event;
    ui_navigation_switch_async(UI_PAGE_HOME);
}

static void lora_page_settings_clicked(lv_event_t *event)
{
    lora_work_t work;

    (void)event;
    if (lora_timed_enabled)
    {
        memset(&work, 0, sizeof(work));
        work.type = LORA_WORK_SET_PERIODIC;
        (void)lora_page_queue_work(&work);
        lora_timed_enabled = false;
        lv_obj_remove_state(lora_timed_button, LV_STATE_CHECKED);
        lv_label_set_text(lora_timed_label,
                          LV_SYMBOL_REFRESH "  Timed send");
    }
    if (lora_listen_enabled)
    {
        memset(&work, 0, sizeof(work));
        work.type = LORA_WORK_SET_LISTEN;
        (void)lora_page_queue_work(&work);
        lora_listen_enabled = false;
        lv_obj_remove_state(lora_listen_button, LV_STATE_CHECKED);
        lv_label_set_text(lora_listen_label,
                          LV_SYMBOL_EYE_OPEN "  Listen");
    }
    lora_page_show_settings(lora_page_owner);
}

static void lora_page_settings_back_clicked(lv_event_t *event)
{
    (void)event;
    lora_page_show_main(lora_page_owner);
}

static void lora_page_send_clicked(lv_event_t *event)
{
    const char *message;
    lora_work_t work;
    rt_err_t result;

    (void)event;
    if (!lora_service_is_ready())
    {
        lv_label_set_text(lora_status_label, "SX1262 is not available");
        lv_obj_set_style_text_color(lora_status_label,
                                    lv_color_hex(UI_COLOR_ERROR), 0);
        return;
    }
    message = lv_textarea_get_text(lora_message_input);
    if (message == RT_NULL || message[0] == '\0')
    {
        lv_label_set_text(lora_status_label, "Enter a message to send");
        lv_obj_set_style_text_color(lora_status_label,
                                    lv_color_hex(UI_COLOR_WARNING), 0);
        return;
    }

    memset(&work, 0, sizeof(work));
    work.type = LORA_WORK_SEND;
    rt_strncpy(work.payload, message, sizeof(work.payload) - 1U);
    result = lora_page_queue_work(&work);
    if (result != RT_EOK)
    {
        lv_label_set_text(lora_status_label, "Radio queue is busy");
        lv_obj_set_style_text_color(lora_status_label,
                                    lv_color_hex(UI_COLOR_WARNING), 0);
        return;
    }

    lv_label_set_text_fmt(lora_tx_message_label, "TX QUEUED   %s", message);
    lv_textarea_set_text(lora_message_input, "");
    lv_label_set_text(lora_status_label, "Sending LoRa packet...");
    lv_obj_set_style_text_color(lora_status_label,
                                lv_color_hex(UI_COLOR_SECONDARY), 0);
}

static void lora_page_timed_clicked(lv_event_t *event)
{
    lv_obj_t *button = lv_event_get_target_obj(event);
    const char *message;
    lora_work_t work;
    rt_err_t result;

    lora_timed_enabled = lv_obj_has_state(button, LV_STATE_CHECKED);
    if (!lora_service_is_ready())
    {
        lora_timed_enabled = false;
        lv_obj_remove_state(button, LV_STATE_CHECKED);
        lv_label_set_text(lora_status_label, "SX1262 is not available");
        lv_obj_set_style_text_color(lora_status_label,
                                    lv_color_hex(UI_COLOR_ERROR), 0);
        return;
    }

    if (lora_timed_enabled && lora_listen_enabled)
    {
        memset(&work, 0, sizeof(work));
        work.type = LORA_WORK_SET_LISTEN;
        result = lora_page_queue_work(&work);
        if (result != RT_EOK)
        {
            lora_timed_enabled = false;
            lv_obj_remove_state(button, LV_STATE_CHECKED);
            lv_label_set_text(lora_status_label, "Radio queue is busy");
            lv_obj_set_style_text_color(lora_status_label,
                                        lv_color_hex(UI_COLOR_WARNING), 0);
            return;
        }
        lora_listen_enabled = false;
        lv_obj_remove_state(lora_listen_button, LV_STATE_CHECKED);
        lv_label_set_text(lora_listen_label,
                          LV_SYMBOL_EYE_OPEN "  Listen");
    }

    memset(&work, 0, sizeof(work));
    work.type = LORA_WORK_SET_PERIODIC;
    work.enabled = lora_timed_enabled;
    work.interval_ms =
        lora_interval_ms_values[lora_saved_parameters.interval];
    message = lv_textarea_get_text(lora_message_input);
    if (message != RT_NULL)
        rt_strncpy(work.payload, message, sizeof(work.payload) - 1U);
    result = lora_page_queue_work(&work);
    if (result != RT_EOK)
    {
        lora_timed_enabled = false;
        lv_obj_remove_state(button, LV_STATE_CHECKED);
        lv_label_set_text(lora_status_label, "Radio queue is busy");
        lv_obj_set_style_text_color(lora_status_label,
                                    lv_color_hex(UI_COLOR_WARNING), 0);
        return;
    }
    lv_label_set_text(
        lora_timed_label,
        lora_timed_enabled ? LV_SYMBOL_STOP "  Timed send on" : LV_SYMBOL_REFRESH "  Timed send");
    lv_label_set_text_fmt(
        lora_status_label,
        lora_timed_enabled ? "Timed send active: every %s" : "Timed send stopped",
        lora_interval_values[lora_saved_parameters.interval]);
    lv_obj_set_style_text_color(
        lora_status_label,
        lv_color_hex(lora_timed_enabled ? UI_COLOR_SUCCESS : UI_COLOR_MUTED), 0);
}

static void lora_page_listen_clicked(lv_event_t *event)
{
    lv_obj_t *button = lv_event_get_target_obj(event);
    lora_work_t work;
    rt_err_t result;

    lora_listen_enabled = lv_obj_has_state(button, LV_STATE_CHECKED);
    if (!lora_service_is_ready())
    {
        lora_listen_enabled = false;
        lv_obj_remove_state(button, LV_STATE_CHECKED);
        lv_label_set_text(lora_status_label, "SX1262 is not available");
        lv_obj_set_style_text_color(lora_status_label,
                                    lv_color_hex(UI_COLOR_ERROR), 0);
        return;
    }

    if (lora_listen_enabled && lora_timed_enabled)
    {
        memset(&work, 0, sizeof(work));
        work.type = LORA_WORK_SET_PERIODIC;
        result = lora_page_queue_work(&work);
        if (result != RT_EOK)
        {
            lora_listen_enabled = false;
            lv_obj_remove_state(button, LV_STATE_CHECKED);
            lv_label_set_text(lora_status_label, "Radio queue is busy");
            lv_obj_set_style_text_color(lora_status_label,
                                        lv_color_hex(UI_COLOR_WARNING), 0);
            return;
        }
        lora_timed_enabled = false;
        lv_obj_remove_state(lora_timed_button, LV_STATE_CHECKED);
        lv_label_set_text_fmt(
            lora_timed_label, LV_SYMBOL_REFRESH "  Timed %s",
            lora_interval_values[lora_saved_parameters.interval]);
    }

    memset(&work, 0, sizeof(work));
    work.type = LORA_WORK_SET_LISTEN;
    work.enabled = lora_listen_enabled;
    result = lora_page_queue_work(&work);
    if (result != RT_EOK)
    {
        lora_listen_enabled = false;
        lv_obj_remove_state(button, LV_STATE_CHECKED);
        lv_label_set_text(lora_status_label, "Radio queue is busy");
        lv_obj_set_style_text_color(lora_status_label,
                                    lv_color_hex(UI_COLOR_WARNING), 0);
        return;
    }

    lv_label_set_text(
        lora_listen_label,
        lora_listen_enabled ? LV_SYMBOL_STOP "  Listening" :
                              LV_SYMBOL_EYE_OPEN "  Listen");
    lv_label_set_text(lora_status_label,
                      lora_listen_enabled ? "Listening for LoRa packets..." :
                                            "Receive stopped - radio idle");
    lv_obj_set_style_text_color(
        lora_status_label,
        lv_color_hex(lora_listen_enabled ? UI_COLOR_SECONDARY :
                                           UI_COLOR_MUTED), 0);
}

static void lora_page_save_clicked(lv_event_t *event)
{
    lora_ui_parameters_t selected;
    lora_page_snapshot_t snapshot;
    lora_work_t work;
    rt_err_t result;

    (void)event;
    selected = lora_edit_parameters;

    if (!lora_service_is_ready())
    {
        lora_page_show_main(lora_page_owner);
        lv_label_set_text(lora_status_label, "SX1262 is not available");
        lv_obj_set_style_text_color(lora_status_label,
                                    lv_color_hex(UI_COLOR_ERROR), 0);
        return;
    }

    memset(&snapshot, 0, sizeof(snapshot));
    if (lora_snapshot_lock_initialized &&
        rt_mutex_take(&lora_snapshot_lock,
                      RT_WAITING_FOREVER) == RT_EOK)
    {
        snapshot = lora_snapshot;
        rt_mutex_release(&lora_snapshot_lock);
    }
    memset(&work, 0, sizeof(work));
    work.type = LORA_WORK_CONFIGURE;
    if (snapshot.config.frequency_hz != 0U)
        work.config = snapshot.config;
    else
        lora_service_config_default(&work.config);
    work.config.frequency_hz = lora_frequency_hz_values[selected.frequency];
    work.config.bandwidth = (lora_service_bandwidth_t)selected.bw;
    work.config.spreading_factor = (uint8_t)(selected.sf + 5U);
    work.config.coding_rate = (uint8_t)(selected.cr + 5U);
    work.config.output_power_dbm = lora_power_dbm_values[selected.power];
    result = lora_page_queue_work(&work);
    if (result == RT_EOK)
        lora_saved_parameters = selected;

    lora_page_show_main(lora_page_owner);
    lv_label_set_text(lora_status_label, result == RT_EOK ? "Applying radio parameters..." : "Radio queue is busy");
    lv_obj_set_style_text_color(lora_status_label,
                                lv_color_hex(result == RT_EOK ? UI_COLOR_SECONDARY : UI_COLOR_WARNING), 0);
}

static void lora_page_create_main_header(lv_obj_t *parent)
{
    lv_obj_t *title;

    lora_main_back = lora_page_create_header_button(
        parent, 12, LV_SYMBOL_LEFT, lora_page_back_clicked);
    title = lora_page_create_label(parent, "LORA",
                                   &lv_font_montserrat_24,
                                   UI_COLOR_TEXT);
    lv_obj_set_size(title, 250, 34);
    lv_obj_set_pos(title, 72, LORA_HEADER_Y + 6);
    lora_settings_button = lora_page_create_header_button(
        parent, UI_SCREEN_WIDTH - 60, LV_SYMBOL_SETTINGS,
        lora_page_settings_clicked);
}

static void lora_page_create_config_panel(lv_obj_t *parent)
{
    lv_obj_t *icon;
    lv_obj_t *section;

    lora_config_panel = lv_obj_create(parent);
    lv_obj_set_size(lora_config_panel, LORA_CONTENT_WIDTH, 54);
    lv_obj_set_pos(lora_config_panel, LORA_SIDE_PADDING, LORA_CONTENT_Y);
    lora_page_style_container(lora_config_panel, UI_COLOR_PANEL,
                              LV_OPA_COVER);
    lv_obj_remove_flag(lora_config_panel, LV_OBJ_FLAG_SCROLLABLE);

    icon = lv_image_create(lora_config_panel);
    lv_image_set_src(icon, &lora_icon);
    lv_image_set_scale(icon, 128);
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 0);

    section = lora_page_create_label(lora_config_panel, "SX1262  CHECKING",
                                     &lv_font_montserrat_12,
                                     UI_COLOR_WARNING);
    lv_obj_set_pos(section, 64, 7);
    lora_radio_state_label = section;
    lora_config_label = lora_page_create_label(
        lora_config_panel, "", &lv_font_montserrat_14, UI_COLOR_TEXT);
    lv_obj_set_size(lora_config_label, 362, 22);
    lv_obj_set_pos(lora_config_label, 64, 27);
}

static void lora_page_create_log_panel(lv_obj_t *parent)
{
    lv_obj_t *title;
    lv_obj_t *separator;

    lora_log_panel = lv_obj_create(parent);
    lv_obj_set_size(lora_log_panel, LORA_CONTENT_WIDTH, 136);
    lv_obj_set_pos(lora_log_panel, LORA_SIDE_PADDING, 160);
    lora_page_style_container(lora_log_panel, UI_COLOR_PANEL,
                              LV_OPA_COVER);
    lv_obj_remove_flag(lora_log_panel, LV_OBJ_FLAG_SCROLLABLE);

    title = lora_page_create_label(lora_log_panel, "LINK MONITOR",
                                   &lv_font_montserrat_12,
                                   UI_COLOR_MUTED);
    lv_obj_set_pos(title, 14, 10);

    lora_rx_meta_label = lora_page_create_label(
        lora_log_panel, "RX   Waiting for packet",
        &lv_font_montserrat_12, UI_COLOR_SECONDARY);
    lv_obj_set_pos(lora_rx_meta_label, 14, 34);
    lora_rx_message_label = lora_page_create_label(
        lora_log_panel, "--", &lv_font_montserrat_16, UI_COLOR_TEXT);
    lv_obj_set_size(lora_rx_message_label, 420, 24);
    lv_label_set_long_mode(lora_rx_message_label, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(lora_rx_message_label, 14, 54);

    separator = lv_obj_create(lora_log_panel);
    lv_obj_set_size(separator, 420, 1);
    lv_obj_set_pos(separator, 14, 84);
    lv_obj_set_style_bg_color(separator, lv_color_hex(UI_COLOR_TRACK), 0);
    lv_obj_set_style_bg_opa(separator, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(separator, 0, 0);
    lv_obj_set_style_pad_all(separator, 0, 0);

    lora_tx_message_label = lora_page_create_label(
        lora_log_panel, "TX   Waiting for message",
        &lv_font_montserrat_14, UI_COLOR_MUTED);
    lv_obj_set_size(lora_tx_message_label, 420, 24);
    lv_label_set_long_mode(lora_tx_message_label, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(lora_tx_message_label, 14, 101);
}

static void lora_page_create_composer(lv_obj_t *parent)
{
    lv_obj_t *send_label;

    lora_message_input = lv_textarea_create(parent);
    lv_textarea_set_one_line(lora_message_input, true);
    lv_obj_set_size(lora_message_input, 340, 56);
    lv_obj_set_pos(lora_message_input, LORA_SIDE_PADDING, 304);
    lv_textarea_set_max_length(lora_message_input, 96U);
    lv_textarea_set_placeholder_text(lora_message_input,
                                     "Type LoRa message...");
    lv_obj_set_style_bg_color(lora_message_input,
                              lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(lora_message_input, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(lora_message_input,
                                  lv_color_hex(UI_COLOR_TRACK), 0);
    lv_obj_set_style_border_width(lora_message_input, 1, 0);
    lv_obj_set_style_radius(lora_message_input, 4, 0);
    lv_obj_set_style_text_color(lora_message_input,
                                lv_color_hex(UI_COLOR_TEXT), 0);
    lv_obj_set_style_text_font(lora_message_input,
                               &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_opa(lora_message_input, LV_OPA_COVER, 0);
    lv_obj_set_style_text_letter_space(lora_message_input, 0, 0);
    lv_obj_set_style_pad_top(lora_message_input, 15, 0);
    lv_obj_set_style_pad_bottom(lora_message_input, 15, 0);
    lv_obj_set_scrollbar_mode(lora_message_input, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(lora_message_input, LV_DIR_HOR);
    lv_obj_remove_flag(lora_message_input, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_remove_flag(lora_message_input, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    ui_theme_apply_focus_style(lora_message_input);
    lv_obj_add_event_cb(lora_message_input, lora_page_input_clicked,
                        LV_EVENT_CLICKED, RT_NULL);
    lv_obj_add_event_cb(lora_message_input,
                        lora_page_input_value_changed,
                        LV_EVENT_VALUE_CHANGED, RT_NULL);

    lora_send_button = lv_button_create(parent);
    lv_obj_set_size(lora_send_button, 96, 56);
    lv_obj_set_pos(lora_send_button, 368, 304);
    lora_page_style_button(lora_send_button, UI_COLOR_PRIMARY);
    lv_obj_add_event_cb(lora_send_button, lora_page_send_clicked,
                        LV_EVENT_CLICKED, RT_NULL);
    send_label = lora_page_create_label(lora_send_button,
                                        LV_SYMBOL_UPLOAD "  Send",
                                        &lv_font_montserrat_16,
                                        UI_COLOR_BACKGROUND);
    lv_obj_center(send_label);

    lora_timed_button = lv_button_create(parent);
    lv_obj_set_size(lora_timed_button, 216, 52);
    lv_obj_set_pos(lora_timed_button, LORA_SIDE_PADDING, 372);
    lora_page_style_button(lora_timed_button, UI_COLOR_PANEL);
    lv_obj_add_flag(lora_timed_button, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_style_bg_color(lora_timed_button,
                              lv_color_hex(UI_COLOR_SUCCESS),
                              LV_STATE_CHECKED);
    lv_obj_add_event_cb(lora_timed_button, lora_page_timed_clicked,
                        LV_EVENT_CLICKED, RT_NULL);
    lora_timed_label = lora_page_create_label(
        lora_timed_button, LV_SYMBOL_REFRESH "  Timed 1 s",
        &lv_font_montserrat_16, UI_COLOR_TEXT);
    lv_obj_center(lora_timed_label);

    lora_listen_button = lv_button_create(parent);
    lv_obj_set_size(lora_listen_button, 212, 52);
    lv_obj_set_pos(lora_listen_button, 252, 372);
    lora_page_style_button(lora_listen_button, UI_COLOR_PANEL);
    lv_obj_add_flag(lora_listen_button, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_style_bg_color(lora_listen_button,
                              lv_color_hex(UI_COLOR_SECONDARY),
                              LV_STATE_CHECKED);
    lv_obj_add_event_cb(lora_listen_button, lora_page_listen_clicked,
                        LV_EVENT_CLICKED, RT_NULL);
    lora_listen_label = lora_page_create_label(
        lora_listen_button, LV_SYMBOL_EYE_OPEN "  Listen",
        &lv_font_montserrat_16, UI_COLOR_TEXT);
    lv_obj_center(lora_listen_label);

    lora_status_label = lora_page_create_label(
        parent, "Radio idle",
        &lv_font_montserrat_12, UI_COLOR_MUTED);
    lv_obj_set_size(lora_status_label, LORA_CONTENT_WIDTH, 20);
    lv_obj_set_pos(lora_status_label, LORA_SIDE_PADDING, 440);
}

static void lora_page_create_keyboard(ui_page_t *page)
{
    if (page == RT_NULL || page->root == RT_NULL ||
        lora_message_input == RT_NULL || lora_keyboard != RT_NULL)
    {
        return;
    }

    lora_keyboard = lv_keyboard_create(page->root);
    lv_obj_set_size(lora_keyboard, UI_SCREEN_WIDTH,
                    LORA_KEYBOARD_HEIGHT);
    lv_obj_set_pos(lora_keyboard, 0, LORA_KEYBOARD_Y);
    lv_obj_set_style_bg_color(lora_keyboard,
                              lv_color_hex(UI_COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(lora_keyboard, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(lora_keyboard, 4, 0);
    lv_obj_set_style_pad_row(lora_keyboard, 4, 0);
    lv_obj_set_style_pad_column(lora_keyboard, 3, 0);
    lv_obj_set_style_bg_color(lora_keyboard,
                              lv_color_hex(UI_COLOR_PANEL),
                              LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(lora_keyboard, LV_OPA_COVER,
                            LV_PART_ITEMS);
    lv_obj_set_style_border_color(lora_keyboard,
                                  lv_color_hex(UI_COLOR_TRACK),
                                  LV_PART_ITEMS);
    lv_obj_set_style_border_width(lora_keyboard, 1,
                                  LV_PART_ITEMS);
    lv_obj_set_style_radius(lora_keyboard, 3, LV_PART_ITEMS);
    lv_obj_set_style_text_color(lora_keyboard,
                                lv_color_hex(UI_COLOR_TEXT),
                                LV_PART_ITEMS);
    lv_obj_set_style_text_font(lora_keyboard,
                               &lv_font_montserrat_16,
                               LV_PART_ITEMS);
    lv_obj_set_style_bg_color(lora_keyboard,
                              lv_color_hex(UI_COLOR_PRIMARY),
                              LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(lora_keyboard,
                                lv_color_hex(UI_COLOR_BACKGROUND),
                                LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(lora_keyboard,
                              lv_color_hex(UI_COLOR_SECONDARY),
                              LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(lora_keyboard,
                                lv_color_hex(UI_COLOR_BACKGROUND),
                                LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_keyboard_set_mode(lora_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(lora_keyboard, lora_message_input);
    lv_obj_add_event_cb(lora_keyboard, lora_page_keyboard_event,
                        LV_EVENT_ALL, RT_NULL);
    lv_obj_add_flag(lora_keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void lora_page_create_main_view(ui_page_t *page)
{
    lora_main_view = lv_obj_create(page->root);
    lv_obj_set_size(lora_main_view, UI_SCREEN_WIDTH, UI_SCREEN_HEIGHT);
    lora_page_style_container(lora_main_view, UI_COLOR_BACKGROUND,
                              LV_OPA_COVER);
    lv_obj_remove_flag(lora_main_view, LV_OBJ_FLAG_SCROLLABLE);

    lora_page_create_main_header(lora_main_view);
    lora_page_create_config_panel(lora_main_view);
    lora_page_create_log_panel(lora_main_view);
    lora_page_create_composer(lora_main_view);
    lora_page_update_parameter_labels();
}

static void lora_page_hide_option_view(void)
{
    if (lora_option_view == RT_NULL)
        return;

    lv_obj_add_flag(lora_option_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(lora_settings_view, LV_OBJ_FLAG_HIDDEN);
    lora_page_add_settings_group_objects(lora_page_owner);
    if (lora_page_owner != RT_NULL && lora_page_owner->group != RT_NULL)
        lv_group_focus_obj(lora_parameter_controls[lora_active_parameter]);
}

static void lora_page_option_back_clicked(lv_event_t *event)
{
    (void)event;
    lora_page_hide_option_view();
}

static void lora_page_option_clicked(lv_event_t *event)
{
    uint8_t index = (uint8_t)(uintptr_t)lv_event_get_user_data(event);
    uint8_t count;
    const char *const *values;

    if (index >= lora_active_option_count)
        return;
    lora_page_set_parameter_index(&lora_edit_parameters,
                                  lora_active_parameter, index);
    values = lora_page_get_parameter_values(lora_active_parameter, &count);
    lv_label_set_text(lora_parameter_value_labels[lora_active_parameter],
                      values[index]);
    lora_page_hide_option_view();
}

static void lora_page_parameter_clicked(lv_event_t *event)
{
    lora_param_id_t parameter =
        (lora_param_id_t)(uintptr_t)lv_event_get_user_data(event);
    const char *const *values;
    uint8_t selected;
    uint8_t index;

    if (parameter >= LORA_PARAM_COUNT)
        return;
    lora_active_parameter = parameter;
    values = lora_page_get_parameter_values(parameter,
                                            &lora_active_option_count);
    selected = lora_page_get_parameter_index(&lora_edit_parameters,
                                              parameter);
    lv_label_set_text(lora_option_title, lora_parameter_titles[parameter]);
    lv_obj_scroll_to_y(lora_option_body, 0, LV_ANIM_OFF);
    for (index = 0U; index < LORA_OPTION_MAX_COUNT; index++)
    {
        if (index < lora_active_option_count)
        {
            lv_label_set_text(lora_option_labels[index], values[index]);
            lv_obj_set_style_bg_color(
                lora_option_buttons[index],
                lv_color_hex(index == selected ? UI_COLOR_SECONDARY :
                                                   UI_COLOR_PANEL),
                0);
            lv_obj_set_style_text_color(
                lora_option_labels[index],
                lv_color_hex(index == selected ? UI_COLOR_BACKGROUND :
                                                   UI_COLOR_TEXT),
                0);
            lv_obj_remove_flag(lora_option_buttons[index],
                               LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(lora_option_buttons[index],
                            LV_OBJ_FLAG_HIDDEN);
        }
    }
    lv_obj_add_flag(lora_settings_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(lora_option_view, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(lora_option_view);
    lora_page_add_option_group_objects(lora_page_owner);
}

static lv_obj_t *lora_page_create_parameter_control(
    lv_obj_t *parent, lv_coord_t y, const char *label_text,
    lora_param_id_t parameter)
{
    lv_obj_t *label;
    lv_obj_t *control;
    lv_obj_t *arrow;
    lv_obj_t *separator;

    label = lora_page_create_label(parent, label_text,
                                   &lv_font_montserrat_16,
                                   UI_COLOR_TEXT);
    lv_obj_set_size(label, 190, 24);
    lv_obj_set_pos(label, 18, y + 17);

    control = lv_button_create(parent);
    lv_obj_set_size(control, 210, 44);
    lv_obj_set_pos(control, 220, y + 7);
    lora_page_style_button(control, UI_COLOR_PANEL);
    lv_obj_set_style_border_color(control,
                                  lv_color_hex(UI_COLOR_TRACK), 0);
    lv_obj_set_style_border_width(control, 1, 0);
    lv_obj_add_event_cb(control, lora_page_parameter_clicked,
                        LV_EVENT_CLICKED, (void *)(uintptr_t)parameter);
    lora_parameter_value_labels[parameter] = lora_page_create_label(
        control, "", &lv_font_montserrat_16, UI_COLOR_TEXT);
    lv_obj_set_size(lora_parameter_value_labels[parameter], 155, 24);
    lv_obj_set_pos(lora_parameter_value_labels[parameter], 14, 11);
    arrow = lora_page_create_label(control, LV_SYMBOL_DOWN,
                                   &lv_font_montserrat_16,
                                   UI_COLOR_MUTED);
    lv_obj_set_size(arrow, 24, 24);
    lv_obj_set_pos(arrow, 174, 11);
    lv_obj_set_style_text_align(arrow, LV_TEXT_ALIGN_CENTER, 0);

    separator = lv_obj_create(parent);
    lv_obj_set_size(separator, 412, 1);
    lv_obj_set_pos(separator, 18, y + LORA_SETTING_ROW_HEIGHT - 1);
    lv_obj_set_style_bg_color(separator, lv_color_hex(UI_COLOR_TRACK), 0);
    lv_obj_set_style_bg_opa(separator, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(separator, 0, 0);
    lv_obj_set_style_pad_all(separator, 0, 0);
    return control;
}

static void lora_page_create_option_view(ui_page_t *page)
{
    uint8_t index;

    lora_option_view = lv_obj_create(page->root);
    lv_obj_set_size(lora_option_view, UI_SCREEN_WIDTH, UI_SCREEN_HEIGHT);
    lora_page_style_container(lora_option_view, UI_COLOR_BACKGROUND,
                              LV_OPA_COVER);
    lv_obj_remove_flag(lora_option_view, LV_OBJ_FLAG_SCROLLABLE);
    lora_option_back = lora_page_create_header_button(
        lora_option_view, 12, LV_SYMBOL_LEFT,
        lora_page_option_back_clicked);
    lora_option_title = lora_page_create_label(
        lora_option_view, "", &lv_font_montserrat_24, UI_COLOR_TEXT);
    lv_obj_set_size(lora_option_title, 360, 34);
    lv_obj_set_pos(lora_option_title, 72, LORA_HEADER_Y + 6);

    lora_option_body = lv_obj_create(lora_option_view);
    lv_obj_set_size(lora_option_body, LORA_CONTENT_WIDTH,
                    UI_SCREEN_HEIGHT - LORA_CONTENT_Y - 8);
    lv_obj_set_pos(lora_option_body, LORA_SIDE_PADDING, LORA_CONTENT_Y);
    lora_page_style_container(lora_option_body, UI_COLOR_BACKGROUND,
                              LV_OPA_COVER);
    lv_obj_set_scroll_dir(lora_option_body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(lora_option_body, LV_SCROLLBAR_MODE_ACTIVE);
    for (index = 0U; index < LORA_OPTION_MAX_COUNT; index++)
    {
        lora_option_buttons[index] = lv_button_create(lora_option_body);
        lv_obj_set_size(lora_option_buttons[index],
                        LORA_CONTENT_WIDTH - 8,
                        LORA_OPTION_ROW_HEIGHT - 4);
        lv_obj_set_pos(lora_option_buttons[index], 4,
                       index * LORA_OPTION_ROW_HEIGHT);
        lora_page_style_button(lora_option_buttons[index], UI_COLOR_PANEL);
        lv_obj_set_style_border_color(lora_option_buttons[index],
                                      lv_color_hex(UI_COLOR_TRACK), 0);
        lv_obj_set_style_border_width(lora_option_buttons[index], 1, 0);
        lv_obj_add_flag(lora_option_buttons[index],
                        LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        lv_obj_add_event_cb(lora_option_buttons[index],
                            lora_page_option_clicked, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)index);
        lora_option_labels[index] = lora_page_create_label(
            lora_option_buttons[index], "", &lv_font_montserrat_16,
            UI_COLOR_TEXT);
        lv_obj_center(lora_option_labels[index]);
    }
    lv_obj_add_flag(lora_option_view, LV_OBJ_FLAG_HIDDEN);
}

static void lora_page_create_settings_view(ui_page_t *page)
{
    lv_obj_t *title;
    lv_obj_t *body;
    lv_obj_t *save_label;

    lora_settings_view = lv_obj_create(page->root);
    lv_obj_set_size(lora_settings_view, UI_SCREEN_WIDTH, UI_SCREEN_HEIGHT);
    lora_page_style_container(lora_settings_view, UI_COLOR_BACKGROUND,
                              LV_OPA_COVER);
    lv_obj_remove_flag(lora_settings_view, LV_OBJ_FLAG_SCROLLABLE);

    lora_settings_back = lora_page_create_header_button(
        lora_settings_view, 12, LV_SYMBOL_LEFT,
        lora_page_settings_back_clicked);
    title = lora_page_create_label(lora_settings_view, "LORA SETTINGS",
                                   &lv_font_montserrat_24,
                                   UI_COLOR_TEXT);
    lv_obj_set_size(title, 300, 34);
    lv_obj_set_pos(title, 72, LORA_HEADER_Y + 6);

    body = lv_obj_create(lora_settings_view);
    lv_obj_set_size(body, LORA_CONTENT_WIDTH, 304);
    lv_obj_set_pos(body, LORA_SIDE_PADDING, LORA_CONTENT_Y);
    lora_page_style_container(body, UI_COLOR_BACKGROUND, LV_OPA_COVER);
    lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    lora_parameter_controls[LORA_PARAM_FREQUENCY] =
        lora_page_create_parameter_control(
            body, 0, "Frequency", LORA_PARAM_FREQUENCY);
    lora_parameter_controls[LORA_PARAM_BW] =
        lora_page_create_parameter_control(
            body, LORA_SETTING_ROW_HEIGHT, "Bandwidth", LORA_PARAM_BW);
    lora_parameter_controls[LORA_PARAM_SF] =
        lora_page_create_parameter_control(
            body, LORA_SETTING_ROW_HEIGHT * 2, "Spreading factor",
            LORA_PARAM_SF);
    lora_parameter_controls[LORA_PARAM_CR] =
        lora_page_create_parameter_control(
            body, LORA_SETTING_ROW_HEIGHT * 3, "Coding rate",
            LORA_PARAM_CR);
    lora_parameter_controls[LORA_PARAM_POWER] =
        lora_page_create_parameter_control(
            body, LORA_SETTING_ROW_HEIGHT * 4, "Transmit power",
            LORA_PARAM_POWER);
    lora_parameter_controls[LORA_PARAM_INTERVAL] =
        lora_page_create_parameter_control(
            body, LORA_SETTING_ROW_HEIGHT * 5, "Send interval",
            LORA_PARAM_INTERVAL);

    lora_save_button = lv_button_create(lora_settings_view);
    lv_obj_set_size(lora_save_button, LORA_CONTENT_WIDTH, 50);
    lv_obj_set_pos(lora_save_button, LORA_SIDE_PADDING, 414);
    lora_page_style_button(lora_save_button, UI_COLOR_PRIMARY);
    lv_obj_add_event_cb(lora_save_button, lora_page_save_clicked,
                        LV_EVENT_CLICKED, RT_NULL);
    save_label = lora_page_create_label(
        lora_save_button, LV_SYMBOL_SAVE "  Save parameters",
        &lv_font_montserrat_18, UI_COLOR_BACKGROUND);
    lv_obj_center(save_label);

    lv_obj_add_flag(lora_settings_view, LV_OBJ_FLAG_HIDDEN);
    lora_page_create_option_view(page);
}

static void lora_page_refresh(void)
{
    lora_page_snapshot_t snapshot;
    uint32_t status_color = UI_COLOR_MUTED;

    if (!lora_service_is_ready())
    {
        lv_label_set_text(lora_radio_state_label, "SX1262  UNAVAILABLE");
        lv_obj_set_style_text_color(lora_radio_state_label,
                                    lv_color_hex(UI_COLOR_ERROR), 0);
        lv_label_set_text(lora_status_label, "SX1262 initialization failed");
        lv_obj_set_style_text_color(lora_status_label,
                                    lv_color_hex(UI_COLOR_ERROR), 0);
        return;
    }
    if (!lora_snapshot_lock_initialized ||
        rt_mutex_take(&lora_snapshot_lock,
                      RT_WAITING_FOREVER) != RT_EOK)
        return;
    snapshot = lora_snapshot;
    rt_mutex_release(&lora_snapshot_lock);
    if (snapshot.revision == lora_displayed_revision)
        return;
    lora_displayed_revision = snapshot.revision;

    lv_label_set_text(lora_radio_state_label, "SX1262  READY");
    lv_obj_set_style_text_color(lora_radio_state_label,
                                lv_color_hex(UI_COLOR_SUCCESS), 0);
    if (snapshot.config.frequency_hz != 0U)
    {
        lora_page_parameters_from_config(&snapshot.config);
        lora_page_update_parameter_labels();
    }

    switch (snapshot.result_type)
    {
    case LORA_RESULT_READY:
        lv_label_set_text(lora_status_label, "Radio ready - listening");
        status_color = UI_COLOR_SUCCESS;
        break;
    case LORA_RESULT_TX_OK:
        lv_label_set_text_fmt(lora_tx_message_label, "TX #%lu   %s",
                              (unsigned long)snapshot.tx_count,
                              snapshot.last_tx);
        lv_label_set_text_fmt(lora_status_label,
                              "Packet #%lu sent - listening",
                              (unsigned long)snapshot.tx_count);
        status_color = UI_COLOR_SUCCESS;
        break;
    case LORA_RESULT_TX_FAILED:
        lv_label_set_text_fmt(lora_tx_message_label, "TX FAILED   %s",
                              snapshot.last_tx);
        lv_label_set_text_fmt(lora_status_label,
                              "Transmit failed: %d", snapshot.result);
        status_color = UI_COLOR_ERROR;
        break;
    case LORA_RESULT_RX_OK:
        lv_label_set_text_fmt(
            lora_rx_meta_label, "RX #%lu   RSSI %d dBm   SNR %d dB",
            (unsigned long)snapshot.rx_count, snapshot.rssi_dbm,
            snapshot.snr_db);
        lv_label_set_text(lora_rx_message_label, snapshot.last_rx);
        lv_label_set_text_fmt(lora_status_label,
                              "Packet #%lu received",
                              (unsigned long)snapshot.rx_count);
        status_color = UI_COLOR_SECONDARY;
        break;
    case LORA_RESULT_RX_FAILED:
        lv_label_set_text_fmt(lora_status_label,
                              "Receive failed: %d", snapshot.result);
        status_color = UI_COLOR_ERROR;
        break;
    case LORA_RESULT_CONFIG_OK:
        lv_label_set_text_fmt(lora_status_label,
                              "Radio set to %lu MHz at %d dBm",
                              (unsigned long)(snapshot.config.frequency_hz /
                                              1000000U),
                              snapshot.config.output_power_dbm);
        status_color = UI_COLOR_SUCCESS;
        break;
    case LORA_RESULT_CONFIG_FAILED:
        lv_label_set_text_fmt(lora_status_label,
                              "Parameter apply failed: %d",
                              snapshot.result);
        status_color = UI_COLOR_ERROR;
        break;
    case LORA_RESULT_NONE:
    default:
        lv_label_set_text(lora_status_label, "Radio idle");
        break;
    }
    lv_obj_set_style_text_color(lora_status_label,
                                lv_color_hex(status_color), 0);
}

static void lora_page_on_create(ui_page_t *page)
{
    if (page == RT_NULL || page->root == RT_NULL)
        return;

    lora_page_owner = page;
    lora_timed_enabled = false;
    lora_listen_enabled = false;
    lora_worker_restart_pending = false;
    lora_displayed_revision = UINT32_MAX;
    if (page->group != RT_NULL)
        lv_group_set_wrap(page->group, true);
    lora_page_create_main_view(page);
    lora_page_show_main(page);
}

static rt_err_t lora_page_activate_worker(void)
{
    lora_work_t work;
    rt_err_t result;

    result = lora_page_worker_start();
    if (result != RT_EOK)
        return result;

    memset(&work, 0, sizeof(work));
    work.type = LORA_WORK_SET_ACTIVE;
    work.enabled = true;
    return lora_page_queue_work(&work);
}

static void lora_page_on_enter(ui_page_t *page)
{
    rt_err_t result;

    lora_page_show_main(page);
    lora_displayed_revision = UINT32_MAX;
    if (!lora_service_is_ready())
    {
        lora_page_refresh();
        return;
    }

    lora_worker_restart_pending = true;
    result = lora_page_activate_worker();
    if (result == RT_EOK)
    {
        lora_worker_restart_pending = false;
        return;
    }

    if (result == -RT_EBUSY)
    {
        lv_label_set_text(lora_status_label, "Waiting for radio worker...");
        lv_obj_set_style_text_color(lora_status_label,
                                    lv_color_hex(UI_COLOR_WARNING), 0);
    }
    else
    {
        lora_worker_restart_pending = false;
        lv_label_set_text_fmt(lora_status_label,
                              "Start radio worker failed: %d", result);
        lv_obj_set_style_text_color(lora_status_label,
                                    lv_color_hex(UI_COLOR_ERROR), 0);
    }
}

static void lora_page_on_leave(ui_page_t *page)
{
    (void)page;
    lora_page_hide_keyboard();
    lora_timed_enabled = false;
    lora_listen_enabled = false;
    lora_worker_restart_pending = false;
    (void)lora_page_worker_stop();
}

static void lora_page_on_destroy(ui_page_t *page)
{
    (void)page;
    lora_page_owner = RT_NULL;
    lora_main_view = RT_NULL;
    lora_settings_view = RT_NULL;
    lora_main_back = RT_NULL;
    lora_settings_button = RT_NULL;
    lora_config_panel = RT_NULL;
    lora_log_panel = RT_NULL;
    lora_message_input = RT_NULL;
    lora_send_button = RT_NULL;
    lora_timed_button = RT_NULL;
    lora_timed_label = RT_NULL;
    lora_listen_button = RT_NULL;
    lora_listen_label = RT_NULL;
    lora_status_label = RT_NULL;
    lora_config_label = RT_NULL;
    lora_radio_state_label = RT_NULL;
    lora_rx_meta_label = RT_NULL;
    lora_rx_message_label = RT_NULL;
    lora_tx_message_label = RT_NULL;
    lora_keyboard = RT_NULL;
    lora_settings_back = RT_NULL;
    lora_save_button = RT_NULL;
    lora_option_view = RT_NULL;
    lora_option_back = RT_NULL;
    lora_option_title = RT_NULL;
    lora_option_body = RT_NULL;
    lora_active_option_count = 0U;
    memset(lora_parameter_controls, 0,
           sizeof(lora_parameter_controls));
    memset(lora_parameter_value_labels, 0,
           sizeof(lora_parameter_value_labels));
    memset(lora_option_buttons, 0, sizeof(lora_option_buttons));
    memset(lora_option_labels, 0, sizeof(lora_option_labels));
}

static void lora_page_on_timer(ui_page_t *page)
{
    rt_err_t result;

    (void)page;
    lora_page_refresh();
    if (!lora_worker_restart_pending)
        return;

    result = lora_page_activate_worker();
    if (result == RT_EOK)
    {
        lora_worker_restart_pending = false;
    }
    else if (result != -RT_EBUSY)
    {
        lora_worker_restart_pending = false;
        lv_label_set_text_fmt(lora_status_label,
                              "Start radio worker failed: %d", result);
        lv_obj_set_style_text_color(lora_status_label,
                                    lv_color_hex(UI_COLOR_ERROR), 0);
    }
}

static void lora_page_on_gesture(ui_page_t *page,
                                 ui_gesture_t gesture)
{
    if (gesture != UI_GESTURE_RIGHT)
        return;

    if (lora_keyboard != RT_NULL &&
        !lv_obj_has_flag(lora_keyboard, LV_OBJ_FLAG_HIDDEN))
    {
        lora_page_hide_keyboard();
    }
    else if (lora_option_view != RT_NULL &&
             !lv_obj_has_flag(lora_option_view, LV_OBJ_FLAG_HIDDEN))
    {
        lora_page_hide_option_view();
    }
    else if (lora_settings_view != RT_NULL &&
             !lv_obj_has_flag(lora_settings_view, LV_OBJ_FLAG_HIDDEN))
    {
        lora_page_show_main(page);
    }
    else
    {
        ui_navigation_switch_async(UI_PAGE_HOME);
    }
}

static void lora_page_on_event(ui_page_t *page,
                               const ui_event_t *event)
{
    if (page == RT_NULL || event == RT_NULL ||
        event->type != UI_EVENT_KEY)
        return;

    if (event->data == LV_KEY_HOME)
    {
        ui_navigation_switch_async(UI_PAGE_HOME);
    }
    else if (event->data == LV_KEY_ESC)
    {
        lora_page_on_gesture(page, UI_GESTURE_RIGHT);
    }
}

rt_err_t lora_page_register(void)
{
    rt_err_t result;
    const ui_page_t page = {
        .id = UI_PAGE_LORA,
        .app_id = UI_APP_NONE,
        .name = "LoRa Page",
        .on_create = lora_page_on_create,
        .on_enter = lora_page_on_enter,
        .on_leave = lora_page_on_leave,
        .on_destroy = lora_page_on_destroy,
        .on_timer = lora_page_on_timer,
        .on_gesture = lora_page_on_gesture,
        .on_event = lora_page_on_event,
        .timer_interval_ms = LORA_PAGE_REFRESH_MS,
    };

    if (!lora_snapshot_lock_initialized)
    {
        result = rt_mutex_init(&lora_snapshot_lock, "lora_ui",
                               RT_IPC_FLAG_PRIO);
        if (result != RT_EOK)
            return result;
        memset(&lora_snapshot, 0, sizeof(lora_snapshot));
        lora_snapshot_lock_initialized = true;
    }
    return ui_navigation_register_page(&page);
}
