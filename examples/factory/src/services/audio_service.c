#include "audio_service.h"

#include <rtdevice.h>
#include <stdlib.h>
#include <string.h>
#include <ulog.h>

#if RT_USING_DFS
#include "dfs_posix.h"
#endif

#include "platform_psram.h"
#include "mem_section.h"

#ifdef PKG_USING_AUDIO_MANAGER_LIB
#include "audio_manager_lib.h"
#endif

#define AUDIO_VOLUME_MIN 0
#define AUDIO_VOLUME_MAX 15
#define AUDIO_MUSIC_DIRECTORY "/music"
#define AUDIO_MUSIC_PATH_MAX 128U
#define AUDIO_MUSIC_MAX_TRACKS 256U
#define AUDIO_RECORD_SAMPLE_RATE 16000U
#define AUDIO_RECORD_CHANNELS 1U
#define AUDIO_RECORD_BITS_PER_SAMPLE 16U
#define AUDIO_RECORD_BYTES_PER_SECOND \
    (AUDIO_RECORD_SAMPLE_RATE * AUDIO_RECORD_CHANNELS * \
     AUDIO_RECORD_BITS_PER_SAMPLE / 8U)
#define AUDIO_RECORD_MAX_BYTES \
    (AUDIO_RECORD_BYTES_PER_SECOND * AUDIO_SERVICE_RECORD_MAX_SECONDS)
#define AUDIO_RECORD_WAV_HEADER_SIZE 44U
#define AUDIO_RECORD_NORMALIZE_TARGET 28000U
#define AUDIO_RECORD_NORMALIZE_MAX_GAIN 4U
#define AUDIO_RECORD_NORMALIZE_MIN_PEAK 256U
#define AUDIO_RECORD_SAVE_PATH_MAX 256U
#define AUDIO_RECORD_SAVE_STACK_SIZE 2048U
#define AUDIO_RECORD_SAVE_PRIORITY RT_THREAD_PRIORITY_MIDDLE
#define AUDIO_RECORD_SAVE_TIMESLICE 10U

#if defined(PKG_USING_AUDIO_MANAGER_LIB) && defined(BSP_USING_PSRAM)
L2_NON_RET_BSS_SECT_BEGIN(factory_audio_record_buffer)
L2_NON_RET_BSS_SECT(
    factory_audio_record_buffer,
    ALIGN(4) static uint8_t audio_record_buffer[AUDIO_RECORD_MAX_BYTES]);
L2_NON_RET_BSS_SECT_END
#endif

typedef struct
{
    char *path;
} audio_music_track_t;

static uint32_t audio_init_errors;
static bool audio_initialized;
static bool audio_speaker_enabled;
static rt_mutex_t audio_music_lock;
static audio_music_track_t *audio_music_tracks;
static size_t audio_music_track_count;
static int32_t audio_music_current_index = -1;
static uint32_t audio_music_position_seconds;
static uint32_t audio_music_duration_seconds;
static audio_music_mode_t audio_music_mode = AUDIO_MUSIC_MODE_ORDER;
static bool audio_music_playing;
static bool audio_music_paused;
static bool audio_record_session_active;
static bool audio_record_segment_running;
static bool audio_record_pending;
static bool audio_record_limit_reached;
static volatile bool audio_record_save_in_progress;
static volatile bool audio_record_save_error;
static uint32_t audio_record_elapsed_ms;
static uint32_t audio_record_bytes;
static rt_tick_t audio_record_segment_start_ms;
static rt_thread_t audio_record_save_thread;
static char audio_record_save_path[AUDIO_RECORD_SAVE_PATH_MAX];

#ifdef PKG_USING_AUDIO_MANAGER_LIB
extern uint8_t g_audrx_buf[];
extern uint32_t g_tmp_pos;
#endif

static uint32_t audio_service_record_elapsed(void)
{
    uint32_t elapsed = audio_record_elapsed_ms;

    if (audio_record_session_active && audio_record_segment_running)
    {
        elapsed += (uint32_t)(rt_tick_get_millisecond() -
                              audio_record_segment_start_ms);
    }
    return elapsed;
}

