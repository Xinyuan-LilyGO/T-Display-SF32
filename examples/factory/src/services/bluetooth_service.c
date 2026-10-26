#include "bluetooth_service.h"

#include <string.h>

#include "audio_service.h"
#include "platform_psram.h"

#ifdef BLUETOOTH
#include "bf0_sibles.h"
#include "bt_connection_manager.h"
#include "bts2_app_inc.h"
#include "bts2_app_interface.h"

#endif

#ifdef DBG_WARNING
#undef DBG_WARNING
#endif
#ifdef DBG_ERROR
#undef DBG_ERROR
#endif
#include <ulog.h>

#define BLUETOOTH_PAN_CONNECT_DELAY_MS 3000U
#define BLUETOOTH_A2DP_SUSPEND_GRACE_MS 400U
#define BLUETOOTH_WORK_QUEUE_DEPTH     8U
#define BLUETOOTH_WORKER_STACK_SIZE    2048U
#define BLUETOOTH_WORKER_PRIORITY      (RT_THREAD_PRIORITY_HIGH + 6U)
#define BLUETOOTH_LOCAL_NAME           "T-SF32-display"

#ifdef BLUETOOTH
typedef enum
{
    BLUETOOTH_WORK_CONFIGURE = 0,
    BLUETOOTH_WORK_POWER_ON,
    BLUETOOTH_WORK_POWER_OFF,
    BLUETOOTH_WORK_CONNECT_A2DP,
    BLUETOOTH_WORK_CONNECT_AVRCP,
    BLUETOOTH_WORK_CONNECT_PAN,
    BLUETOOTH_WORK_DISCONNECT_PAN,
    BLUETOOTH_WORK_DISCONNECT_PHONE,
} bluetooth_work_type_t;

typedef struct
{
    bluetooth_work_type_t type;
    uint8_t address[BLUETOOTH_SERVICE_ADDRESS_SIZE];
} bluetooth_work_t;

static rt_mutex_t bluetooth_lock;
static rt_mq_t bluetooth_work_queue;
static struct rt_thread bluetooth_worker_control;
static rt_thread_t bluetooth_worker;
static void *bluetooth_worker_stack;
static rt_timer_t bluetooth_pan_timer;
static rt_timer_t bluetooth_a2dp_suspend_timer;
static bluetooth_service_status_t bluetooth_status;
static bluetooth_service_event_callback_t bluetooth_callback;
static void *bluetooth_callback_user_data;
static bool bluetooth_connect_avrcp_pending;
static bool bluetooth_requested_enabled;
static bool bluetooth_ble_core_started;

static void bluetooth_service_set_speaker(bool enabled)
{
    rt_err_t result = audio_service_set_speaker_enabled(enabled);

    if (result != RT_EOK)
    {
        LOG_W("Set Bluetooth speaker %s failed: %d",
              enabled ? "on" : "off", result);
    }
}

static void bluetooth_service_clear_music_info(void)
{
    bluetooth_status.metadata_valid = false;
    bluetooth_status.track_title[0] = '\0';
    bluetooth_status.track_artist[0] = '\0';
    bluetooth_status.track_album[0] = '\0';
    bluetooth_status.track_duration_ms = 0U;
    bluetooth_status.track_position_ms = 0U;
}

static void bluetooth_service_copy_music_text(char *destination,
                                               size_t destination_size,
                                               const uint8_t *source,
                                               uint32_t source_size)
{
    size_t copy_size;

    if (destination == RT_NULL || destination_size == 0U)
        return;
    destination[0] = '\0';
    if (source == RT_NULL || source_size == 0U)
        return;

    copy_size = source_size;
    if (copy_size >= destination_size)
        copy_size = destination_size - 1U;
    memcpy(destination, source, copy_size);
    destination[copy_size] = '\0';
}

static uint32_t bluetooth_service_parse_duration_ms(
    const uint8_t *text, uint32_t length)
{
    uint32_t value = 0U;
    uint32_t index;

    for (index = 0U; index < length; index++)
    {
        uint8_t character = text[index];

        if (character < (uint8_t)'0' || character > (uint8_t)'9')
            break;
        if (value > (UINT32_MAX - 9U) / 10U)
            return UINT32_MAX;
        value = value * 10U + (uint32_t)(character - (uint8_t)'0');
    }
    return value;
}

static rt_err_t bluetooth_service_queue_work(const bluetooth_work_t *work)
{
    if (bluetooth_work_queue == RT_NULL || work == RT_NULL)
        return -RT_EINVAL;
    return rt_mq_send(bluetooth_work_queue, (void *)work,
                      sizeof(*work));
}

static void bluetooth_service_copy_status(
    bluetooth_service_status_t *status)
{
    if (status == RT_NULL || bluetooth_lock == RT_NULL)
        return;

    rt_mutex_take(bluetooth_lock, RT_WAITING_FOREVER);
    *status = bluetooth_status;
    rt_mutex_release(bluetooth_lock);
}

