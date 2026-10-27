#include "codex_micro_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cJSON.h>
#include <rtdevice.h>
#include <ulog.h>

#include "battery_service.h"
#include "mem_section.h"

#ifdef BLUETOOTH
#include "att.h"
#include "bf0_ble_gap.h"
#include "bf0_sibles.h"
#include "bf0_sibles_advertising.h"
#include "ble_connection_manager.h"
#endif

#define CODEX_REPORT_ID                 6U
#define CODEX_REPORT_SIZE               64U
#define CODEX_REPORT_BODY_SIZE          63U
#define CODEX_FRAGMENT_SIZE             61U
#define CODEX_RPC_CHANNEL               2U
#define CODEX_RPC_BUFFER_SIZE           768U
#define CODEX_RX_QUEUE_DEPTH            4U
#define CODEX_TX_MESSAGE_SIZE           192U
#define CODEX_TX_QUEUE_DEPTH            4U
#define CODEX_WORKER_STACK_SIZE         3072U
#define CODEX_WORKER_PRIORITY           18U
#define CODEX_TX_RETRY_MS               50U
#define CODEX_ADV_RETRY_MS              200U
#define CODEX_SECURITY_RETRY_MS         100U
#define CODEX_SECURITY_RETRY_LIMIT      10U
#define CODEX_DEVICE_NAME               "Codex Micro #T-SF32"
#define CODEX_FIRMWARE_VERSION          "0.4.1-sf32"
#define CODEX_COMPAT_VENDOR_ID          0x303AU
#define CODEX_COMPAT_PRODUCT_ID         0x8360U

typedef struct
{
    uint16_t version;
    uint8_t country_code;
    uint8_t flags;
} __packed codex_hids_info_t;

typedef struct
{
    uint8_t id;
    uint8_t type;
} __packed codex_hids_report_ref_t;

typedef struct
{
    uint16_t length;
    uint16_t offset;
    char data[CODEX_TX_MESSAGE_SIZE];
} codex_tx_message_t;

typedef enum
{
    CODEX_TX_SEND_DONE = 0,
    CODEX_TX_SEND_RETRY,
    CODEX_TX_SEND_DROP,
} codex_tx_send_result_t;

typedef struct
{
    uint16_t length;
    char data[CODEX_RPC_BUFFER_SIZE];
} codex_rx_message_t;

static codex_micro_status_t codex_status;
static struct rt_thread codex_worker;
static struct rt_semaphore codex_work_sem;

/* CPU-only storage, like the other factory service stacks. Keep it out of
 * the internal SRAM heap needed by the audio decoder and LVGL. */
#if defined(BSP_USING_PSRAM)
L2_NON_RET_BSS_SECT_BEGIN(factory_codex_buffers)
L2_NON_RET_BSS_SECT(factory_codex_buffers,
    ALIGN(8) static uint8_t codex_worker_stack[CODEX_WORKER_STACK_SIZE]);
L2_NON_RET_BSS_SECT(factory_codex_buffers,
    static char codex_rx_reassembly[CODEX_RPC_BUFFER_SIZE]);
L2_NON_RET_BSS_SECT(factory_codex_buffers,
    static codex_rx_message_t codex_rx_queue[CODEX_RX_QUEUE_DEPTH]);
L2_NON_RET_BSS_SECT(factory_codex_buffers,
    static codex_tx_message_t codex_tx_queue[CODEX_TX_QUEUE_DEPTH]);
L2_NON_RET_BSS_SECT(factory_codex_buffers,
    static codex_rx_message_t codex_work_rx_message);
L2_NON_RET_BSS_SECT(factory_codex_buffers,
    static codex_tx_message_t codex_work_tx_message);
L2_NON_RET_BSS_SECT_END
#else
ALIGN(RT_ALIGN_SIZE)
static uint8_t codex_worker_stack[CODEX_WORKER_STACK_SIZE];
static char codex_rx_reassembly[CODEX_RPC_BUFFER_SIZE];
static codex_rx_message_t codex_rx_queue[CODEX_RX_QUEUE_DEPTH];
static codex_tx_message_t codex_tx_queue[CODEX_TX_QUEUE_DEPTH];
static codex_rx_message_t codex_work_rx_message;
static codex_tx_message_t codex_work_tx_message;
#endif

static bool codex_worker_initialized;
static bool codex_registration_pending;
static bool codex_services_registered;
static bool codex_service_active;
static bool codex_advertising_initialized;
static bool codex_advertising_start_pending;
static bool codex_security_request_pending;
static uint8_t codex_security_retry_count;
static uint8_t codex_conn_idx = 0xFFU;
static uint16_t codex_key_state;

static uint16_t codex_rx_reassembly_length;
static uint8_t codex_rx_head;
static uint8_t codex_rx_tail;
static uint8_t codex_rx_count;
static uint8_t codex_tx_head;
static uint8_t codex_tx_tail;
static uint8_t codex_tx_count;

#ifdef BLUETOOTH
static sibles_hdl codex_hids_handle;
static sibles_hdl codex_dis_handle;
static uint8_t codex_ctrl_point;
static uint8_t codex_input_report[CODEX_REPORT_BODY_SIZE];
static uint16_t codex_notify_config;

enum
{
    CODEX_HIDS_REMOTE_WAKE = 1U,
    CODEX_HIDS_NORMALLY_CONNECTABLE = 2U,
    CODEX_HIDS_INPUT = 1U,
    CODEX_HIDS_OUTPUT = 2U,
};

static codex_hids_info_t codex_hids_info = {
    .version = 0x0111U,
    .country_code = 0U,
    .flags = CODEX_HIDS_REMOTE_WAKE | CODEX_HIDS_NORMALLY_CONNECTABLE,
};

static codex_hids_report_ref_t codex_input_ref = {
    .id = CODEX_REPORT_ID,
    .type = CODEX_HIDS_INPUT,
};

static codex_hids_report_ref_t codex_output_ref = {
    .id = CODEX_REPORT_ID,
    .type = CODEX_HIDS_OUTPUT,
};

static const uint8_t codex_report_map[] = {
    0x06, 0x00, 0xFF,
    0x09, 0x01,
    0xA1, 0x01,
    0x85, CODEX_REPORT_ID,
    0x15, 0x00,
    0x26, 0xFF, 0x00,
    0x75, 0x08,
    0x95, CODEX_REPORT_BODY_SIZE,
    0x09, 0x02,
    0x81, 0x02,
    0x95, CODEX_REPORT_BODY_SIZE,
    0x09, 0x03,
    0x91, 0x02,
    0xC0,
};