static void audio_service_record_normalize(uint8_t *data, uint32_t bytes)
{
    int16_t *samples = (int16_t *)data;
    uint32_t sample_count = bytes / sizeof(*samples);
    uint32_t peak = 0U;
    uint32_t gain_q15;
    uint32_t index;

    for (index = 0U; index < sample_count; index++)
    {
        int32_t sample = samples[index];
        uint32_t magnitude = (uint32_t)(sample < 0 ? -sample : sample);

        if (magnitude > peak)
            peak = magnitude;
    }
    if (peak < AUDIO_RECORD_NORMALIZE_MIN_PEAK ||
            peak >= AUDIO_RECORD_NORMALIZE_TARGET)
    {
        return;
    }

    gain_q15 = (AUDIO_RECORD_NORMALIZE_TARGET << 15) / peak;
    if (gain_q15 > (AUDIO_RECORD_NORMALIZE_MAX_GAIN << 15))
        gain_q15 = AUDIO_RECORD_NORMALIZE_MAX_GAIN << 15;
    for (index = 0U; index < sample_count; index++)
    {
        int32_t amplified = ((int32_t)samples[index] *
                             (int32_t)gain_q15) >> 15;

        if (amplified > INT16_MAX)
            amplified = INT16_MAX;
        else if (amplified < INT16_MIN)
            amplified = INT16_MIN;
        samples[index] = (int16_t)amplified;
    }
    LOG_I("Normalized recording peak %u with gain %u.%02ux",
          (unsigned int)peak, (unsigned int)(gain_q15 >> 15),
          (unsigned int)(((gain_q15 & 0x7FFFU) * 100U) >> 15));
}

static void audio_service_record_snapshot(void)
{
#if defined(PKG_USING_AUDIO_MANAGER_LIB) && defined(BSP_USING_PSRAM)
    uint32_t bytes = g_tmp_pos;

    if (bytes > AUDIO_RECORD_MAX_BYTES)
        bytes = AUDIO_RECORD_MAX_BYTES;
    bytes &= ~1U;
    if (bytes > 0U)
    {
        memcpy(audio_record_buffer, g_audrx_buf, bytes);
        audio_service_record_normalize(audio_record_buffer, bytes);
    }
    audio_record_bytes = bytes;
    audio_record_pending = bytes > 0U;
#else
    audio_record_bytes = 0U;
    audio_record_pending = false;
#endif
}

static void audio_service_store_u16(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
}

static void audio_service_store_u32(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
    output[2] = (uint8_t)(value >> 16);
    output[3] = (uint8_t)(value >> 24);
}

static void audio_service_build_wav_header(uint8_t *header,
                                           uint32_t data_bytes)
{
    uint16_t block_align = AUDIO_RECORD_CHANNELS *
                           AUDIO_RECORD_BITS_PER_SAMPLE / 8U;

    memset(header, 0, AUDIO_RECORD_WAV_HEADER_SIZE);
    memcpy(header, "RIFF", 4U);
    audio_service_store_u32(header + 4U, data_bytes + 36U);
    memcpy(header + 8U, "WAVEfmt ", 8U);
    audio_service_store_u32(header + 16U, 16U);
    audio_service_store_u16(header + 20U, 1U);
    audio_service_store_u16(header + 22U, AUDIO_RECORD_CHANNELS);
    audio_service_store_u32(header + 24U, AUDIO_RECORD_SAMPLE_RATE);
    audio_service_store_u32(header + 28U,
                            AUDIO_RECORD_BYTES_PER_SECOND);
    audio_service_store_u16(header + 32U, block_align);
    audio_service_store_u16(header + 34U,
                            AUDIO_RECORD_BITS_PER_SAMPLE);
    memcpy(header + 36U, "data", 4U);
    audio_service_store_u32(header + 40U, data_bytes);
}

#if defined(PKG_USING_AUDIO_MANAGER_LIB) && RT_USING_DFS && \
    defined(BSP_USING_PSRAM)
static bool audio_service_write_all(int descriptor, const uint8_t *data,
                                    uint32_t size)
{
    uint32_t offset = 0U;

    while (offset < size)
    {
        int written = write(descriptor, data + offset, size - offset);

        if (written <= 0)
            return false;
        offset += (uint32_t)written;
    }
    return true;
}