static void bluetooth_service_notify(bluetooth_service_event_t event)
{
    bluetooth_service_event_callback_t callback;
    bluetooth_service_status_t status;
    void *user_data;

    rt_mutex_take(bluetooth_lock, RT_WAITING_FOREVER);
    status = bluetooth_status;
    callback = bluetooth_callback;
    user_data = bluetooth_callback_user_data;
    rt_mutex_release(bluetooth_lock);

    if (callback != RT_NULL)
        callback(event, &status, user_data);
}

static bool bluetooth_service_result_ok(bt_err_t result)
{
    return result == BT_EOK || result == BT_ERROR_IN_PROGRESS;
}

static void bluetooth_service_configure(void)
{
    bt_interface_set_local_name((int)strlen(BLUETOOTH_LOCAL_NAME),
                                (void *)BLUETOOTH_LOCAL_NAME);
    bt_interface_set_scan_mode(TRUE, TRUE);
}

static void bluetooth_service_process_work(const bluetooth_work_t *work)
{
    bt_err_t result = BT_EOK;

    switch (work->type)
    {
    case BLUETOOTH_WORK_CONFIGURE:
        bluetooth_service_configure();
        return;

    case BLUETOOTH_WORK_POWER_ON:
        bt_interface_open_bt();
        bluetooth_service_configure();
        return;

    case BLUETOOTH_WORK_POWER_OFF:
        bt_interface_close_bt();
        return;

    case BLUETOOTH_WORK_CONNECT_A2DP:
#ifdef CFG_AV_SNK
        result = bt_interface_conn_ext((unsigned char *)work->address,
                                       BT_PROFILE_A2DP);
#else
        result = BT_ERROR_UNSUPPORTED;
#endif
        break;

    case BLUETOOTH_WORK_CONNECT_AVRCP:
#ifdef CFG_AVRCP
        result = bt_interface_conn_ext((unsigned char *)work->address,
                                       BT_PROFILE_AVRCP);
#else
        result = BT_ERROR_UNSUPPORTED;
#endif
        break;

    case BLUETOOTH_WORK_CONNECT_PAN:
#ifdef CFG_PAN
    {
        bluetooth_service_status_t status;

        bluetooth_service_copy_status(&status);
        if (!status.enabled || !status.phone_connected ||
                !status.pan_enabled || status.pan_connected ||
                !status.peer_address_valid)
        {
            return;
        }
        result = bt_interface_conn_ext(status.peer_address, BT_PROFILE_PAN);
    }
#else
        result = BT_ERROR_UNSUPPORTED;
#endif
        break;

    case BLUETOOTH_WORK_DISCONNECT_PAN:
#ifdef CFG_PAN
    {
        bluetooth_service_status_t status;

        bluetooth_service_copy_status(&status);
        if (!status.peer_address_valid)
            return;
        result = bt_interface_disc_ext(status.peer_address, BT_PROFILE_PAN);
    }
#else
        result = BT_ERROR_UNSUPPORTED;
#endif
        break;

    case BLUETOOTH_WORK_DISCONNECT_PHONE:
#ifdef BSP_BT_CONNECTION_MANAGER
        bt_cm_disconnect_req();
        return;
#else
        result = BT_ERROR_UNSUPPORTED;
#endif
        break;

    default:
        result = BT_ERROR_INPARAM;
        break;
    }

    if (!bluetooth_service_result_ok(result))
        LOG_W("Bluetooth work %u failed: 0x%08x",
              (unsigned int)work->type, (unsigned int)result);
}

static void bluetooth_service_worker_entry(void *parameter)
{
    bluetooth_work_t work;

    (void)parameter;
    while (true)
    {
        if (rt_mq_recv(bluetooth_work_queue, &work, sizeof(work),
                       RT_WAITING_FOREVER) == RT_EOK)
        {
            bluetooth_service_process_work(&work);
        }
    }
}

static void bluetooth_service_pan_timer_callback(void *parameter)
{
    bluetooth_work_t work = {0};

    (void)parameter;
    work.type = BLUETOOTH_WORK_CONNECT_PAN;
    if (bluetooth_service_queue_work(&work) != RT_EOK)
        LOG_W("Queue delayed PAN connection failed");
}

static void bluetooth_service_a2dp_suspend_timer_callback(void *parameter)
{
    bluetooth_service_status_t status;

    (void)parameter;
    bluetooth_service_copy_status(&status);
    if (status.a2dp_connected &&
            status.music_state == BLUETOOTH_MUSIC_PAUSED)
    {
        bluetooth_service_set_speaker(false);
    }
}

static void bluetooth_service_schedule_pan(void)
{
    bluetooth_service_status_t status;

    bluetooth_service_copy_status(&status);
    if (!status.pan_enabled || !status.phone_connected ||
            status.pan_connected || !status.peer_address_valid)
    {
        return;
    }

    rt_timer_stop(bluetooth_pan_timer);
    rt_timer_start(bluetooth_pan_timer);
}

static void bluetooth_service_set_peer(
    const bt_notify_device_mac_t *peer)
{
    if (peer == RT_NULL)
        return;
    memcpy(bluetooth_status.peer_address, peer->addr,
           sizeof(bluetooth_status.peer_address));
    bluetooth_status.peer_address_valid = true;
    bluetooth_status.phone_connected = true;
}