enum
{
    CODEX_HIDS_IDX_SVC = 0,
    CODEX_HIDS_IDX_INFO_CHAR,
    CODEX_HIDS_IDX_INFO_VAL,
    CODEX_HIDS_IDX_REPORT_MAP_CHAR,
    CODEX_HIDS_IDX_REPORT_MAP_VAL,
    CODEX_HIDS_IDX_INPUT_CHAR,
    CODEX_HIDS_IDX_INPUT_VAL,
    CODEX_HIDS_IDX_INPUT_REF,
    CODEX_HIDS_IDX_INPUT_CCC,
    CODEX_HIDS_IDX_OUTPUT_CHAR,
    CODEX_HIDS_IDX_OUTPUT_VAL,
    CODEX_HIDS_IDX_OUTPUT_REF,
    CODEX_HIDS_IDX_CTRL_CHAR,
    CODEX_HIDS_IDX_CTRL_VAL,
    CODEX_HIDS_ATT_COUNT,
};

static struct attm_desc codex_hids_att_db[] = {
    [CODEX_HIDS_IDX_SVC] =
        {ATT_DECL_PRIMARY_SERVICE, PERM(RD, ENABLE), 0, 0},
    [CODEX_HIDS_IDX_INFO_CHAR] =
        {ATT_DECL_CHARACTERISTIC, PERM(RD, ENABLE), 0, 0},
    [CODEX_HIDS_IDX_INFO_VAL] =
        {ATT_CHAR_HID_INFO, PERM(RD, ENABLE), PERM(RI, ENABLE),
         sizeof(codex_hids_info)},
    [CODEX_HIDS_IDX_REPORT_MAP_CHAR] =
        {ATT_DECL_CHARACTERISTIC, PERM(RD, ENABLE), 0, 0},
    [CODEX_HIDS_IDX_REPORT_MAP_VAL] =
        {ATT_CHAR_REPORT_MAP, PERM(RD, ENABLE),
         PERM(RI, ENABLE), sizeof(codex_report_map)},
    [CODEX_HIDS_IDX_INPUT_CHAR] =
        {ATT_DECL_CHARACTERISTIC, PERM(RD, ENABLE), 0, 0},
    [CODEX_HIDS_IDX_INPUT_VAL] =
        {ATT_CHAR_REPORT, PERM(RD, ENABLE) | PERM(NTF, ENABLE),
         PERM(UUID_LEN, UUID_16) | PERM(RI, ENABLE),
         CODEX_REPORT_BODY_SIZE},
    [CODEX_HIDS_IDX_INPUT_REF] =
        {ATT_DESC_REPORT_REF, PERM(RD, ENABLE),
         PERM(RI, ENABLE), sizeof(codex_input_ref)},
    [CODEX_HIDS_IDX_INPUT_CCC] =
        {ATT_DESC_CLIENT_CHAR_CFG,
         PERM(RD, ENABLE) | PERM(WRITE_REQ, ENABLE) | PERM(WP, UNAUTH),
         PERM(RI, ENABLE), 2},
    [CODEX_HIDS_IDX_OUTPUT_CHAR] =
        {ATT_DECL_CHARACTERISTIC, PERM(RD, ENABLE), 0, 0},
    [CODEX_HIDS_IDX_OUTPUT_VAL] =
        {ATT_CHAR_REPORT,
         PERM(RD, ENABLE) | PERM(WRITE_REQ, ENABLE) |
             PERM(WRITE_COMMAND, ENABLE) | PERM(WP, UNAUTH),
         PERM(UUID_LEN, UUID_16) | PERM(RI, ENABLE),
         CODEX_REPORT_BODY_SIZE},
    [CODEX_HIDS_IDX_OUTPUT_REF] =
        {ATT_DESC_REPORT_REF, PERM(RD, ENABLE),
         PERM(RI, ENABLE), sizeof(codex_output_ref)},
    [CODEX_HIDS_IDX_CTRL_CHAR] =
        {ATT_DECL_CHARACTERISTIC, PERM(RD, ENABLE), 0, 0},
    [CODEX_HIDS_IDX_CTRL_VAL] =
        {ATT_CHAR_HID_CTNL_PT,
         PERM(RD, ENABLE) | PERM(WRITE_REQ, ENABLE) |
             PERM(WRITE_COMMAND, ENABLE),
         0, sizeof(codex_ctrl_point)},
};

enum
{
    CODEX_DIS_IDX_SVC = 0,
    CODEX_DIS_IDX_MANUFACTURER_CHAR,
    CODEX_DIS_IDX_MANUFACTURER_VAL,
    CODEX_DIS_IDX_MODEL_CHAR,
    CODEX_DIS_IDX_MODEL_VAL,
    CODEX_DIS_IDX_FIRMWARE_CHAR,
    CODEX_DIS_IDX_FIRMWARE_VAL,
    CODEX_DIS_IDX_PNP_CHAR,
    CODEX_DIS_IDX_PNP_VAL,
    CODEX_DIS_ATT_COUNT,
};

static struct attm_desc codex_dis_att_db[] = {
    [CODEX_DIS_IDX_SVC] =
        {ATT_DECL_PRIMARY_SERVICE, PERM(RD, ENABLE), 0, 0},
    [CODEX_DIS_IDX_MANUFACTURER_CHAR] =
        {ATT_DECL_CHARACTERISTIC, PERM(RD, ENABLE), 0, 0},
    [CODEX_DIS_IDX_MANUFACTURER_VAL] =
        {ATT_CHAR_MANUF_NAME, PERM(RD, ENABLE), PERM(RI, ENABLE), 16},
    [CODEX_DIS_IDX_MODEL_CHAR] =
        {ATT_DECL_CHARACTERISTIC, PERM(RD, ENABLE), 0, 0},
    [CODEX_DIS_IDX_MODEL_VAL] =
        {ATT_CHAR_MODEL_NB, PERM(RD, ENABLE), PERM(RI, ENABLE), 16},
    [CODEX_DIS_IDX_FIRMWARE_CHAR] =
        {ATT_DECL_CHARACTERISTIC, PERM(RD, ENABLE), 0, 0},
    [CODEX_DIS_IDX_FIRMWARE_VAL] =
        {ATT_CHAR_FW_REV, PERM(RD, ENABLE), PERM(RI, ENABLE), 16},
    [CODEX_DIS_IDX_PNP_CHAR] =
        {ATT_DECL_CHARACTERISTIC, PERM(RD, ENABLE), 0, 0},
    [CODEX_DIS_IDX_PNP_VAL] =
        {ATT_CHAR_PNP_ID, PERM(RD, ENABLE), PERM(RI, ENABLE), 7},
};