static void audio_service_record_save_entry(void *parameter)
{
    uint8_t header[AUDIO_RECORD_WAV_HEADER_SIZE];
    uint32_t offset = 0U;
    uint32_t bytes = audio_record_bytes;
    int descriptor;
    bool success = false;

    (void)parameter;
    descriptor = open(audio_record_save_path,
                      O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0);
    if (descriptor < 0)
    {
        LOG_E("Open recording '%s' failed, errno=%d",
              audio_record_save_path, rt_get_errno());
        goto finish;
    }

    audio_service_build_wav_header(header, bytes);
    if (!audio_service_write_all(descriptor, header, sizeof(header)))
        goto close_file;
    while (offset < bytes)
    {
        uint32_t chunk = bytes - offset;

        if (chunk > 4096U)
            chunk = 4096U;
        if (!audio_service_write_all(descriptor,
                                     audio_record_buffer + offset,
                                     chunk))
        {
            goto close_file;
        }
        offset += chunk;
    }
    success = fsync(descriptor) == 0;

close_file:
    if (close(descriptor) != 0)
        success = false;
    if (!success)
    {
        LOG_E("Save recording '%s' failed, errno=%d",
              audio_record_save_path, rt_get_errno());
        unlink(audio_record_save_path);
    }

finish:
    audio_record_save_error = !success;
    if (success)
    {
        audio_record_pending = false;
        audio_record_limit_reached = false;
        LOG_I("Saved %u recording bytes to '%s'",
              (unsigned int)bytes, audio_record_save_path);
    }
    audio_record_save_in_progress = false;
    audio_record_save_thread = RT_NULL;
}
#endif

void *audio_server_external_cache_calloc(audio_type_t audio_type,
                                         uint32_t count,
                                         uint32_t size)
{
    if (audio_type != AUDIO_TYPE_LOCAL_MUSIC)
        return RT_NULL;
    return t_sf32_psram_calloc(count, size);
}

void audio_server_external_cache_free(audio_type_t audio_type,
                                      void *pointer)
{
    if (audio_type == AUDIO_TYPE_LOCAL_MUSIC)
        t_sf32_psram_free(pointer);
}

void *mp3ctrl_external_calloc(uint32_t count, uint32_t size)
{
    return t_sf32_psram_calloc(count, size);
}

void mp3ctrl_external_free(void *pointer)
{
    t_sf32_psram_free(pointer);
}

static bool audio_service_music_extension_supported(const char *name)
{
    const char *extension;

    if (name == RT_NULL)
        return false;
    extension = strrchr(name, '.');
    if (extension == RT_NULL)
        return false;
    if ((extension[1] == 'm' || extension[1] == 'M') &&
            (extension[2] == 'p' || extension[2] == 'P') &&
            extension[3] == '3' && extension[4] == '\0')
    {
        return true;
    }
    return (extension[1] == 'w' || extension[1] == 'W') &&
           (extension[2] == 'a' || extension[2] == 'A') &&
           (extension[3] == 'v' || extension[3] == 'V') &&
           extension[4] == '\0';
}

static int audio_service_music_track_compare(const void *left,
                                             const void *right)
{
    const audio_music_track_t *left_track = left;
    const audio_music_track_t *right_track = right;

    return strcmp(left_track->path, right_track->path);
}

static const char *audio_service_music_basename(const char *path)
{
    const char *separator;

    separator = path == RT_NULL ? RT_NULL : strrchr(path, '/');
    return separator == RT_NULL ? path : separator + 1;
}

static int32_t audio_service_music_next_index_locked(bool forward)
{
    int32_t index;

    if (audio_music_track_count == 0U)
        return -1;
    if (audio_music_current_index < 0 ||
            (size_t)audio_music_current_index >= audio_music_track_count)
    {
        return 0;
    }
    if (audio_music_mode == AUDIO_MUSIC_MODE_REPEAT_ONE)
        return audio_music_current_index;
    if (audio_music_mode == AUDIO_MUSIC_MODE_SHUFFLE)
    {
        if (audio_music_track_count == 1U)
            return 0;
        do
        {
            index = (int32_t)(rand() % audio_music_track_count);
        } while (index == audio_music_current_index);
        return index;
    }
    if (forward)
    {
        return (audio_music_current_index + 1) %
               (int32_t)audio_music_track_count;
    }
    return (audio_music_current_index - 1 +
            (int32_t)audio_music_track_count) %
           (int32_t)audio_music_track_count;
}

static rt_err_t audio_service_music_start_locked(int32_t index)
{
    const char *path;

    if (index < 0 || (size_t)index >= audio_music_track_count)
        return -RT_EINVAL;
    path = audio_music_tracks[index].path;
    audio_music_current_index = index;
    audio_music_position_seconds = 0U;
    audio_music_duration_seconds = 0U;
    audio_music_playing = true;
    audio_music_paused = false;
    mp3_file_play(path);
    return RT_EOK;
}

