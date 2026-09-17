#ifndef FACTORY_AUDIO_SERVICE_H
#define FACTORY_AUDIO_SERVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <rtthread.h>

#ifdef __cplusplus
extern "C" {
#endif

enum
{
    AUDIO_INIT_ERROR_NONE     = 0,
    AUDIO_INIT_ERROR_AUDPRC   = (1UL << 0),
    AUDIO_INIT_ERROR_AUDCODEC = (1UL << 1),
    AUDIO_INIT_ERROR_MANAGER  = (1UL << 2),
};

typedef struct
{
    bool initialized;
    bool recording;
    bool record_paused;
    bool record_stopping;
    bool record_saving;
    bool record_full;
    bool record_available;
    bool record_save_error;
    bool playing;
    bool play_paused;
    bool play_stopping;
    uint32_t record_bytes;
    uint32_t record_duration_ms;
    int current_db;
} audio_service_status_t;

#define AUDIO_SERVICE_RECORD_MAX_SECONDS 15U

#define AUDIO_MUSIC_TRACK_NAME_MAX 96U

typedef enum
{
    AUDIO_MUSIC_MODE_ORDER = 0,
    AUDIO_MUSIC_MODE_REPEAT_ONE,
    AUDIO_MUSIC_MODE_SHUFFLE,
    AUDIO_MUSIC_MODE_COUNT,
} audio_music_mode_t;

typedef struct
{
    size_t track_count;
    int32_t current_index;
    uint32_t position_seconds;
    uint32_t duration_seconds;
    audio_music_mode_t mode;
    bool playing;
    bool paused;
} audio_music_status_t;

rt_err_t audio_service_init(void);
bool audio_service_is_ready(void);
uint32_t audio_service_get_init_errors(void);
rt_err_t audio_service_get_status(audio_service_status_t *status);

rt_err_t audio_service_record_start(void);
rt_err_t audio_service_record_pause(void);
rt_err_t audio_service_record_resume(void);
rt_err_t audio_service_record_stop(void);
rt_err_t audio_service_record_save(const char *path);
rt_err_t audio_service_record_play(void);
rt_err_t audio_service_play_start(const char *path);
rt_err_t audio_service_play_pause(void);
rt_err_t audio_service_play_resume(void);
rt_err_t audio_service_play_stop(void);
rt_err_t audio_service_set_volume(int volume);
int audio_service_get_volume(void);
rt_err_t audio_service_set_speaker_enabled(bool enabled);

rt_err_t audio_service_music_scan(void);
rt_err_t audio_service_music_get_status(audio_music_status_t *status);
rt_err_t audio_service_music_get_track_name(size_t index, char *name,
                                            size_t name_size);
rt_err_t audio_service_music_play_index(size_t index);
rt_err_t audio_service_music_pause(void);
rt_err_t audio_service_music_resume(void);
rt_err_t audio_service_music_stop(void);
rt_err_t audio_service_music_next(void);
rt_err_t audio_service_music_previous(void);
rt_err_t audio_service_music_set_mode(audio_music_mode_t mode);
audio_music_mode_t audio_service_music_cycle_mode(void);

#ifdef __cplusplus
}
#endif

#endif /* FACTORY_AUDIO_SERVICE_H */