static const char codex_manufacturer[] = "Work Louder";
static const char codex_model[] = "Codex Micro";
static const uint8_t codex_pnp_id[] = {
    0x02U,
    (uint8_t)(CODEX_COMPAT_VENDOR_ID & 0xFFU),
    (uint8_t)(CODEX_COMPAT_VENDOR_ID >> 8),
    (uint8_t)(CODEX_COMPAT_PRODUCT_ID & 0xFFU),
    (uint8_t)(CODEX_COMPAT_PRODUCT_ID >> 8),
    0x01U, 0x04U,
};

SIBLES_ADVERTISING_CONTEXT_DECLAR(codex_advertising_context);
#endif /* BLUETOOTH */

static rt_base_t codex_lock(void)
{
    return rt_hw_interrupt_disable();
}

static void codex_unlock(rt_base_t level)
{
    rt_hw_interrupt_enable(level);
}

static codex_micro_agent_state_t codex_state_from_color(uint32_t color)
{
    switch (color)
    {
    case 0x000000U: return CODEX_MICRO_AGENT_OFF;
    case 0xFFFFFFU: return CODEX_MICRO_AGENT_IDLE;
    case 0x304FFEU: return CODEX_MICRO_AGENT_WORKING;
    case 0x00FF4CU: return CODEX_MICRO_AGENT_COMPLETE;
    case 0xFF6D00U: return CODEX_MICRO_AGENT_AWAITING_INPUT;
    case 0xFF0033U: return CODEX_MICRO_AGENT_ERROR;
    default: return CODEX_MICRO_AGENT_UNKNOWN;
    }
}

static void codex_schedule_work(void)
{
    if (codex_worker_initialized)
        rt_sem_release(&codex_work_sem);
}

static bool codex_json_complete(const char *text, uint16_t length)
{
    int depth = 0;
    bool started = false;
    bool in_string = false;
    bool escaped = false;
    uint16_t index;

    for (index = 0U; index < length; index++)
    {
        char value = text[index];

        if (!started)
        {
            if (value == '{')
            {
                started = true;
                depth = 1;
            }
            continue;
        }
        if (in_string)
        {
            if (escaped)
                escaped = false;
            else if (value == '\\')
                escaped = true;
            else if (value == '"')
                in_string = false;
            continue;
        }
        if (value == '"')
            in_string = true;
        else if (value == '{')
            depth++;
        else if (value == '}' && --depth == 0)
            return true;
    }
    return false;
}

static bool codex_rx_enqueue(const char *text, uint16_t length)
{
    rt_base_t level;
    codex_rx_message_t *message;

    if (length == 0U || length >= CODEX_RPC_BUFFER_SIZE)
        return false;
    level = codex_lock();
    if (codex_rx_count >= CODEX_RX_QUEUE_DEPTH)
    {
        codex_status.protocol_errors++;
        codex_unlock(level);
        return false;
    }
    message = &codex_rx_queue[codex_rx_tail];
    memcpy(message->data, text, length);
    message->data[length] = '\0';
    message->length = length;
    codex_rx_tail = (uint8_t)((codex_rx_tail + 1U) % CODEX_RX_QUEUE_DEPTH);
    codex_rx_count++;
    codex_unlock(level);
    codex_schedule_work();
    return true;
}

static bool codex_tx_enqueue(const char *text, uint16_t length)
{
    rt_base_t level;
    codex_tx_message_t *message;

    if (text == RT_NULL || length == 0U || length > CODEX_TX_MESSAGE_SIZE)
        return false;
    level = codex_lock();
    if (codex_tx_count >= CODEX_TX_QUEUE_DEPTH)
    {
        codex_status.protocol_errors++;
        codex_unlock(level);
        return false;
    }
    message = &codex_tx_queue[codex_tx_tail];
    memcpy(message->data, text, length);
    message->length = length;
    message->offset = 0U;
    codex_tx_tail = (uint8_t)((codex_tx_tail + 1U) % CODEX_TX_QUEUE_DEPTH);
    codex_tx_count++;
    codex_unlock(level);
    codex_schedule_work();
    return true;
}

static void codex_receive_fragment(uint8_t channel,
                                   const uint8_t *data,
                                   uint16_t length)
{
    uint16_t index;

    if (channel != CODEX_RPC_CHANNEL || data == RT_NULL || length == 0U)
        return;
    for (index = 0U; index < length; index++)
    {
        char value = (char)data[index];

        if (value == '\r' || value == '\n')
        {
            if (codex_rx_reassembly_length > 0U)
            {
                codex_rx_enqueue(codex_rx_reassembly,
                                 codex_rx_reassembly_length);
                codex_rx_reassembly_length = 0U;
            }
            continue;
        }
        if (codex_rx_reassembly_length >= CODEX_RPC_BUFFER_SIZE - 1U)
        {
            codex_rx_reassembly_length = 0U;
            codex_status.protocol_errors++;
            continue;
        }
        codex_rx_reassembly[codex_rx_reassembly_length++] = value;
        if (codex_json_complete(codex_rx_reassembly,
                                codex_rx_reassembly_length))
        {
            codex_rx_enqueue(codex_rx_reassembly,
                             codex_rx_reassembly_length);
            codex_rx_reassembly_length = 0U;
        }
    }
}

static void codex_send_response(int id, const char *payload)
{
    char response[CODEX_TX_MESSAGE_SIZE];
    int length;

    if (id < 0 || payload == RT_NULL)
        return;
    length = snprintf(response, sizeof(response),
                      "{\"id\":%d,%s}\r\n", id, payload);
    if (length > 0 && length < (int)sizeof(response))
        codex_tx_enqueue(response, (uint16_t)length);
}