static void audio_service_mp3_event(audio_mp3_event_t event,
                                    uint32_t value, void *user_data)
{
    int32_t next_index;

    (void)user_data;
    if (audio_music_lock == RT_NULL)
        return;
    rt_mutex_take(audio_music_lock, RT_WAITING_FOREVER);
    if (event == AUDIO_MP3_EVENT_PROGRESS)
    {
        audio_music_position_seconds = value;
    }
    else if (event == AUDIO_MP3_EVENT_DURATION)
    {
        audio_music_duration_seconds = value;
    }
    else if (event == AUDIO_MP3_EVENT_PLAY_TO_END)
    {
        next_index = audio_service_music_next_index_locked(true);
        if (next_index >= 0)
            audio_service_music_start_locked(next_index);
        else
        {
            audio_music_playing = false;
            audio_music_paused = false;
        }
    }
    rt_mutex_release(audio_music_lock);
}

rt_err_t audio_service_init(void)
{
#ifdef PKG_USING_AUDIO_MANAGER_LIB
    rt_err_t result;

    if (audio_initialized)
        return RT_EOK;

    audio_init_errors = AUDIO_INIT_ERROR_NONE;
    if (rt_device_find(AUDPRC_DEVICE_NAME) == RT_NULL)
    {
        LOG_E("Audio processor device '%s' not found", AUDPRC_DEVICE_NAME);
        audio_init_errors |= AUDIO_INIT_ERROR_AUDPRC;
    }
    if (rt_device_find(AUDCODEC_DEVICE_NAME) == RT_NULL)
    {
        LOG_E("Audio codec device '%s' not found", AUDCODEC_DEVICE_NAME);
        audio_init_errors |= AUDIO_INIT_ERROR_AUDCODEC;
    }
    if (audio_init_errors != AUDIO_INIT_ERROR_NONE)
        return -RT_ENOSYS;

    result = audio_manager_init();
    if (result != RT_EOK)
    {
        LOG_E("Audio manager initialization failed: %d", result);
        audio_init_errors |= AUDIO_INIT_ERROR_MANAGER;
        return result;
    }

    audio_music_lock = rt_mutex_create("music_srv", RT_IPC_FLAG_PRIO);
    if (audio_music_lock == RT_NULL)
        return -RT_ENOMEM;
    mp3_set_event_callback(audio_service_mp3_event, RT_NULL);

    audio_initialized = true;
    audio_speaker_enabled = false;
    LOG_I("Audio service initialized");
    return RT_EOK;
#else
    audio_init_errors = AUDIO_INIT_ERROR_AUDPRC |
                        AUDIO_INIT_ERROR_AUDCODEC |
                        AUDIO_INIT_ERROR_MANAGER;
    LOG_E("Audio manager library must be enabled");
    return -RT_ENOSYS;
#endif
}

bool audio_service_is_ready(void)
{
    return audio_initialized;
}

uint32_t audio_service_get_init_errors(void)
{
    return audio_init_errors;
}

rt_err_t audio_service_get_status(audio_service_status_t *status)
{
    if (status == RT_NULL)
        return -RT_EINVAL;

    memset(status, 0, sizeof(*status));
    status->current_db = -100;
#ifdef PKG_USING_AUDIO_MANAGER_LIB
    if (!audio_initialized)
        return -RT_ERROR;
    else
    {
        const audio_manager_t *audio = get_audio_info();
        uint32_t record_elapsed = audio_service_record_elapsed();

        if (audio_record_session_active && audio->is_recording &&
                record_elapsed >=
                    AUDIO_SERVICE_RECORD_MAX_SECONDS * 1000U)
        {
            audio_record_limit_reached = true;
            audio_service_record_stop();
            audio = get_audio_info();
            record_elapsed = audio_service_record_elapsed();
        }

        status->initialized = true;
        status->recording = audio->is_recording != RT_FALSE;
        status->record_paused = audio->record_paused != RT_FALSE;
        status->record_stopping = !status->recording &&
                                  audio->rx_thread != RT_NULL;
        status->record_saving = audio_record_save_in_progress;
        status->record_full = audio_record_limit_reached ||
                              audio->record_full != RT_FALSE;
        status->record_available = audio_record_pending &&
                                   !audio_record_save_in_progress;
        status->record_save_error = audio_record_save_error;
        status->record_bytes = audio_record_bytes;
        status->record_duration_ms = audio_record_session_active ?
                                     record_elapsed :
                                     audio_record_elapsed_ms;
        status->playing = audio->is_playing != RT_FALSE;
        status->play_paused = audio->play_paused != RT_FALSE;
        status->play_stopping = !status->playing &&
                                audio->tx_thread != RT_NULL;
        status->current_db = audio->current_db;
        return RT_EOK;
    }
#else
    return -RT_ENOSYS;
#endif
}

