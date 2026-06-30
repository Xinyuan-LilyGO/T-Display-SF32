#include "bluetooth_manger.h"
#include "ulog.h"
#include "rtthread.h"
#include "bf0_hal.h"
#include "drv_io.h"
#include "stdio.h"
#include "string.h"
#include "rtc_time.h"
#include "audio_manager_lib.h"

#define BT_APP_READY 1

music_info_t current_music;
static bt_app_t g_bt_app_env;
static rt_thread_t bt_app_task = RT_NULL;

bt_app_t *bt_app_get_env(void)
{
    return &g_bt_app_env;
}

/**
 * @brief  Main program
 * @param  None
 * @retval 0 if success, otherwise failure number
 */
#ifdef BT_DEVICE_NAME
static const char *local_name = BT_DEVICE_NAME;
#else
static const char *local_name = "T-SF32-Display";
#endif

void bt_app_thread_entry(void *parameter);
static void parse_song_title(uint8_t *data, uint16_t len);
static void parse_song_artist(uint8_t *data, uint16_t len);
static void parse_song_album(uint8_t *data, uint16_t len);
static void parse_song_playtime(uint8_t *data, uint16_t len);
static int bt_app_interface_event_handle(uint16_t type, uint16_t event_id, uint8_t *data, uint16_t data_len);
static void bt_app_connect_pan_timeout_handle(void *parameter);

rt_err_t sf32_bluetooth_init(void)
{
    uint32_t value;
    // audio_pa_open();
    g_bt_app_env.g_bt_app_mb = rt_mb_create("bt_app", 8, RT_IPC_FLAG_FIFO);
    RT_ASSERT(g_bt_app_env.g_bt_app_mb);
    // #ifdef BSP_BT_CONNECTION_MANAGER
    //     bt_cm_set_profile_target(BT_BASIC_PROFILE, BT_LINK_PHONE, 1);
    // #endif // BSP_BT_CONNECTION_MANAGER

    bt_interface_register_bt_event_notify_callback(bt_app_interface_event_handle);

#ifdef ZBT
    LOG_I("ZBT mode, enable Bluetooth stack");
    bt_enable(NULL);
#else
    LOG_I("BLE mode, enable Ble stack");
    sifli_ble_enable();
#endif

    // Wait for stack/profile ready.
    if (RT_EOK == rt_mb_recv(g_bt_app_env.g_bt_app_mb, (rt_uint32_t *)&value, 8000) && value == BT_APP_READY)
    {
        LOG_I("BT/BLE stack and profile ready");
        bt_interface_set_local_name(strlen(local_name), (void *)local_name);
    }
    else
    {
        LOG_E("BT/BLE stack and profile init failed");
    }

    current_music.is_playing = 0;
    strcpy(current_music.title, "No song");
    strcpy(current_music.artist, "No artist");
    strcpy(current_music.album, "No album");
    current_music.current_ms = 0;
    current_music.duration_ms = 0;

    // Update Bluetooth name
    bt_app_task = rt_thread_create("bt_app", bt_app_thread_entry, RT_NULL, 2 * 1024, RT_THREAD_PRIORITY_MIDDLE, 10);
    rt_thread_startup(bt_app_task);
    return RT_EOK;
}

void bt_app_thread_entry(void *parameter)
{
    while (1)
    {
        uint32_t value;
        rt_mb_recv(g_bt_app_env.g_bt_app_mb, (rt_uint32_t *)&value, RT_WAITING_FOREVER);
        LOG_D("bt_app_thread_entry %d", value);
        switch (value)
        {
        case BT_APP_CONNECT_PAN:
        {
            if (g_bt_app_env.bt_connected)
                bt_interface_conn_ext((char *)&g_bt_app_env.bd_addr, BT_PROFILE_PAN);
            break;
        }
        case BT_APP_AUDIO_PAUSE:
        {
            audio_pa_close();
            bt_interface_avrcp_pause();
            break;
        }
        case BT_APP_AUDIO_PLAY:
        {
            audio_pa_open();
            bt_interface_avrcp_play();
            break;
        }
        case BT_APP_AUDIO_LAST:
        {
            bt_interface_avrcp_previous();
            break;
        }
        case BT_APP_AUDIO_NEXT:
        {
            bt_interface_avrcp_next();
            break;
        }
        default:
            LOG_I("bt_app_thread_entry unknown value: %d", value);
            break;
        }
    }
}