static void codex_parse_thread_status(cJSON *params)
{
    cJSON *entry;
    rt_base_t level;

    if (!cJSON_IsArray(params))
        return;
    level = codex_lock();
    cJSON_ArrayForEach(entry, params)
    {
        cJSON *id = cJSON_GetObjectItemCaseSensitive(entry, "id");
        cJSON *color = cJSON_GetObjectItemCaseSensitive(entry, "c");
        cJSON *effect = cJSON_GetObjectItemCaseSensitive(entry, "e");
        int slot;

        if (!cJSON_IsNumber(id))
            continue;
        slot = id->valueint;
        if (slot < 0 || slot >= (int)CODEX_MICRO_AGENT_COUNT)
            continue;
        if (cJSON_IsNumber(color) && color->valuedouble >= 0.0 &&
                color->valuedouble <= 16777215.0)
        {
            uint32_t rgb = (uint32_t)color->valuedouble;

            codex_status.agents[slot].color = rgb;
            codex_status.agents[slot].state = codex_state_from_color(rgb);
        }
        if (cJSON_IsNumber(effect))
            codex_status.agents[slot].selected = effect->valueint == 4;
    }
    codex_unlock(level);
}

static void codex_process_message(const char *text)
{
    cJSON *root;
    cJSON *method_item;
    cJSON *params;
    cJSON *id_item;
    const char *method;
    int id = -1;
    rt_base_t level;

    root = cJSON_Parse(text);
    if (root == RT_NULL)
    {
        level = codex_lock();
        codex_status.protocol_errors++;
        codex_unlock(level);
        return;
    }
    method_item = cJSON_GetObjectItemCaseSensitive(root, "method");
    if (!cJSON_IsString(method_item))
        method_item = cJSON_GetObjectItemCaseSensitive(root, "m");
    params = cJSON_GetObjectItemCaseSensitive(root, "params");
    if (params == RT_NULL)
        params = cJSON_GetObjectItemCaseSensitive(root, "p");
    id_item = cJSON_GetObjectItemCaseSensitive(root, "id");
    if (cJSON_IsNumber(id_item))
        id = id_item->valueint;

    method = cJSON_IsString(method_item) ? method_item->valuestring : RT_NULL;
    if (method == RT_NULL)
    {
        codex_send_response(id,
            "\"error\":{\"code\":-32600,\"message\":\"Invalid request\"}");
    }
    else if (strcmp(method, "sys.version") == 0)
    {
        codex_send_response(id,
            "\"result\":{\"version\":\"" CODEX_FIRMWARE_VERSION "\"}");
    }
    else if (strcmp(method, "device.status") == 0)
    {
        battery_info_t battery;
        char payload[144];
        int percent = 0;
        bool charging = false;

        if (battery_service_get_info(&battery) == RT_EOK)
        {
            percent = battery.percent;
            charging = battery.charging;
        }
        snprintf(payload, sizeof(payload),
                 "\"result\":{\"version\":\"%s\",\"profile_index\":0,"
                 "\"layer_index\":0,\"battery\":%d,\"is_charging\":%s}",
                 CODEX_FIRMWARE_VERSION, percent,
                 charging ? "true" : "false");
        codex_send_response(id, payload);
    }
    else if (strcmp(method, "v.oai.thstatus") == 0)
    {
        codex_parse_thread_status(params);
        codex_send_response(id, "\"result\":null");
    }
    else if (strcmp(method, "v.oai.rgbcfg") == 0 ||
             strcmp(method, "host.focused_app") == 0)
    {
        codex_send_response(id, "\"result\":null");
    }
    else if (strcmp(method, "ui.active_screen") == 0)
    {
        codex_send_response(id, "\"result\":{\"screen\":\"home\"}");
    }
    else
    {
        codex_send_response(id,
            "\"error\":{\"code\":-32601,\"message\":\"Method not found\"}");
    }

    level = codex_lock();
    codex_status.rx_messages++;
    codex_unlock(level);
    cJSON_Delete(root);
}

#ifdef BLUETOOTH
static uint8_t *codex_hids_get_cb(uint8_t conn_idx, uint8_t index,
                                  uint16_t *length)
{
    LOG_I("Codex HIDS read: conn=%u idx=%u", conn_idx, index);
    *length = 0U;
    switch (index)
    {
    case CODEX_HIDS_IDX_INFO_VAL:
        *length = sizeof(codex_hids_info);
        return (uint8_t *)&codex_hids_info;
    case CODEX_HIDS_IDX_REPORT_MAP_VAL:
        *length = sizeof(codex_report_map);
        return (uint8_t *)codex_report_map;
    case CODEX_HIDS_IDX_INPUT_VAL:
    case CODEX_HIDS_IDX_OUTPUT_VAL:
        *length = sizeof(codex_input_report);
        return codex_input_report;
    case CODEX_HIDS_IDX_INPUT_CCC:
        *length = sizeof(codex_notify_config);
        return (uint8_t *)&codex_notify_config;
    case CODEX_HIDS_IDX_INPUT_REF:
        *length = sizeof(codex_input_ref);
        return (uint8_t *)&codex_input_ref;
    case CODEX_HIDS_IDX_OUTPUT_REF:
        *length = sizeof(codex_output_ref);
        return (uint8_t *)&codex_output_ref;
    case CODEX_HIDS_IDX_CTRL_VAL:
        *length = sizeof(codex_ctrl_point);
        return &codex_ctrl_point;
    default:
        return RT_NULL;
    }
}

static uint8_t codex_hids_set_cb(uint8_t conn_idx, sibles_set_cbk_t *para)
{
    const uint8_t *report;
    uint16_t report_length;
    rt_base_t level;

    if (para == RT_NULL)
        return 0U;
    if (para->idx == CODEX_HIDS_IDX_INPUT_CCC)
    {
        codex_notify_config = para->len >= 2U
                                  ? (uint16_t)para->value[0] |
                                        ((uint16_t)para->value[1] << 8)
                                  : para->value[0];
        level = codex_lock();
        codex_status.protocol_ready = codex_notify_config != 0U;
        codex_unlock(level);
        LOG_I("Codex Micro notifications %s",
              codex_notify_config != 0U ? "enabled" : "disabled");
        return 0U;
    }
    if (para->idx == CODEX_HIDS_IDX_CTRL_VAL)
    {
        if (para->len > 0U)
            codex_ctrl_point = para->value[0];
        return 0U;
    }
    if (para->idx != CODEX_HIDS_IDX_OUTPUT_VAL || para->value == RT_NULL)
        return 0U;

    report = para->value;
    report_length = para->len;
    if (report_length == CODEX_REPORT_SIZE)
    {
        if (report[0] != CODEX_REPORT_ID)
        {
            codex_status.protocol_errors++;
            return 0U;
        }
        report++;
        report_length--;
    }
    if (report_length != CODEX_REPORT_BODY_SIZE ||
            report[1] > CODEX_FRAGMENT_SIZE)
    {
        codex_status.protocol_errors++;
        return 0U;
    }
    codex_conn_idx = conn_idx;
    codex_receive_fragment(report[0], &report[2], report[1]);
    return 0U;
}

