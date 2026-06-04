#ifndef __BLUETOOTH_H__
#define __BLUETOOTH_H__

#include "bts2_app_inc.h"
#include "ble_connection_manager.h"
#include "bt_connection_manager.h"

#ifdef AUDIO_USING_MANAGER
#include "audio_server.h"
#endif

#define AVRCP_MEDIA_ATTRIBUTES_TITLE 0x01    // 歌曲名
#define AVRCP_MEDIA_ATTRIBUTES_ARTIST 0x02   // 歌手名
#define AVRCP_MEDIA_ATTRIBUTES_ALBUM 0x03    // 专辑
#define AVRCP_MEDIA_ATTRIBUTES_GENRE 0x06    // 流派
#define AVRCP_MEDIA_ATTRIBUTES_PLAYTIME 0x07 // 歌曲长度

#define BT_APP_CONNECT_PAN 1
#define PAN_TIMER_MS 3000
#define BT_APP_AUDIO_PAUSE 2
#define BT_APP_AUDIO_PLAY 3
#define BT_APP_AUDIO_LAST 4
#define BT_APP_AUDIO_NEXT 5

typedef struct
{
    uint32_t duration_ms;
    uint32_t current_ms;
    uint8_t is_playing; // 1:播放(play) 0:暂停(stop)
    char title[AVRCP_MAX_SONG_NAME_LEN];
    char artist[AVRCP_MAX_SINGER_NAME_LEN];
    char album[AVRCP_MAX_ALBUM_INFO_LEN];
} music_info_t;

typedef struct
{
    bool bt_connected;
    bool bluetooth_pan;
    uint8_t is_a2dp_connected;
    uint8_t is_abs_enabled;
    bt_notify_device_mac_t bd_addr;
    rt_timer_t pan_connect_timer;
    bt_notify_device_mac_t addr;
    rt_timer_t avacp_event_timer;
    rt_mailbox_t g_bt_app_mb;
} bt_app_t;

rt_err_t sf32_bluetooth_init(void);
bt_app_t *bt_app_get_env(void);
music_info_t *get_bt_music_info(void);
void set_bt_volume(uint8_t volume);
uint8_t get_bt_volume(void);


#endif