void bt_app_connect_pan_timeout_handle(void *parameter)
{
    if ((g_bt_app_env.g_bt_app_mb != NULL) && (g_bt_app_env.bt_connected))
        rt_mb_send(g_bt_app_env.g_bt_app_mb, BT_APP_CONNECT_PAN);
}

static int bt_common_event_handler(uint16_t event_id, uint8_t *data, uint16_t data_len)
{
    bt_app_t *env = bt_app_get_env();
    int pan_conn = 0;

    switch (event_id)
    {
    case BT_NOTIFY_COMMON_BT_STACK_READY:
        rt_mb_send(env->g_bt_app_mb, BT_APP_READY);
        break;

    case BT_NOTIFY_COMMON_ACL_CONNECTED:
        break;

    case BT_NOTIFY_COMMON_ACL_DISCONNECTED:
    {
        bt_notify_device_base_info_t *info = (bt_notify_device_base_info_t *)data;
        LOG_I("disconnected(0x%.2x:%.2x:%.2x:%.2x:%.2x:%.2x) res %d", info->mac.addr[5],
              info->mac.addr[4], info->mac.addr[3], info->mac.addr[2],
              info->mac.addr[1], info->mac.addr[0], info->res);
        env->bt_connected = FALSE;
        memset(&env->bd_addr, 0xFF, sizeof(env->bd_addr));
        if (env->pan_connect_timer)
            rt_timer_stop(env->pan_connect_timer);
        audio_pa_close();

        env->is_a2dp_connected = 0;
        env->is_abs_enabled = 0;
        env->bluetooth_pan = false;
        break;
    }

    case BT_NOTIFY_COMMON_ENCRYPTION:
    {
        bt_notify_device_mac_t *mac = (bt_notify_device_mac_t *)data;
        LOG_I("Encryption competed");
        env->bd_addr = *mac;
        pan_conn = 1;
        break;
    }

    case BT_NOTIFY_COMMON_PAIR_IND:
    {
        bt_notify_device_base_info_t *info = (bt_notify_device_base_info_t *)data;
        LOG_I("Pairing completed %d", info->res);
        if (info->res == BTS2_SUCC)
        {
            env->bd_addr = info->mac;
            pan_conn = 1;
        }
        break;
    }

    default:
        break;
    }

    if (pan_conn)
    {
        LOG_I("bd addr 0x%.2x:%.2x:%.2x:%.2x:%.2x:%.2x\n", env->bd_addr.addr[5],
              env->bd_addr.addr[4], env->bd_addr.addr[3],
              env->bd_addr.addr[2], env->bd_addr.addr[1], env->bd_addr.addr[0]);
        env->bt_connected = TRUE;
        if (!env->pan_connect_timer)
            env->pan_connect_timer = rt_timer_create("connect_pan", bt_app_connect_pan_timeout_handle, (void *)env,
                                                     rt_tick_from_millisecond(PAN_TIMER_MS), RT_TIMER_FLAG_SOFT_TIMER);
        else
            rt_timer_stop(env->pan_connect_timer);
        rt_timer_start(env->pan_connect_timer);
    }
    return 0;
}

static int bt_pan_event_handler(uint16_t event_id, uint8_t *data, uint16_t data_len)
{
    bt_app_t *env = bt_app_get_env();

    switch (event_id)
    {
    case BT_NOTIFY_PAN_PROFILE_CONNECTED:
        LOG_I("pan connect successed \n");
        if (env->pan_connect_timer)
            rt_timer_stop(env->pan_connect_timer);
        g_bt_app_env.bluetooth_pan = true;
        break;

    case BT_NOTIFY_PAN_PROFILE_DISCONNECTED:
        LOG_I("pan disconnect with remote device\n");
        g_bt_app_env.bluetooth_pan = false;
        if (env->pan_connect_timer)
            rt_timer_stop(env->pan_connect_timer);
        break;

    default:
        break;
    }
    return 0;
}