static uint8_t *codex_dis_get_cb(uint8_t conn_idx, uint8_t index,
                                 uint16_t *length)
{
    LOG_I("Codex DIS read: conn=%u idx=%u", conn_idx, index);
    *length = 0U;
    switch (index)
    {
    case CODEX_DIS_IDX_MANUFACTURER_VAL:
        *length = sizeof(codex_manufacturer) - 1U;
        return (uint8_t *)codex_manufacturer;
    case CODEX_DIS_IDX_MODEL_VAL:
        *length = sizeof(codex_model) - 1U;
        return (uint8_t *)codex_model;
    case CODEX_DIS_IDX_FIRMWARE_VAL:
        *length = sizeof(CODEX_FIRMWARE_VERSION) - 1U;
        return (uint8_t *)CODEX_FIRMWARE_VERSION;
    case CODEX_DIS_IDX_PNP_VAL:
        *length = sizeof(codex_pnp_id);
        return (uint8_t *)codex_pnp_id;
    default:
        return RT_NULL;
    }
}

static uint8_t codex_dis_set_cb(uint8_t conn_idx, sibles_set_cbk_t *para)
{
    (void)conn_idx;
    (void)para;
    return 0U;
}

static uint8_t codex_advertising_event(uint8_t event, void *context,
                                       void *data)
{
    rt_base_t level;
    uint8_t status = SIBLES_ADV_NO_ERR;

    (void)context;
    if (event == SIBLES_ADV_EVT_ADV_STARTED && data != RT_NULL)
        status = ((sibles_adv_evt_startted_t *)data)->status;
    level = codex_lock();
    if (event == SIBLES_ADV_EVT_ADV_STARTED)
    {
        codex_status.advertising = status == SIBLES_ADV_NO_ERR;
        codex_advertising_start_pending = status != SIBLES_ADV_NO_ERR;
    }
    else if (event == SIBLES_ADV_EVT_ADV_STOPPED)
        codex_status.advertising = false;
    codex_unlock(level);
    if (event == SIBLES_ADV_EVT_ADV_STARTED)
    {
        LOG_I("Codex Micro advertising started: status=%u", status);
        if (status != SIBLES_ADV_NO_ERR)
            codex_schedule_work();
    }
    if (event == SIBLES_ADV_EVT_ADV_STOPPED && data != RT_NULL)
    {
        sibles_adv_evt_stopped_t *stopped = data;
        LOG_I("Codex Micro advertising stopped: %u", stopped->reason);
    }
    return 0U;
}

static void codex_register_services(void)
{
    sibles_register_svc_t service;
    sibles_advertising_para_t advertising = {0};
    ble_gap_dev_name_t *device_name;
    uint16_t hid_uuid = ATT_SVC_HID;
    uint16_t appearance = 961U;
    uint8_t result;

    if (codex_services_registered)
        return;

    memset(&service, 0, sizeof(service));
    service.att_db = codex_hids_att_db;
    service.num_entry = CODEX_HIDS_ATT_COUNT;
    service.sec_lvl = PERM(SVC_AUTH, NO_AUTH) |
                      PERM(SVC_UUID_LEN, UUID_16);
    service.uuid = ATT_SVC_HID;
    codex_hids_handle = sibles_register_svc(&service);
    if (codex_hids_handle == RT_NULL)
    {
        LOG_E("Register Codex Micro HID service failed");
        codex_status.protocol_errors++;
        return;
    }
    sibles_register_cbk(codex_hids_handle, codex_hids_get_cb,
                        codex_hids_set_cb);

    memset(&service, 0, sizeof(service));
    service.att_db = codex_dis_att_db;
    service.num_entry = CODEX_DIS_ATT_COUNT;
    service.sec_lvl = PERM(SVC_AUTH, NO_AUTH) |
                      PERM(SVC_UUID_LEN, UUID_16);
    service.uuid = ATT_SVC_DEVICE_INFO;
    codex_dis_handle = sibles_register_svc(&service);
    if (codex_dis_handle != RT_NULL)
        sibles_register_cbk(codex_dis_handle, codex_dis_get_cb,
                            codex_dis_set_cb);
    else
        LOG_W("Register Codex Micro Device Information service failed");

    device_name = rt_malloc(sizeof(*device_name) +
                            sizeof(CODEX_DEVICE_NAME) - 1U);
    if (device_name != RT_NULL)
    {
        device_name->len = sizeof(CODEX_DEVICE_NAME) - 1U;
        memcpy(device_name->name, CODEX_DEVICE_NAME, device_name->len);
        ble_gap_set_dev_name(device_name);
        rt_free(device_name);
    }

    advertising.own_addr_type = GAPM_STATIC_ADDR;
    advertising.config.adv_mode = SIBLES_ADV_CONNECT_MODE;
    advertising.config.mode_config.conn_config.duration = 0U;
    advertising.config.mode_config.conn_config.interval = 0x30U;
    advertising.config.max_tx_pwr = 0x7FU;
    advertising.config.is_auto_restart = 1U;
    advertising.adv_data.disc_mode = GAPM_ADV_MODE_GEN_DISC;
    advertising.rsp_data.completed_name = rt_malloc(
        sizeof(sibles_adv_type_name_t) + sizeof(CODEX_DEVICE_NAME) - 1U);
    advertising.adv_data.completed_uuid = rt_malloc(
        sizeof(sibles_adv_type_srv_uuid_t) + 1U + ATT_UUID_16_LEN);
    advertising.adv_data.appearance = rt_malloc(sizeof(appearance));
    if (advertising.rsp_data.completed_name == RT_NULL ||
            advertising.adv_data.completed_uuid == RT_NULL ||
            advertising.adv_data.appearance == RT_NULL)
    {
        LOG_E("Allocate Codex Micro advertising fields failed");
        goto advertising_cleanup;
    }
    advertising.rsp_data.completed_name->name_len =
        sizeof(CODEX_DEVICE_NAME) - 1U;
    memcpy(advertising.rsp_data.completed_name->name, CODEX_DEVICE_NAME,
           sizeof(CODEX_DEVICE_NAME) - 1U);
    advertising.adv_data.completed_uuid->count = 1U;
    advertising.adv_data.completed_uuid->uuid_list[0].uuid_len =
        ATT_UUID_16_LEN;
    memcpy(advertising.adv_data.completed_uuid->uuid_list[0].uuid.uuid_16,
           &hid_uuid, ATT_UUID_16_LEN);
    memcpy(advertising.adv_data.appearance, &appearance, sizeof(appearance));
    advertising.evt_handler = codex_advertising_event;

    result = sibles_advertising_init(codex_advertising_context,
                                     &advertising);
    if (result == SIBLES_ADV_NO_ERR)
        codex_advertising_initialized = true;
    else
    {
        LOG_E("Initialize Codex Micro advertising failed: %u", result);
    }

advertising_cleanup:
    rt_free(advertising.rsp_data.completed_name);
    rt_free(advertising.adv_data.completed_uuid);
    rt_free(advertising.adv_data.appearance);
    codex_services_registered = true;
    LOG_I("Codex Micro BLE HID ready: %s", CODEX_DEVICE_NAME);
}