rt_err_t audio_service_record_start(void)
{
#ifdef PKG_USING_AUDIO_MANAGER_LIB
    rt_err_t result;

    if (!audio_initialized)
        return -RT_ERROR;
    if (audio_record_save_in_progress)
        return -RT_EBUSY;

    result = audio_record_start(RT_NULL);
    if (result != RT_EOK)
        return result;

    audio_record_session_active = true;
    audio_record_segment_running = true;
    audio_record_pending = false;
    audio_record_limit_reached = false;
    audio_record_save_error = false;
    audio_record_elapsed_ms = 0U;
    audio_record_bytes = 0U;
    audio_record_segment_start_ms = rt_tick_get_millisecond();
    return RT_EOK;
#else
    return -RT_ENOSYS;
#endif
}

rt_err_t audio_service_record_pause(void)
{
#ifdef PKG_USING_AUDIO_MANAGER_LIB
    if (!audio_initialized)
        return -RT_ERROR;
    if (!audio_record_session_active || !audio_record_segment_running)
        return -RT_ERROR;

    audio_record_elapsed_ms = audio_service_record_elapsed();
    audio_record_segment_running = false;
    audio_record_pause();
    return RT_EOK;
#else
    return -RT_ENOSYS;
#endif
}

rt_err_t audio_service_record_resume(void)
{
#ifdef PKG_USING_AUDIO_MANAGER_LIB
    if (!audio_initialized)
        return -RT_ERROR;
    if (!audio_record_session_active || audio_record_segment_running)
        return -RT_ERROR;

    audio_record_resume();
    audio_record_segment_start_ms = rt_tick_get_millisecond();
    audio_record_segment_running = true;
    return RT_EOK;
#else
    return -RT_ENOSYS;
#endif
}

rt_err_t audio_service_record_stop(void)
{
#ifdef PKG_USING_AUDIO_MANAGER_LIB
    if (!audio_initialized)
        return -RT_ERROR;
    if (!audio_record_session_active)
        return RT_EOK;

    if (audio_record_segment_running)
        audio_record_elapsed_ms = audio_service_record_elapsed();
    if (audio_record_elapsed_ms >=
            AUDIO_SERVICE_RECORD_MAX_SECONDS * 1000U)
    {
        audio_record_elapsed_ms =
            AUDIO_SERVICE_RECORD_MAX_SECONDS * 1000U;
        audio_record_limit_reached = true;
    }
    audio_record_segment_running = false;
    audio_record_stop();
    audio_record_session_active = false;
    audio_service_record_snapshot();
    return RT_EOK;
#else
    return -RT_ENOSYS;
#endif
}

rt_err_t audio_service_record_save(const char *path)
{
#if defined(PKG_USING_AUDIO_MANAGER_LIB) && RT_USING_DFS && \
    defined(BSP_USING_PSRAM)
    size_t path_length;
    rt_err_t result;

    if (!audio_initialized)
        return -RT_ERROR;
    if (path == RT_NULL)
        return -RT_EINVAL;
    path_length = strlen(path);
    if (path_length == 0U || path_length >= sizeof(audio_record_save_path))
        return -RT_EINVAL;
    if (audio_record_session_active || audio_record_save_in_progress ||
            !audio_record_pending || audio_record_bytes == 0U)
    {
        return -RT_EBUSY;
    }

    memcpy(audio_record_save_path, path, path_length + 1U);
    audio_record_save_error = false;
    audio_record_save_in_progress = true;
    audio_record_save_thread = rt_thread_create(
        "rec_save", audio_service_record_save_entry, RT_NULL,
        AUDIO_RECORD_SAVE_STACK_SIZE, AUDIO_RECORD_SAVE_PRIORITY,
        AUDIO_RECORD_SAVE_TIMESLICE);
    if (audio_record_save_thread == RT_NULL)
    {
        audio_record_save_in_progress = false;
        audio_record_save_error = true;
        return -RT_ENOMEM;
    }
    result = rt_thread_startup(audio_record_save_thread);
    if (result != RT_EOK)
    {
        rt_thread_delete(audio_record_save_thread);
        audio_record_save_thread = RT_NULL;
        audio_record_save_in_progress = false;
        audio_record_save_error = true;
    }
    return result;
#else
    (void)path;
    return -RT_ENOSYS;
#endif
}