static int bt_a2dp_event_handler(uint16_t event_id, uint8_t *data, uint16_t data_len)
{
    bt_app_t *env = bt_app_get_env();

    switch (event_id)
    {
    case BT_NOTIFY_A2DP_PROFILE_CONNECTED:
    {
        bt_notify_profile_state_info_t *profile_info = (bt_notify_profile_state_info_t *)data;
        if (profile_info->res == BTS2_SUCC)
        {
            env->addr = profile_info->mac;
            env->is_a2dp_connected = 1;
        }
        LOG_I("A2DP connected");
        break;
    }

    case BT_NOTIFY_A2DP_PROFILE_DISCONNECTED:
        env->is_a2dp_connected = 0;
        LOG_I("A2DP disconnected");
        break;

    default:
        break;
    }
    return 0;
}

static int bt_avrcp_event_handler(uint16_t event_id, uint8_t *data, uint16_t data_len)
{
    bt_app_t *env = bt_app_get_env();

    switch (event_id)
    {
    case BT_NOTIFY_AVRCP_PROFILE_CONNECTED:
        LOG_I("AVRCP connected");
        {
            bt_notify_profile_state_info_t *profile_info = (bt_notify_profile_state_info_t *)data;
            bt_interface_set_avrcp_role_ext(&profile_info->mac, AVRCP_CT);
        }
        break;

    case BT_NOTIFY_AVRCP_PROFILE_DISCONNECTED:
        env->is_abs_enabled = 0;
        LOG_I("AVRCP disconnected");
        break;

    case BT_NOTIFY_AVRCP_VOLUME_CHANGED_REGISTER:
        env->is_abs_enabled = 1;
        break;

    case BT_NOTIFY_AVRCP_ABSOLUTE_VOLUME:
    {
        uint8_t *volume = (uint8_t *)data;
#ifdef AUDIO_USING_MANAGER
        uint8_t local_vol = bt_interface_avrcp_abs_vol_2_local_vol(*volume, audio_server_get_max_volume());
        audio_server_set_private_volume(AUDIO_TYPE_BT_MUSIC, local_vol);
#endif
        break;
    }

    case BT_NOTIFY_AVRCP_PLAY_STATUS:
    {
        uint8_t voide_start = 0;
        for (int i = data_len - 1; i >= 0; i--)
            voide_start |= data[i] << (i * 8);
        if (voide_start == 1 || voide_start == 0)
        {
            current_music.is_playing = voide_start;
        }

        break;
    }

    case BT_NOTIFY_AVRCP_SONG_PROGREAS_STATUS:
    {
        uint32_t curren_music_time_ms = 0;
        if (data_len >= 4)
        {
            curren_music_time_ms = (data[3] << 24) | (data[2] << 16) |
                                   (data[1] << 8) | data[0];
            current_music.current_ms = curren_music_time_ms;
        }
        break;
    }

    case BT_NOTIFY_AVRCP_MEDIA_ATTRIBUTE_CFM:
    {
        bt_notify_avrcp_media_attribute_cfm_t *attr_cfm = (bt_notify_avrcp_media_attribute_cfm_t *)data;
        uint8_t *attr_data = attr_cfm->value;
        uint16_t attr_len = attr_cfm->value_length;
        switch (attr_cfm->media_attribute)
        {
        case AVRCP_MEDIA_ATTRIBUTES_TITLE:
            parse_song_title(attr_data, attr_len);
            break;
        case AVRCP_MEDIA_ATTRIBUTES_ARTIST:
            parse_song_artist(attr_data, attr_len);
            break;
        case AVRCP_MEDIA_ATTRIBUTES_ALBUM:
            // parse_song_album(attr_data, attr_len);
            break;
        case AVRCP_MEDIA_ATTRIBUTES_PLAYTIME:
            parse_song_playtime(attr_data, attr_len);
            break;
        }
        break;
    }

    default:
        break;
    }
    return 0;
}

static int bt_sifli_notify_common_event_hdl(uint16_t event_id, uint8_t *data, uint16_t data_len)
{
    switch (event_id)
    {
    case BT_NOTIFY_COMMON_BT_STACK_READY:
        LOG_I("BT stack ready");
        break;
    case BT_NOTIFY_COMMON_CLOSE_COMPLETE:
        LOG_I("BT stack close complete");
        break;
    case BT_NOTIFY_COMMON_DISCOVER_IND:
        break;
    case BT_NOTIFY_COMMON_INQUIRY_CMP:
        break;
    }
    return 0;
}