static void bluetooth_service_clear_connection(void)
{
    bluetooth_status.phone_connected = false;
    bluetooth_status.pan_connected = false;
    bluetooth_status.a2dp_connected = false;
    bluetooth_status.avrcp_connected = false;
    bluetooth_status.peer_address_valid = false;
    bluetooth_status.music_state = BLUETOOTH_MUSIC_STOPPED;
    bluetooth_service_clear_music_info();
    bluetooth_connect_avrcp_pending = false;
}

static int bluetooth_service_common_event(uint16_t event_id, uint8_t *data)
{
    bluetooth_service_event_t service_event =
        BLUETOOTH_SERVICE_EVENT_PHONE_CHANGED;
    bool disable_speaker = false;
    bool schedule_pan = false;
    bool configure = false;
    bool power_off = false;
    bool notify = false;

    rt_mutex_take(bluetooth_lock, RT_WAITING_FOREVER);
    switch (event_id)
    {
    case BT_NOTIFY_COMMON_BT_STACK_READY:
        bluetooth_status.stack_ready = true;
        bluetooth_status.enabled = bluetooth_requested_enabled;
        service_event = BLUETOOTH_SERVICE_EVENT_READY;
        configure = bluetooth_requested_enabled;
        power_off = !bluetooth_requested_enabled;
        notify = true;
        break;

    case BT_NOTIFY_COMMON_CLOSE_COMPLETE:
        bluetooth_status.enabled = false;
        bluetooth_status.pan_enabled = false;
        bluetooth_service_clear_connection();
        disable_speaker = true;
        service_event = BLUETOOTH_SERVICE_EVENT_POWER_CHANGED;
        notify = true;
        break;

    case BT_NOTIFY_COMMON_ACL_CONNECTED:
        if (data != RT_NULL)
        {
            bt_notify_device_acl_conn_info_t *info =
                (bt_notify_device_acl_conn_info_t *)data;

            if (info->res == BTS2_SUCC)
            {
                bluetooth_service_set_peer(&info->mac);
                schedule_pan = true;
                notify = true;
            }
        }
        break;

    case BT_NOTIFY_COMMON_ENCRYPTION:
        if (data != RT_NULL)
        {
            bluetooth_service_set_peer((bt_notify_device_mac_t *)data);
            schedule_pan = true;
            notify = true;
        }
        break;

    case BT_NOTIFY_COMMON_PAIR_IND:
        if (data != RT_NULL)
        {
            bt_notify_device_base_info_t *info =
                (bt_notify_device_base_info_t *)data;

            if (info->res == BTS2_SUCC)
            {
                bluetooth_service_set_peer(&info->mac);
                schedule_pan = true;
                notify = true;
            }
        }
        break;

    case BT_NOTIFY_COMMON_ACL_DISCONNECTED:
        bluetooth_service_clear_connection();
        disable_speaker = true;
        notify = true;
        break;

    default:
        break;
    }
    rt_mutex_release(bluetooth_lock);

    if (disable_speaker)
    {
        if (bluetooth_a2dp_suspend_timer != RT_NULL)
            rt_timer_stop(bluetooth_a2dp_suspend_timer);
        bluetooth_service_set_speaker(false);
    }
    if (configure)
    {
        bluetooth_work_t work = {0};

        work.type = BLUETOOTH_WORK_CONFIGURE;
        bluetooth_service_queue_work(&work);
    }
    else if (power_off)
    {
        bluetooth_work_t work = {0};

        work.type = BLUETOOTH_WORK_POWER_OFF;
        bluetooth_service_queue_work(&work);
    }
    if (schedule_pan)
        bluetooth_service_schedule_pan();
    if (notify)
        bluetooth_service_notify(service_event);
    return 0;
}