rt_err_t audio_service_record_play(void)
{
#if defined(PKG_USING_AUDIO_MANAGER_LIB) && defined(BSP_USING_PSRAM)
    audio_manager_t *audio;

    if (!audio_initialized)
        return -RT_ERROR;
    if (audio_record_session_active || audio_record_save_in_progress ||
            !audio_record_pending || audio_record_bytes == 0U)
    {
        return -RT_EBUSY;
    }

    audio = get_audio_info();
    if (audio->rx_thread != RT_NULL || audio->tx_thread != RT_NULL ||
            audio->is_recording || audio->is_playing)
    {
        return -RT_EBUSY;
    }

    memcpy(g_audrx_buf, audio_record_buffer, audio_record_bytes);
    g_tmp_pos = audio_record_bytes;
    audio->samplerate = AUDIO_RECORD_SAMPLE_RATE;
    audio->channels = AUDIO_RECORD_CHANNELS;
    audio->bit_depth = AUDIO_RECORD_BITS_PER_SAMPLE;
    audio->ram_play_size = audio_record_bytes;
    return audio_play_start(RT_NULL);
#else
    return -RT_ENOSYS;
#endif
}

rt_err_t audio_service_play_start(const char *path)
{
#ifdef PKG_USING_AUDIO_MANAGER_LIB
    if (!audio_initialized)
        return -RT_ERROR;
    if (path == RT_NULL)
        return -RT_EINVAL;
    return audio_play_start(path);
#else
    (void)path;
    return -RT_ENOSYS;
#endif
}

rt_err_t audio_service_play_pause(void)
{
#ifdef PKG_USING_AUDIO_MANAGER_LIB
    if (!audio_initialized)
        return -RT_ERROR;
    audio_play_pause();
    return RT_EOK;
#else
    return -RT_ENOSYS;
#endif
}

rt_err_t audio_service_play_resume(void)
{
#ifdef PKG_USING_AUDIO_MANAGER_LIB
    if (!audio_initialized)
        return -RT_ERROR;
    audio_play_resume();
    return RT_EOK;
#else
    return -RT_ENOSYS;
#endif
}

rt_err_t audio_service_play_stop(void)
{
#ifdef PKG_USING_AUDIO_MANAGER_LIB
    if (!audio_initialized)
        return -RT_ERROR;
    audio_play_stop();
    return RT_EOK;
#else
    return -RT_ENOSYS;
#endif
}

rt_err_t audio_service_set_volume(int volume)
{
#ifdef PKG_USING_AUDIO_MANAGER_LIB
    if (!audio_initialized)
        return -RT_ERROR;
    if (volume < AUDIO_VOLUME_MIN || volume > AUDIO_VOLUME_MAX)
        return -RT_EINVAL;
    audio_set_volume(volume);
    return RT_EOK;
#else
    (void)volume;
    return -RT_ENOSYS;
#endif
}

int audio_service_get_volume(void)
{
#ifdef PKG_USING_AUDIO_MANAGER_LIB
    return audio_initialized ? audio_get_volume() : -RT_ERROR;
#else
    return -RT_ENOSYS;
#endif
}

rt_err_t audio_service_set_speaker_enabled(bool enabled)
{
#ifdef PKG_USING_AUDIO_MANAGER_LIB
    if (!audio_initialized)
        return -RT_ERROR;
    if (audio_speaker_enabled == enabled)
        return RT_EOK;

    if (enabled)
        audio_pa_open();
    else
        audio_pa_close();
    audio_speaker_enabled = enabled;
    LOG_I("Speaker amplifier %s", enabled ? "enabled" : "disabled");
    return RT_EOK;
#else
    (void)enabled;
    return -RT_ENOSYS;
#endif
}

