#ifndef FACTORY_BLUETOOTH_SERVICE_H
#define FACTORY_BLUETOOTH_SERVICE_H

#include <stdbool.h>
#include <stdint.h>
#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLUETOOTH_SERVICE_ADDRESS_SIZE 6U
#define BLUETOOTH_SERVICE_VOLUME_MAX  15U
#define BLUETOOTH_SERVICE_TRACK_TEXT_MAX 129U

typedef enum
{
    BLUETOOTH_MUSIC_STOPPED = 0,
    BLUETOOTH_MUSIC_PAUSED,
    BLUETOOTH_MUSIC_PLAYING,
} bluetooth_music_state_t;

typedef enum
{
    BLUETOOTH_SERVICE_EVENT_READY = 0,
    BLUETOOTH_SERVICE_EVENT_POWER_CHANGED,
    BLUETOOTH_SERVICE_EVENT_PHONE_CHANGED,
    BLUETOOTH_SERVICE_EVENT_PAN_CHANGED,
    BLUETOOTH_SERVICE_EVENT_MUSIC_CHANGED,
} bluetooth_service_event_t;

typedef struct
{
    bool initialized;
    bool stack_ready;
    bool enabled;
    bool phone_connected;
    bool pan_enabled;
    bool pan_connected;
    bool a2dp_connected;
    bool avrcp_connected;
    bool peer_address_valid;
    uint8_t peer_address[BLUETOOTH_SERVICE_ADDRESS_SIZE];
    bluetooth_music_state_t music_state;
    bool metadata_valid;
    char track_title[BLUETOOTH_SERVICE_TRACK_TEXT_MAX];
    char track_artist[BLUETOOTH_SERVICE_TRACK_TEXT_MAX];
    char track_album[BLUETOOTH_SERVICE_TRACK_TEXT_MAX];
    uint32_t track_duration_ms;
    uint32_t track_position_ms;
} bluetooth_service_status_t;

/* Called from the Bluetooth stack context; UI users must post to the UI thread. */
typedef void (*bluetooth_service_event_callback_t)(
    bluetooth_service_event_t event,
    const bluetooth_service_status_t *status,
    void *user_data);

rt_err_t bluetooth_service_init(void);
bool bluetooth_service_is_ready(void);
bool bluetooth_service_is_enabled(void);
bool bluetooth_service_is_pan_enabled(void);
rt_err_t bluetooth_service_get_status(bluetooth_service_status_t *status);
rt_err_t bluetooth_service_set_event_callback(
    bluetooth_service_event_callback_t callback,
    void *user_data);

rt_err_t bluetooth_service_set_enabled(bool enabled);
/* Address uses the SiFli SDK byte order (addr[0] through addr[5]). */
rt_err_t bluetooth_service_connect_phone(
    const uint8_t address[BLUETOOTH_SERVICE_ADDRESS_SIZE]);
rt_err_t bluetooth_service_disconnect_phone(void);
rt_err_t bluetooth_service_set_pan_enabled(bool enabled);

rt_err_t bluetooth_service_music_play(void);
rt_err_t bluetooth_service_music_pause(void);
rt_err_t bluetooth_service_music_stop(void);
rt_err_t bluetooth_service_music_next(void);
rt_err_t bluetooth_service_music_previous(void);
rt_err_t bluetooth_service_music_volume_up(void);
rt_err_t bluetooth_service_music_volume_down(void);
rt_err_t bluetooth_service_music_set_volume(uint8_t volume);
rt_err_t bluetooth_service_prepare_local_audio(void);

#ifdef __cplusplus
}
#endif

#endif /* FACTORY_BLUETOOTH_SERVICE_H */