static int bluetooth_service_a2dp_event(uint16_t event_id, uint8_t *data)
{
    bool connect_avrcp = false;
    bool enable_speaker = false;
    bool disable_speaker = false;
    bool cancel_speaker_disable = false;
    bool schedule_speaker_disable = false;
    bool notify = false;

    rt_mutex_take(bluetooth_lock, RT_WAITING_FOREVER);
    switch (event_id)
    {
    case BT_NOTIFY_A2DP_PROFILE_CONNECTED:
        if (data != RT_NULL)
        {
            bt_notify_profile_state_info_t *info =
                (bt_notify_profile_state_info_t *)data;

            if (info->res == BTS2_SUCC)
            {
                bluetooth_service_set_peer(&info->mac);
                bluetooth_status.a2dp_connected = true;
                connect_avrcp = bluetooth_connect_avrcp_pending;
                bluetooth_connect_avrcp_pending = false;
                notify = true;
            }
        }
        break;

    case BT_NOTIFY_A2DP_PROFILE_DISCONNECTED:
        bluetooth_status.a2dp_connected = false;
        bluetooth_status.music_state = BLUETOOTH_MUSIC_STOPPED;
        bluetooth_service_clear_music_info();
        cancel_speaker_disable = true;
        disable_speaker = true;
        notify = true;
        break;

    case BT_NOTIFY_A2DP_START_IND:
        bluetooth_status.music_state = BLUETOOTH_MUSIC_PLAYING;
        cancel_speaker_disable = true;
        enable_speaker = true;
        notify = true;
        break;

    case BT_NOTIFY_A2DP_SUSPEND_IND:
        bluetooth_status.music_state = BLUETOOTH_MUSIC_PAUSED;
        schedule_speaker_disable = true;
        notify = true;
        break;

    default:
        break;
    }
    rt_mutex_release(bluetooth_lock);

    if (cancel_speaker_disable && bluetooth_a2dp_suspend_timer != RT_NULL)
        rt_timer_stop(bluetooth_a2dp_suspend_timer);
    if (enable_speaker)
        bluetooth_service_set_speaker(true);
    else if (disable_speaker)
        bluetooth_service_set_speaker(false);
    else if (schedule_speaker_disable &&
             bluetooth_a2dp_suspend_timer != RT_NULL)
    {
        rt_timer_stop(bluetooth_a2dp_suspend_timer);
        rt_timer_start(bluetooth_a2dp_suspend_timer);
    }
    if (connect_avrcp && data != RT_NULL)
    {
        bt_notify_profile_state_info_t *info =
            (bt_notify_profile_state_info_t *)data;
        bluetooth_work_t work = {0};

        work.type = BLUETOOTH_WORK_CONNECT_AVRCP;
        memcpy(work.address, info->mac.addr, sizeof(work.address));
        bluetooth_service_queue_work(&work);
    }
    if (notify)
    {
        bluetooth_service_notify(
            event_id == BT_NOTIFY_A2DP_START_IND ||
            event_id == BT_NOTIFY_A2DP_SUSPEND_IND ?
            BLUETOOTH_SERVICE_EVENT_MUSIC_CHANGED :
            BLUETOOTH_SERVICE_EVENT_PHONE_CHANGED);
    }
    return 0;
}

static int bluetooth_service_avrcp_event(uint16_t event_id, uint8_t *data,
                                         uint16_t data_len)
{
    bool connected = false;
    bool notify = false;
    int local_volume = -1;

    rt_mutex_take(bluetooth_lock, RT_WAITING_FOREVER);
    switch (event_id)
    {
    case BT_NOTIFY_AVRCP_PROFILE_CONNECTED:
        if (data != RT_NULL)
        {
            bt_notify_profile_state_info_t *info =
                (bt_notify_profile_state_info_t *)data;

            if (info->res == BTS2_SUCC)
            {
                bluetooth_service_set_peer(&info->mac);
                bluetooth_status.avrcp_connected = true;
                connected = true;
                notify = true;
            }
        }
        break;

    case BT_NOTIFY_AVRCP_PROFILE_DISCONNECTED:
        bluetooth_status.avrcp_connected = false;
        bluetooth_status.music_state = BLUETOOTH_MUSIC_STOPPED;
        bluetooth_service_clear_music_info();
        notify = true;
        break;

    case BT_NOTIFY_AVRCP_MUSIC_DETAIL_INFO:
        if (data != RT_NULL &&
                data_len >= sizeof(bt_notify_avrcp_music_detail_t))
        {
            const bt_notify_avrcp_music_detail_t *detail =
                (const bt_notify_avrcp_music_detail_t *)data;
            uint32_t duration_size = detail->detail_info.duration.size;

            if (duration_size >
                    (uint32_t)sizeof(detail->detail_info.duration.play_time))
            {
                duration_size =
                    (uint32_t)sizeof(detail->detail_info.duration.play_time);
            }

            bluetooth_service_copy_music_text(
                bluetooth_status.track_title,
                sizeof(bluetooth_status.track_title),
                detail->detail_info.song_name.song_name,
                detail->detail_info.song_name.size);
            bluetooth_service_copy_music_text(
                bluetooth_status.track_artist,
                sizeof(bluetooth_status.track_artist),
                detail->detail_info.singer_name.singer_name,
                detail->detail_info.singer_name.size);
            bluetooth_service_copy_music_text(
                bluetooth_status.track_album,
                sizeof(bluetooth_status.track_album),
                detail->detail_info.album_info.album_name,
                detail->detail_info.album_info.size);
            bluetooth_status.track_duration_ms =
                bluetooth_service_parse_duration_ms(
                    detail->detail_info.duration.play_time,
                    duration_size);
            bluetooth_status.metadata_valid =
                bluetooth_status.track_title[0] != '\0' ||
                bluetooth_status.track_artist[0] != '\0' ||
                bluetooth_status.track_album[0] != '\0';
            notify = true;
        }
        break;

    case BT_NOTIFY_AVRCP_PLAY_STATUS:
        if (data != RT_NULL && data_len >= sizeof(uint8_t))
        {
            if (data[0] == 0U)
                bluetooth_status.music_state = BLUETOOTH_MUSIC_PLAYING;
            else if (data[0] == 1U)
                bluetooth_status.music_state = BLUETOOTH_MUSIC_PAUSED;
            else
                bluetooth_status.music_state = BLUETOOTH_MUSIC_STOPPED;
            notify = true;
        }
        break;

    case BT_NOTIFY_AVRCP_SONG_PROGREAS_STATUS:
        if (data != RT_NULL && data_len >= sizeof(uint32_t))
        {
            uint32_t position;

            memcpy(&position, data, sizeof(position));
            bluetooth_status.track_position_ms = position;
            notify = true;
        }
        break;

    case BT_NOTIFY_AVRCP_TRACK_CHANGE_STATUS:
        bluetooth_service_clear_music_info();
        notify = true;
        break;

    case BT_NOTIFY_AVRCP_ABSOLUTE_VOLUME:
        if (data != RT_NULL && data_len >= sizeof(uint8_t))
        {
            local_volume = (int)
                bt_interface_avrcp_abs_vol_2_local_vol(
                    data[0], BLUETOOTH_SERVICE_VOLUME_MAX);
        }
        break;

    default:
        break;
    }
    rt_mutex_release(bluetooth_lock);

    if (local_volume >= 0)
        audio_service_set_volume(local_volume);
    if (connected && data != RT_NULL)
    {
        bt_notify_profile_state_info_t *info =
            (bt_notify_profile_state_info_t *)data;

        bt_interface_set_avrcp_role_ext(&info->mac, AVRCP_CT);
        bt_interface_avrcp_playback_register_request(&info->mac);
        bt_interface_playback_pos_register_request(&info->mac);
        bt_interface_track_change_register_request(&info->mac);
        bt_interface_avrcp_get_play_status_request(&info->mac);
    }
    if (notify)
    {
        bluetooth_service_notify(
            event_id == BT_NOTIFY_AVRCP_PLAY_STATUS ||
            event_id == BT_NOTIFY_AVRCP_MUSIC_DETAIL_INFO ||
            event_id == BT_NOTIFY_AVRCP_SONG_PROGREAS_STATUS ||
            event_id == BT_NOTIFY_AVRCP_TRACK_CHANGE_STATUS ?
            BLUETOOTH_SERVICE_EVENT_MUSIC_CHANGED :
            BLUETOOTH_SERVICE_EVENT_PHONE_CHANGED);
    }
    return 0;
}