rt_err_t audio_service_music_scan(void)
{
#if defined(PKG_USING_AUDIO_MANAGER_LIB) && RT_USING_DFS
    DIR *directory;
    struct dirent *entry;
    audio_music_track_t *tracks;
    char *path_storage;
    char *path_storage_end;
    size_t count = 0U;
    size_t path_bytes = 0U;
    size_t allocation_size;

    if (!audio_initialized || audio_music_lock == RT_NULL)
        return -RT_ERROR;
    rt_mutex_take(audio_music_lock, RT_WAITING_FOREVER);
    if (audio_music_tracks != RT_NULL)
    {
        rt_mutex_release(audio_music_lock);
        return RT_EOK;
    }
    rt_mutex_release(audio_music_lock);

    directory = opendir(AUDIO_MUSIC_DIRECTORY);
    if (directory == RT_NULL)
        return -RT_ERROR;
    while ((entry = readdir(directory)) != RT_NULL &&
            count < AUDIO_MUSIC_MAX_TRACKS)
    {
        size_t name_length;

        if (!audio_service_music_extension_supported(entry->d_name))
            continue;
        name_length = strlen(entry->d_name);
        if (name_length + sizeof(AUDIO_MUSIC_DIRECTORY) + 1U >
                AUDIO_MUSIC_PATH_MAX)
        {
            continue;
        }
        path_bytes += sizeof(AUDIO_MUSIC_DIRECTORY) + name_length + 1U;
        count++;
    }
    closedir(directory);
    if (count == 0U)
        return RT_EOK;

    allocation_size = count * sizeof(*tracks) + path_bytes;
    tracks = t_sf32_psram_calloc(1U, allocation_size);
    if (tracks == RT_NULL)
    {
        LOG_E("Allocate %u-byte PSRAM music library failed",
              (unsigned int)allocation_size);
        return -RT_ENOMEM;
    }
    path_storage = (char *)(tracks + count);
    path_storage_end = (char *)tracks + allocation_size;

    directory = opendir(AUDIO_MUSIC_DIRECTORY);
    if (directory == RT_NULL)
    {
        t_sf32_psram_free(tracks);
        return -RT_ERROR;
    }
    count = 0U;
    while ((entry = readdir(directory)) != RT_NULL &&
            count < AUDIO_MUSIC_MAX_TRACKS)
    {
        size_t name_length;
        int length;

        if (!audio_service_music_extension_supported(entry->d_name))
            continue;
        name_length = strlen(entry->d_name);
        if (name_length + sizeof(AUDIO_MUSIC_DIRECTORY) + 1U >
                AUDIO_MUSIC_PATH_MAX)
        {
            continue;
        }
        if (path_storage + sizeof(AUDIO_MUSIC_DIRECTORY) +
                name_length + 1U > path_storage_end)
        {
            break;
        }
        length = rt_snprintf(path_storage,
                             AUDIO_MUSIC_PATH_MAX,
                             "%s/%s", AUDIO_MUSIC_DIRECTORY,
                             entry->d_name);
        if (length <= 0 || (size_t)length >= AUDIO_MUSIC_PATH_MAX)
            continue;
        tracks[count].path = path_storage;
        path_storage += (size_t)length + 1U;
        count++;
    }
    closedir(directory);
    qsort(tracks, count, sizeof(*tracks),
          audio_service_music_track_compare);

    rt_mutex_take(audio_music_lock, RT_WAITING_FOREVER);
    audio_music_tracks = tracks;
    audio_music_track_count = count;
    audio_music_current_index = count > 0U ? 0 : -1;
    rt_mutex_release(audio_music_lock);
    LOG_I("Loaded %u tracks from %s into %u bytes of PSRAM",
          (unsigned int)count, AUDIO_MUSIC_DIRECTORY,
          (unsigned int)allocation_size);
    return RT_EOK;
#else
    return -RT_ENOSYS;
#endif
}

rt_err_t audio_service_music_get_status(audio_music_status_t *status)
{
    if (status == RT_NULL)
        return -RT_EINVAL;
    if (audio_music_lock == RT_NULL)
        return -RT_ERROR;
    rt_mutex_take(audio_music_lock, RT_WAITING_FOREVER);
    status->track_count = audio_music_track_count;
    status->current_index = audio_music_current_index;
    status->position_seconds = audio_music_position_seconds;
    status->duration_seconds = audio_music_duration_seconds;
    status->mode = audio_music_mode;
    status->playing = audio_music_playing;
    status->paused = audio_music_paused;
    rt_mutex_release(audio_music_lock);
    return RT_EOK;
}