static bool codex_try_start_advertising(void)
{
    uint8_t result;

    if (!codex_service_active || !codex_advertising_initialized ||
            codex_status.connected || codex_status.advertising)
    {
        return true;
    }
    result = sibles_advertising_start(codex_advertising_context);
    if (result != SIBLES_ADV_NO_ERR)
    {
        LOG_W("Start Codex Micro advertising deferred: %u", result);
        return false;
    }
    LOG_I("Codex Micro advertising requested");
    return true;
}

static codex_tx_send_result_t codex_send_message_now(
    codex_tx_message_t *message)
{
    if (message == RT_NULL || codex_hids_handle == RT_NULL ||
            !codex_status.connected || !codex_status.protocol_ready)
        return CODEX_TX_SEND_DROP;
    while (message->offset < message->length)
    {
        uint16_t fragment_length = message->length - message->offset;
        sibles_value_t value;
        int result;

        if (fragment_length > CODEX_FRAGMENT_SIZE)
            fragment_length = CODEX_FRAGMENT_SIZE;
        memset(codex_input_report, 0, sizeof(codex_input_report));
        codex_input_report[0] = CODEX_RPC_CHANNEL;
        codex_input_report[1] = (uint8_t)fragment_length;
        memcpy(&codex_input_report[2], &message->data[message->offset],
               fragment_length);
        value.hdl = codex_hids_handle;
        value.idx = CODEX_HIDS_IDX_INPUT_VAL;
        value.len = sizeof(codex_input_report);
        value.value = codex_input_report;
        result = sibles_write_value(codex_conn_idx, &value);
        if (result == 0)
            return CODEX_TX_SEND_RETRY;
        if (result != (int)sizeof(codex_input_report))
        {
            codex_status.protocol_errors++;
            return CODEX_TX_SEND_DROP;
        }
        message->offset += fragment_length;
    }
    codex_status.tx_messages++;
    return CODEX_TX_SEND_DONE;
}
#endif /* BLUETOOTH */

static bool codex_rx_dequeue(codex_rx_message_t *message)
{
    rt_base_t level = codex_lock();

    if (codex_rx_count == 0U)
    {
        codex_unlock(level);
        return false;
    }
    *message = codex_rx_queue[codex_rx_head];
    codex_rx_head = (uint8_t)((codex_rx_head + 1U) % CODEX_RX_QUEUE_DEPTH);
    codex_rx_count--;
    codex_unlock(level);
    return true;
}

static bool codex_tx_dequeue(codex_tx_message_t *message)
{
    rt_base_t level = codex_lock();

    if (codex_tx_count == 0U)
    {
        codex_unlock(level);
        return false;
    }
    *message = codex_tx_queue[codex_tx_head];
    codex_tx_head = (uint8_t)((codex_tx_head + 1U) % CODEX_TX_QUEUE_DEPTH);
    codex_tx_count--;
    codex_unlock(level);
    return true;
}

static bool codex_tx_requeue_front(const codex_tx_message_t *message)
{
    rt_base_t level = codex_lock();

    if (codex_tx_count >= CODEX_TX_QUEUE_DEPTH)
    {
        codex_status.protocol_errors++;
        codex_unlock(level);
        return false;
    }
    codex_tx_head = (uint8_t)((codex_tx_head + CODEX_TX_QUEUE_DEPTH - 1U) %
                              CODEX_TX_QUEUE_DEPTH);
    codex_tx_queue[codex_tx_head] = *message;
    codex_tx_count++;
    codex_unlock(level);
    return true;
}

static void codex_worker_entry(void *parameter)
{
    rt_tick_t wait_ticks = RT_WAITING_FOREVER;

    (void)parameter;
    while (true)
    {
        rt_sem_take(&codex_work_sem, wait_ticks);
        wait_ticks = RT_WAITING_FOREVER;
#ifdef BLUETOOTH
        if (codex_registration_pending)
        {
            codex_registration_pending = false;
            codex_register_services();
        }
        if (codex_security_request_pending)
        {
            uint8_t conn_idx = codex_conn_idx;
            uint8_t result;

            codex_security_request_pending = false;
            if (conn_idx != 0xFFU && codex_status.connected)
            {
                result = connection_manager_set_link_security(
                    conn_idx, LE_SECURITY_LEVEL_NO_MITM_BOND);
                LOG_I("Codex Micro security requested: conn=%u result=%u",
                      conn_idx, result);
                if (result != CM_STATUS_OK &&
                        codex_security_retry_count <
                            CODEX_SECURITY_RETRY_LIMIT)
                {
                    codex_security_retry_count++;
                    codex_security_request_pending = true;
                    wait_ticks = rt_tick_from_millisecond(
                        CODEX_SECURITY_RETRY_MS);
                }
            }
        }
        if (codex_advertising_start_pending &&
                !codex_try_start_advertising())
        {
            wait_ticks = rt_tick_from_millisecond(CODEX_ADV_RETRY_MS);
        }
#endif
        while (codex_rx_dequeue(&codex_work_rx_message))
            codex_process_message(codex_work_rx_message.data);
#ifdef BLUETOOTH
        while (codex_tx_dequeue(&codex_work_tx_message))
        {
            codex_tx_send_result_t result =
                codex_send_message_now(&codex_work_tx_message);

            if (result == CODEX_TX_SEND_RETRY)
            {
                if (codex_tx_requeue_front(&codex_work_tx_message))
                    wait_ticks = rt_tick_from_millisecond(CODEX_TX_RETRY_MS);
                break;
            }
        }
#else
        (void)codex_work_tx_message;
#endif
    }
}