static int bluetooth_service_pan_event(uint16_t event_id, uint8_t *data)
{
    bool disconnect_pan = false;

    (void)data;
    rt_mutex_take(bluetooth_lock, RT_WAITING_FOREVER);
    if (event_id == BT_NOTIFY_PAN_PROFILE_CONNECTED)
    {
        bluetooth_status.pan_connected = true;
        disconnect_pan = !bluetooth_status.pan_enabled;
    }
    else if (event_id == BT_NOTIFY_PAN_PROFILE_DISCONNECTED)
        bluetooth_status.pan_connected = false;
    else
    {
        rt_mutex_release(bluetooth_lock);
        return 0;
    }
    rt_mutex_release(bluetooth_lock);

    rt_timer_stop(bluetooth_pan_timer);
    if (disconnect_pan)
    {
        bluetooth_work_t work = {0};

        work.type = BLUETOOTH_WORK_DISCONNECT_PAN;
        bluetooth_service_queue_work(&work);
    }
    bluetooth_service_notify(BLUETOOTH_SERVICE_EVENT_PAN_CHANGED);
    return 0;
}

static int bluetooth_service_stack_event(uint16_t type, uint16_t event_id,
                                         uint8_t *data, uint16_t data_len)
{
    if (bluetooth_lock == RT_NULL)
        return 0;

    if (type == BT_NOTIFY_COMMON)
        return bluetooth_service_common_event(event_id, data);
    if (type == BT_NOTIFY_A2DP)
        return bluetooth_service_a2dp_event(event_id, data);
    if (type == BT_NOTIFY_AVRCP)
        return bluetooth_service_avrcp_event(event_id, data, data_len);
    if (type == BT_NOTIFY_PAN)
        return bluetooth_service_pan_event(event_id, data);
    return 0;
}

static rt_err_t bluetooth_service_check_operational(void)
{
    bluetooth_service_status_t status;

    bluetooth_service_copy_status(&status);
    if (!status.initialized)
        return -RT_ERROR;
    if (!status.stack_ready || !status.enabled)
        return -RT_EBUSY;
    return RT_EOK;
}

static rt_err_t bluetooth_service_music_check(void)
{
    bluetooth_service_status_t status;
    rt_err_t result = bluetooth_service_check_operational();

    if (result != RT_EOK)
        return result;
    bluetooth_service_copy_status(&status);
    return status.avrcp_connected ? RT_EOK : -RT_ERROR;
}