rt_err_t audio_service_music_get_track_name(size_t index, char *name,
                                            size_t name_size)
{
    const char *base_name;
    char *extension;

    if (name == RT_NULL || name_size == 0U)
        return -RT_EINVAL;
    name[0] = '\0';
    if (audio_music_lock == RT_NULL)
        return -RT_ERROR;
    rt_mutex_take(audio_music_lock, RT_WAITING_FOREVER);
    if (index >= audio_music_track_count)
    {
        rt_mutex_release(audio_music_lock);
        return -RT_EINVAL;
    }
    base_name = audio_service_music_basename(audio_music_tracks[index].path);
    rt_strncpy(name, base_name, name_size - 1U);
    name[name_size - 1U] = '\0';
    rt_mutex_release(audio_music_lock);
    extension = strrchr(name, '.');
    if (extension != RT_NULL)
        *extension = '\0';
    return RT_EOK;
}

rt_err_t audio_service_music_play_index(size_t index)
{
    rt_err_t result;

    if (audio_music_lock == RT_NULL)
        return -RT_ERROR;
    rt_mutex_take(audio_music_lock, RT_WAITING_FOREVER);
    result = audio_service_music_start_locked((int32_t)index);
    rt_mutex_release(audio_music_lock);
    return result;
}

rt_err_t audio_service_music_pause(void)
{
    if (audio_music_lock == RT_NULL)
        return -RT_ERROR;
    rt_mutex_take(audio_music_lock, RT_WAITING_FOREVER);
    if (!audio_music_playing || audio_music_paused)
    {
        rt_mutex_release(audio_music_lock);
        return -RT_ERROR;
    }
    audio_music_paused = true;
    mp3_play_pause();
    rt_mutex_release(audio_music_lock);
    return RT_EOK;
}

rt_err_t audio_service_music_resume(void)
{
    if (audio_music_lock == RT_NULL)
        return -RT_ERROR;
    rt_mutex_take(audio_music_lock, RT_WAITING_FOREVER);
    if (!audio_music_playing || !audio_music_paused)
    {
        rt_mutex_release(audio_music_lock);
        return -RT_ERROR;
    }
    audio_music_paused = false;
    mp3_play_resume();
    rt_mutex_release(audio_music_lock);
    return RT_EOK;
}

rt_err_t audio_service_music_stop(void)
{
    if (audio_music_lock == RT_NULL)
        return -RT_ERROR;
    rt_mutex_take(audio_music_lock, RT_WAITING_FOREVER);
    audio_music_playing = false;
    audio_music_paused = false;
    audio_music_position_seconds = 0U;
    audio_music_duration_seconds = 0U;
    mp3_play_stop();
    rt_mutex_release(audio_music_lock);
    return RT_EOK;
}

static rt_err_t audio_service_music_step(bool forward)
{
    int32_t index;
    rt_err_t result;

    if (audio_music_lock == RT_NULL)
        return -RT_ERROR;
    rt_mutex_take(audio_music_lock, RT_WAITING_FOREVER);
    index = audio_service_music_next_index_locked(forward);
    result = index < 0 ? -RT_ERROR :
             audio_service_music_start_locked(index);
    rt_mutex_release(audio_music_lock);
    return result;
}

rt_err_t audio_service_music_next(void)
{
    return audio_service_music_step(true);
}

rt_err_t audio_service_music_previous(void)
{
    return audio_service_music_step(false);
}

rt_err_t audio_service_music_set_mode(audio_music_mode_t mode)
{
    if (mode >= AUDIO_MUSIC_MODE_COUNT || audio_music_lock == RT_NULL)
        return -RT_EINVAL;
    rt_mutex_take(audio_music_lock, RT_WAITING_FOREVER);
    audio_music_mode = mode;
    rt_mutex_release(audio_music_lock);
    return RT_EOK;
}

audio_music_mode_t audio_service_music_cycle_mode(void)
{
    audio_music_mode_t mode;

    if (audio_music_lock == RT_NULL)
        return AUDIO_MUSIC_MODE_ORDER;
    rt_mutex_take(audio_music_lock, RT_WAITING_FOREVER);
    audio_music_mode = (audio_music_mode + 1) % AUDIO_MUSIC_MODE_COUNT;
    mode = audio_music_mode;
    rt_mutex_release(audio_music_lock);
    return mode;
}