#ifdef BLUETOOTH
static int codex_ble_event_handler(uint16_t event_id, uint8_t *data,
                                   uint16_t length, uint32_t context)
{
    rt_base_t level;
    bool log_connected = false;
    bool log_disconnected = false;
    bool log_encrypted = false;
    bool log_bond = false;
    bool log_security_result = false;
    uint8_t detail = 0U;

    (void)length;
    (void)context;
    level = codex_lock();
    switch (event_id)
    {
    case BLE_POWER_ON_IND:
        codex_status.stack_ready = true;
        codex_registration_pending = true;
        break;
    case BLE_GAP_CONNECTED_IND:
        if (data != RT_NULL)
        {
            ble_gap_connect_ind_t *connected = (ble_gap_connect_ind_t *)data;

            codex_conn_idx = connected->conn_idx;
            codex_status.connected = true;
            codex_status.bonded = false;
            codex_status.encrypted = false;
            codex_status.advertising = false;
            codex_status.mtu = 23U;
            codex_security_retry_count = 0U;
            codex_security_request_pending = true;
            log_connected = true;
        }
        break;
    case SIBLES_MTU_EXCHANGE_IND:
        if (data != RT_NULL)
            codex_status.mtu = ((sibles_mtu_exchange_ind_t *)data)->mtu;
        break;
    case BLE_GAP_DISCONNECTED_IND:
        if (data != RT_NULL)
            detail = ((ble_gap_disconnected_ind_t *)data)->reason;
        codex_conn_idx = 0xFFU;
        codex_notify_config = 0U;
        codex_status.connected = false;
        codex_status.bonded = false;
        codex_status.encrypted = false;
        codex_status.protocol_ready = false;
        codex_status.advertising = false;
        codex_status.last_disconnect_reason = detail;
        codex_advertising_start_pending = codex_service_active;
        codex_security_request_pending = false;
        codex_security_retry_count = 0U;
        codex_key_state = 0U;
        log_disconnected = true;
        break;
    case BLE_GAP_ENCRYPT_IND:
        if (data != RT_NULL)
        {
            ble_gap_encrypt_ind_t *encrypted =
                (ble_gap_encrypt_ind_t *)data;

            codex_status.encrypted = true;
            detail = encrypted->auth;
            log_encrypted = true;
        }
        break;
    case BLE_GAP_BOND_IND:
        if (data != RT_NULL)
        {
            ble_gap_bond_ind_t *bond = (ble_gap_bond_ind_t *)data;

            detail = bond->info;
            if (bond->info == GAPC_PAIRING_SUCCEED)
                codex_status.bonded = true;
            else if (bond->info == GAPC_PAIRING_FAILED)
                codex_status.bonded = false;
            log_bond = true;
        }
        break;
    case BLE_GAP_SECURITY_REQUEST_CNF:
        if (data != RT_NULL)
        {
            detail = ((ble_gap_security_request_cnf_t *)data)->status;
            log_security_result = true;
        }
        break;
    default:
        break;
    }
    codex_unlock(level);
    if (log_connected)
        LOG_I("Codex Micro BLE connected: conn=%u", codex_conn_idx);
    if (log_disconnected)
        LOG_I("Codex Micro BLE disconnected: reason=%u", detail);
    if (log_encrypted)
        LOG_I("Codex Micro BLE encrypted: auth=0x%02x", detail);
    if (log_bond)
        LOG_I("Codex Micro BLE bond event: info=%u", detail);
    if (log_security_result)
        LOG_I("Codex Micro security request result: %u", detail);
    if (event_id == BLE_POWER_ON_IND ||
            event_id == BLE_GAP_CONNECTED_IND ||
            event_id == BLE_GAP_DISCONNECTED_IND ||
            event_id == SIBLES_WRITE_VALUE_RSP)
        codex_schedule_work();
    return 0;
}
BLE_EVENT_REGISTER(codex_ble_event_handler, RT_NULL);
#endif /* BLUETOOTH */

rt_err_t codex_micro_service_init(void)
{
    rt_base_t level;
    bool stack_ready;
    rt_err_t result;

    if (codex_status.initialized)
        return RT_EOK;
    level = codex_lock();
    stack_ready = codex_status.stack_ready;
    memset(&codex_status, 0, sizeof(codex_status));
    codex_status.stack_ready = stack_ready;
    codex_status.mtu = 23U;
    codex_conn_idx = 0xFFU;
    codex_unlock(level);

    result = rt_sem_init(&codex_work_sem, "codex", 0, RT_IPC_FLAG_FIFO);
    if (result != RT_EOK)
        return result;
    result = rt_thread_init(&codex_worker, "codex",
                            codex_worker_entry, RT_NULL,
                            codex_worker_stack, sizeof(codex_worker_stack),
                            CODEX_WORKER_PRIORITY, 10U);
    if (result != RT_EOK)
    {
        rt_sem_detach(&codex_work_sem);
        return result;
    }
    codex_worker_initialized = true;
    result = rt_thread_startup(&codex_worker);
    if (result != RT_EOK)
    {
        codex_worker_initialized = false;
        rt_thread_detach(&codex_worker);
        rt_sem_detach(&codex_work_sem);
        return result;
    }
    level = codex_lock();
    codex_status.initialized = true;
    codex_unlock(level);
    if (codex_registration_pending || stack_ready)
    {
        codex_registration_pending = true;
        codex_schedule_work();
    }
    LOG_I("Codex Micro compatibility service initialized");
    return RT_EOK;
}

rt_err_t codex_micro_service_start(void)
{
    rt_base_t level;

    if (!codex_status.initialized)
        return -RT_ERROR;
    level = codex_lock();
    codex_service_active = true;
    codex_advertising_start_pending = true;
    codex_unlock(level);
    codex_schedule_work();
    return RT_EOK;
}

rt_err_t codex_micro_service_get_status(codex_micro_status_t *status)
{
    rt_base_t level;

    if (status == RT_NULL)
        return -RT_EINVAL;
    level = codex_lock();
    *status = codex_status;
    codex_unlock(level);
    return codex_status.initialized ? RT_EOK : -RT_ERROR;
}