static void bluetooth_service_release_resources(void)
{
    if (bluetooth_worker != RT_NULL)
    {
        rt_thread_detach(bluetooth_worker);
        bluetooth_worker = RT_NULL;
    }
    if (bluetooth_worker_stack != RT_NULL)
    {
        t_sf32_psram_free(bluetooth_worker_stack);
        bluetooth_worker_stack = RT_NULL;
    }
    if (bluetooth_pan_timer != RT_NULL)
    {
        rt_timer_delete(bluetooth_pan_timer);
        bluetooth_pan_timer = RT_NULL;
    }
    if (bluetooth_a2dp_suspend_timer != RT_NULL)
    {
        rt_timer_delete(bluetooth_a2dp_suspend_timer);
        bluetooth_a2dp_suspend_timer = RT_NULL;
    }
    if (bluetooth_work_queue != RT_NULL)
    {
        rt_mq_delete(bluetooth_work_queue);
        bluetooth_work_queue = RT_NULL;
    }
    if (bluetooth_lock != RT_NULL)
    {
        rt_mutex_delete(bluetooth_lock);
        bluetooth_lock = RT_NULL;
    }
}
#endif /* BLUETOOTH */

rt_err_t bluetooth_service_init(void)
{
#ifdef BLUETOOTH
    bt_interface_status_t register_result;
    rt_err_t result;

    if (bluetooth_status.initialized)
        return RT_EOK;

    bluetooth_lock = rt_mutex_create("bt_srv", RT_IPC_FLAG_PRIO);
    bluetooth_work_queue = rt_mq_create("bt_work", sizeof(bluetooth_work_t),
                                        BLUETOOTH_WORK_QUEUE_DEPTH,
                                        RT_IPC_FLAG_FIFO);
    bluetooth_pan_timer = rt_timer_create(
        "bt_pan", bluetooth_service_pan_timer_callback, RT_NULL,
        rt_tick_from_millisecond(BLUETOOTH_PAN_CONNECT_DELAY_MS),
        RT_TIMER_FLAG_ONE_SHOT | RT_TIMER_FLAG_SOFT_TIMER);
    bluetooth_a2dp_suspend_timer = rt_timer_create(
        "bt_a2dp", bluetooth_service_a2dp_suspend_timer_callback, RT_NULL,
        rt_tick_from_millisecond(BLUETOOTH_A2DP_SUSPEND_GRACE_MS),
        RT_TIMER_FLAG_ONE_SHOT | RT_TIMER_FLAG_SOFT_TIMER);
    bluetooth_worker_stack = t_sf32_psram_alloc(
        BLUETOOTH_WORKER_STACK_SIZE);
    if (bluetooth_lock == RT_NULL || bluetooth_work_queue == RT_NULL ||
            bluetooth_pan_timer == RT_NULL ||
            bluetooth_a2dp_suspend_timer == RT_NULL ||
            bluetooth_worker_stack == RT_NULL)
    {
        LOG_E("Create Bluetooth service resources failed");
        bluetooth_service_release_resources();
        return -RT_ENOMEM;
    }

    result = rt_thread_init(&bluetooth_worker_control, "bt_service",
                            bluetooth_service_worker_entry, RT_NULL,
                            bluetooth_worker_stack,
                            BLUETOOTH_WORKER_STACK_SIZE,
                            BLUETOOTH_WORKER_PRIORITY, 10U);
    if (result != RT_EOK)
    {
        LOG_E("Initialize Bluetooth worker failed: %d", result);
        bluetooth_service_release_resources();
        return result;
    }
    bluetooth_worker = &bluetooth_worker_control;

    register_result = bt_interface_register_bt_event_notify_callback(
        bluetooth_service_stack_event);
    if (register_result != BT_INTERFACE_STATUS_OK &&
            register_result != BT_INTERFACE_STATUS_ALREADY_EXIST)
    {
        LOG_E("Register Bluetooth callback failed: %d", register_result);
        bluetooth_service_release_resources();
        return -RT_ERROR;
    }

    result = rt_thread_startup(bluetooth_worker);
    if (result != RT_EOK)
    {
        bt_interface_unregister_bt_event_notify_callback(
            bluetooth_service_stack_event);
        bluetooth_service_release_resources();
        return result;
    }

    rt_mutex_take(bluetooth_lock, RT_WAITING_FOREVER);
    memset(&bluetooth_status, 0, sizeof(bluetooth_status));
    bluetooth_status.initialized = true;
    bluetooth_status.enabled = false;
    bluetooth_requested_enabled = false;
    bluetooth_ble_core_started = false;
    rt_mutex_release(bluetooth_lock);

    LOG_I("Bluetooth service ready, BLE core defaults off");
    return RT_EOK;
#else
    LOG_E("Bluetooth stack support must be enabled");
    return -RT_ENOSYS;
#endif
}

bool bluetooth_service_is_ready(void)
{
#ifdef BLUETOOTH
    bluetooth_service_status_t status;

    bluetooth_service_copy_status(&status);
    return status.initialized && status.stack_ready;
#else
    return false;
#endif
}

bool bluetooth_service_is_enabled(void)
{
#ifdef BLUETOOTH
    bluetooth_service_status_t status;

    bluetooth_service_copy_status(&status);
    return status.enabled;
#else
    return false;
#endif
}

bool bluetooth_service_is_pan_enabled(void)
{
#ifdef BLUETOOTH
    bluetooth_service_status_t status;

    bluetooth_service_copy_status(&status);
    return status.pan_enabled;
#else
    return false;
#endif
}