static int bt_app_interface_event_handle(uint16_t type, uint16_t event_id, uint8_t *data, uint16_t data_len)
{
    int ret = 0;
    switch (type)
    {
    case BT_NOTIFY_COMMON:
        bt_sifli_notify_common_event_hdl(event_id, data, data_len);
        ret = bt_common_event_handler(event_id, data, data_len);
        break;
    case BT_NOTIFY_PAN:
        ret = bt_pan_event_handler(event_id, data, data_len);
        break;
    case BT_NOTIFY_A2DP:
        ret = bt_a2dp_event_handler(event_id, data, data_len);
        break;
    case BT_NOTIFY_AVRCP:
        ret = bt_avrcp_event_handler(event_id, data, data_len);
        break;
    }
    return ret;
}

static void parse_song_title(uint8_t *data, uint16_t len)
{
    uint16_t text_len = len;
    if (text_len > AVRCP_MAX_SONG_NAME_LEN - 1)
        text_len = AVRCP_MAX_SONG_NAME_LEN - 1;

    memcpy(current_music.title, &data[0], text_len);
    current_music.title[text_len] = '\0';
    LOG_I("Song Title: %s", current_music.title); // 处理歌曲标题
}

static void parse_song_artist(uint8_t *data, uint16_t len)
{
    uint16_t text_len = len;
    if (text_len > AVRCP_MAX_SONG_NAME_LEN - 1)
        text_len = AVRCP_MAX_SONG_NAME_LEN - 1;

    memcpy(current_music.artist, &data[0], text_len);
    current_music.artist[text_len] = '\0';
    LOG_I("Song Artist: %s", current_music.artist); // 处理歌曲作者
}

static void parse_song_album(uint8_t *data, uint16_t len)
{
    uint16_t text_len = len;
    if (text_len > AVRCP_MAX_SONG_NAME_LEN - 1)
        text_len = AVRCP_MAX_SONG_NAME_LEN - 1;

    memcpy(current_music.album, &data[0], text_len);
    current_music.album[text_len] = '\0';
}

static void parse_song_playtime(uint8_t *data, uint16_t len)
{
    // LOG_I("Raw bytes: %02X %02X %02X %02X %02X %02X",
    //       data[0], data[1], data[2], data[3],data[4], data[5]);

    char time_str[7] = {0}; // 6个字符 + 结束符
    for (int i = 0; i < 6 && i < len; i++)
    {
        time_str[i] = (char)data[i];
    }

    uint32_t duration_ms = atoi(time_str);

    // 有效性检查（1小时=3,600,000ms）
    if (duration_ms > 3600000 * 24)
    {
        LOG_W("Implausible duration: %lu ms (%lu hours)",
              duration_ms, duration_ms / 3600000);
        return;
    }

    current_music.duration_ms = duration_ms;

    // LOG_I("Parsed duration: %lu ms (%02d:%02d:%02d)",
    //       duration_ms,
    //       (duration_ms / 1000) / 3600,      // 小时
    //       (duration_ms / 1000) % 3600 / 60, // 分钟
    //       (duration_ms / 1000) % 60);       // 秒
}

music_info_t *get_bt_music_info(void)
{
    return &current_music;
}

void set_bt_volume(uint8_t volume)
{
    bt_app_t *env = bt_app_get_env();
    uint8_t max_vol = 15;
#ifdef AUDIO_USING_MANAGER
    max_vol = audio_server_get_max_volume();
#endif // AUDIO_USING_MANAGER
    uint8_t abs_vol = bt_interface_avrcp_local_vol_2_abs_vol(volume, max_vol);
    bt_interface_avrcp_set_absolute_volume_as_ct_role(abs_vol);
#ifdef AUDIO_USING_MANAGER
    // If absolute volume register by peer device, then local volume shall also adjust.
    if (env->is_abs_enabled)
        audio_server_set_private_volume(AUDIO_TYPE_BT_MUSIC, volume);
#endif // AUDIO_USING_MANAGER
}

uint8_t get_bt_volume(void)
{
    return audio_server_get_private_volume(AUDIO_TYPE_BT_MUSIC);
}