static const char *codex_key_name(codex_micro_key_t key,
                                  char name[8], uint8_t *agent)
{
    if (key <= CODEX_MICRO_KEY_AGENT_5)
    {
        snprintf(name, 8U, "AG%02u", (unsigned int)key);
        *agent = (uint8_t)key;
    }
    else if (key <= CODEX_MICRO_KEY_SEND)
    {
        snprintf(name, 8U, "ACT%02u", (unsigned int)key);
        *agent = 0xFFU;
    }
    else
    {
        return RT_NULL;
    }
    return name;
}

rt_err_t codex_micro_service_send_key(codex_micro_key_t key, bool pressed)
{
    char name[8];
    char message[112];
    uint8_t agent;
    uint16_t mask;
    int length;
    rt_base_t level;

    if (codex_key_name(key, name, &agent) == RT_NULL)
        return -RT_EINVAL;
    if (!codex_status.protocol_ready)
        return -RT_EBUSY;
    mask = (uint16_t)(1U << (uint8_t)key);
    level = codex_lock();
    if (((codex_key_state & mask) != 0U) == pressed)
    {
        codex_unlock(level);
        return RT_EOK;
    }
    if (pressed)
        codex_key_state |= mask;
    else
        codex_key_state &= (uint16_t)~mask;
    codex_unlock(level);

    if (agent != 0xFFU)
    {
        length = snprintf(message, sizeof(message),
            "{\"method\":\"v.oai.hid\",\"params\":{\"k\":\"%s\","
            "\"act\":%u,\"ag\":%u}}\r\n",
            name, pressed ? 1U : 0U, agent);
    }
    else
    {
        length = snprintf(message, sizeof(message),
            "{\"method\":\"v.oai.hid\",\"params\":{\"k\":\"%s\","
            "\"act\":%u}}\r\n", name, pressed ? 1U : 0U);
    }
    if (length <= 0 || length >= (int)sizeof(message) ||
            !codex_tx_enqueue(message, (uint16_t)length))
    {
        level = codex_lock();
        if (((codex_key_state & mask) != 0U) == pressed)
        {
            if (pressed)
                codex_key_state &= (uint16_t)~mask;
            else
                codex_key_state |= mask;
        }
        codex_unlock(level);
        return -RT_EFULL;
    }
    return RT_EOK;
}

rt_err_t codex_micro_service_send_key_pulse(codex_micro_key_t key)
{
    rt_err_t result = codex_micro_service_send_key(key, true);

    if (result != RT_EOK)
        return result;
    return codex_micro_service_send_key(key, false);
}

rt_err_t codex_micro_service_send_encoder(bool clockwise)
{
    char message[96];
    int length;

    if (!codex_status.protocol_ready)
        return -RT_EBUSY;
    length = snprintf(message, sizeof(message),
        "{\"method\":\"v.oai.hid\",\"params\":{\"k\":\"%s\","
        "\"act\":1}}\r\n", clockwise ? "ENC_CW" : "ENC_CC");
    return length > 0 && length < (int)sizeof(message) &&
               codex_tx_enqueue(message, (uint16_t)length)
               ? RT_EOK : -RT_EFULL;
}

rt_err_t codex_micro_service_send_radial(float angle, float distance)
{
    char message[112];
    int length;

    if (!codex_status.protocol_ready)
        return -RT_EBUSY;
    if (angle < 0.0f)
        angle = 0.0f;
    else if (angle > 1.0f)
        angle = 1.0f;
    if (distance < 0.0f)
        distance = 0.0f;
    else if (distance > 1.0f)
        distance = 1.0f;
    length = snprintf(message, sizeof(message),
        "{\"method\":\"v.oai.rad\",\"params\":{\"a\":%.3f,"
        "\"d\":%.3f}}\r\n", (double)angle, (double)distance);
    return length > 0 && length < (int)sizeof(message) &&
               codex_tx_enqueue(message, (uint16_t)length)
               ? RT_EOK : -RT_EFULL;
}

static int codex_status_command(int argc, char **argv)
{
    codex_micro_status_t status;
    uint8_t index;

    (void)argc;
    (void)argv;
    codex_micro_service_get_status(&status);
    rt_kprintf("Codex Micro: init=%u stack=%u adv=%u conn=%u bond=%u "
               "enc=%u ready=%u mtu=%u disc=%u rx=%lu tx=%lu errors=%lu\n",
               status.initialized, status.stack_ready, status.advertising,
               status.connected, status.bonded, status.encrypted,
               status.protocol_ready, status.mtu,
               status.last_disconnect_reason,
               (unsigned long)status.rx_messages,
               (unsigned long)status.tx_messages,
               (unsigned long)status.protocol_errors);
    for (index = 0U; index < CODEX_MICRO_AGENT_COUNT; index++)
    {
        rt_kprintf("  AG%02u state=%u selected=%u color=%06lx\n", index,
                   status.agents[index].state,
                   status.agents[index].selected,
                   (unsigned long)status.agents[index].color);
    }
    return 0;
}
MSH_CMD_EXPORT_ALIAS(codex_status_command, codex_status,
                     show Codex Micro BLE and agent status);

static int codex_key_command(int argc, char **argv)
{
    int key;

    if (argc != 2)
    {
        rt_kprintf("Usage: codex_key <0..12>\n");
        return -RT_EINVAL;
    }
    key = atoi(argv[1]);
    if (key < 0 || key > CODEX_MICRO_KEY_SEND)
        return -RT_EINVAL;
    return codex_micro_service_send_key_pulse((codex_micro_key_t)key);
}
MSH_CMD_EXPORT_ALIAS(codex_key_command, codex_key,
                     send a Codex Micro key pulse);

static int codex_demo_command(int argc, char **argv)
{
    static const uint32_t colors[CODEX_MICRO_AGENT_COUNT] = {
        0xFFFFFFU, 0x304FFEU, 0x00FF4CU,
        0xFF6D00U, 0xFF0033U, 0x000000U,
    };
    rt_base_t level;
    uint8_t index;

    (void)argc;
    (void)argv;
    level = codex_lock();
    for (index = 0U; index < CODEX_MICRO_AGENT_COUNT; index++)
    {
        codex_status.agents[index].color = colors[index];
        codex_status.agents[index].state =
            codex_state_from_color(colors[index]);
        codex_status.agents[index].selected = index == 1U;
    }
    codex_unlock(level);
    rt_kprintf("Codex Micro demo agent states loaded\n");
    return 0;
}
MSH_CMD_EXPORT_ALIAS(codex_demo_command, codex_demo,
                     load demo Codex Micro agent states);