rt_err_t bluetooth_service_get_status(bluetooth_service_status_t *status)
{
    if (status == RT_NULL)
        return -RT_EINVAL;
#ifdef BLUETOOTH
    bluetooth_service_copy_status(status);
    return RT_EOK;
#else
    memset(status, 0, sizeof(*status));
    return -RT_ENOSYS;
#endif
}

rt_err_t bluetooth_service_set_event_callback(
    bluetooth_service_event_callback_t callback,
    void *user_data)
{
#ifdef BLUETOOTH
    if (bluetooth_lock == RT_NULL)
        return -RT_ERROR;
    rt_mutex_take(bluetooth_lock, RT_WAITING_FOREVER);
    bluetooth_callback = callback;
    bluetooth_callback_user_data = user_data;
    rt_mutex_release(bluetooth_lock);
    return RT_EOK;
#else
    (void)callback;
    (void)user_data;
    return -RT_ENOSYS;
#endif
}

rt_err_t bluetooth_service_set_enabled(bool enabled)
{
#ifdef BLUETOOTH
    bluetooth_service_status_t status;
    bluetooth_work_t work = {0};
    rt_err_t result;
    bool start_ble_core = false;

    bluetooth_service_copy_status(&status);
    if (!status.initialized)
        return -RT_EBUSY;
    if (enabled)
    {
        rt_mutex_take(bluetooth_lock, RT_WAITING_FOREVER);
        if (!bluetooth_ble_core_started)
        {
            bluetooth_ble_core_started = true;
            start_ble_core = true;
        }
        bluetooth_requested_enabled = true;
        rt_mutex_release(bluetooth_lock);
        if (start_ble_core)
        {
            sifli_ble_enable();
            LOG_I("BLE core startup requested");
        }
    }
    if (!status.stack_ready)
    {
        rt_mutex_take(bluetooth_lock, RT_WAITING_FOREVER);
        bluetooth_requested_enabled = enabled;
        bluetooth_status.enabled = enabled;
        rt_mutex_release(bluetooth_lock);
        return RT_EOK;
    }
    if (status.enabled == enabled)
        return RT_EOK;

    work.type = enabled ? BLUETOOTH_WORK_POWER_ON :
                         BLUETOOTH_WORK_POWER_OFF;
    result = bluetooth_service_queue_work(&work);
    if (result != RT_EOK)
        return result;

    rt_mutex_take(bluetooth_lock, RT_WAITING_FOREVER);
    bluetooth_requested_enabled = enabled;
    bluetooth_status.enabled = enabled;
    if (!enabled)
    {
        rt_timer_stop(bluetooth_pan_timer);
        bluetooth_status.pan_enabled = false;
        bluetooth_status.pan_connected = false;
    }
    rt_mutex_release(bluetooth_lock);
    bluetooth_service_notify(BLUETOOTH_SERVICE_EVENT_POWER_CHANGED);
    return RT_EOK;
#else
    (void)enabled;
    return -RT_ENOSYS;
#endif
}

rt_err_t bluetooth_service_connect_phone(
    const uint8_t address[BLUETOOTH_SERVICE_ADDRESS_SIZE])
{
#if defined(BLUETOOTH) && defined(CFG_AV_SNK)
    bluetooth_work_t work = {0};
    rt_err_t result;

    if (address == RT_NULL)
        return -RT_EINVAL;
    result = bluetooth_service_check_operational();
    if (result != RT_EOK)
        return result;
    work.type = BLUETOOTH_WORK_CONNECT_A2DP;
    memcpy(work.address, address, sizeof(work.address));
    rt_mutex_take(bluetooth_lock, RT_WAITING_FOREVER);
    bluetooth_connect_avrcp_pending = true;
    rt_mutex_release(bluetooth_lock);
    result = bluetooth_service_queue_work(&work);
    if (result != RT_EOK)
    {
        rt_mutex_take(bluetooth_lock, RT_WAITING_FOREVER);
        bluetooth_connect_avrcp_pending = false;
        rt_mutex_release(bluetooth_lock);
    }
    return result;
#else
    (void)address;
    return -RT_ENOSYS;
#endif
}

rt_err_t bluetooth_service_disconnect_phone(void)
{
#ifdef BLUETOOTH
    bluetooth_service_status_t status;
    bluetooth_work_t work = {0};

    bluetooth_service_copy_status(&status);
    if (!status.phone_connected)
        return -RT_ERROR;
    work.type = BLUETOOTH_WORK_DISCONNECT_PHONE;
    return bluetooth_service_queue_work(&work);
#else
    return -RT_ENOSYS;
#endif
}

rt_err_t bluetooth_service_set_pan_enabled(bool enabled)
{
#if defined(BLUETOOTH) && defined(CFG_PAN)
    bluetooth_service_status_t status;
    bluetooth_work_t work = {0};
    rt_err_t result;

    result = bluetooth_service_check_operational();
    if (result != RT_EOK)
        return result;

    rt_mutex_take(bluetooth_lock, RT_WAITING_FOREVER);
    bluetooth_status.pan_enabled = enabled;
    status = bluetooth_status;
    rt_mutex_release(bluetooth_lock);

    if (enabled && status.phone_connected && !status.pan_connected)
        bluetooth_service_schedule_pan();
    else if (!enabled)
    {
        rt_timer_stop(bluetooth_pan_timer);
        if (status.pan_connected)
        {
            work.type = BLUETOOTH_WORK_DISCONNECT_PAN;
            result = bluetooth_service_queue_work(&work);
            if (result != RT_EOK)
                return result;
        }
    }
    bluetooth_service_notify(BLUETOOTH_SERVICE_EVENT_PAN_CHANGED);
    return RT_EOK;
#else
    (void)enabled;
    return -RT_ENOSYS;
#endif
}

rt_err_t bluetooth_service_music_play(void)
{
#if defined(BLUETOOTH) && defined(CFG_AVRCP)
    rt_err_t result = bluetooth_service_music_check();

    if (result == RT_EOK)
        bt_interface_avrcp_play();
    return result;
#else
    return -RT_ENOSYS;
#endif
}

rt_err_t bluetooth_service_music_pause(void)
{
#if defined(BLUETOOTH) && defined(CFG_AVRCP)
    rt_err_t result = bluetooth_service_music_check();

    if (result == RT_EOK)
        bt_interface_avrcp_pause();
    return result;
#else
    return -RT_ENOSYS;
#endif
}

rt_err_t bluetooth_service_prepare_local_audio(void)
{
#if defined(BLUETOOTH) && defined(CFG_AV_SNK)
    bluetooth_service_status_t status;
    rt_err_t result = RT_EOK;

    bluetooth_service_copy_status(&status);
    if (!status.a2dp_connected)
        return RT_EOK;
    if (status.music_state == BLUETOOTH_MUSIC_PLAYING)
        result = bluetooth_service_music_pause();
    bt_interface_release_a2dp();
    return result;
#else
    return -RT_ENOSYS;
#endif
}

rt_err_t bluetooth_service_music_stop(void)
{
#if defined(BLUETOOTH) && defined(CFG_AVRCP)
    rt_err_t result = bluetooth_service_music_check();

    if (result == RT_EOK)
        bt_interface_avrcp_stop();
    return result;
#else
    return -RT_ENOSYS;
#endif
}

rt_err_t bluetooth_service_music_next(void)
{
#if defined(BLUETOOTH) && defined(CFG_AVRCP)
    rt_err_t result = bluetooth_service_music_check();

    if (result == RT_EOK)
        bt_interface_avrcp_next();
    return result;
#else
    return -RT_ENOSYS;
#endif
}

rt_err_t bluetooth_service_music_previous(void)
{
#if defined(BLUETOOTH) && defined(CFG_AVRCP)
    rt_err_t result = bluetooth_service_music_check();

    if (result == RT_EOK)
        bt_interface_avrcp_previous();
    return result;
#else
    return -RT_ENOSYS;
#endif
}

rt_err_t bluetooth_service_music_volume_up(void)
{
#if defined(BLUETOOTH) && defined(CFG_AVRCP)
    rt_err_t result = bluetooth_service_music_check();
    int volume;

    if (result == RT_EOK)
    {
        volume = audio_service_get_volume();
        if (volume < (int)BLUETOOTH_SERVICE_VOLUME_MAX)
            result = bluetooth_service_music_set_volume((uint8_t)(volume + 1));
    }
    return result;
#else
    return -RT_ENOSYS;
#endif
}

rt_err_t bluetooth_service_music_volume_down(void)
{
#if defined(BLUETOOTH) && defined(CFG_AVRCP)
    rt_err_t result = bluetooth_service_music_check();
    int volume;

    if (result == RT_EOK)
    {
        volume = audio_service_get_volume();
        if (volume > 0)
            result = bluetooth_service_music_set_volume((uint8_t)(volume - 1));
    }
    return result;
#else
    return -RT_ENOSYS;
#endif
}

rt_err_t bluetooth_service_music_set_volume(uint8_t volume)
{
#if defined(BLUETOOTH) && defined(CFG_AVRCP)
    uint8_t absolute_volume;
    bt_err_t bt_result;
    rt_err_t result;
    bluetooth_service_status_t status;
    bt_notify_device_mac_t peer;

    if (volume > BLUETOOTH_SERVICE_VOLUME_MAX)
        return -RT_EINVAL;
    result = bluetooth_service_music_check();
    if (result != RT_EOK)
        return result;

    bluetooth_service_copy_status(&status);
    if (!status.peer_address_valid)
        return -RT_ERROR;
    memcpy(peer.addr, status.peer_address, sizeof(peer.addr));

    absolute_volume = bt_interface_avrcp_local_vol_2_abs_vol(
        volume, BLUETOOTH_SERVICE_VOLUME_MAX);
    bt_result = bt_interface_avrcp_set_absolute_volume_as_ct_role_ext(
        &peer, absolute_volume);
    if (!bluetooth_service_result_ok(bt_result))
        return -RT_ERROR;
    return audio_service_set_volume(volume);
#else
    (void)volume;
    return -RT_ENOSYS;
#endif
}
