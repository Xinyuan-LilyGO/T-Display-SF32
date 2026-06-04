#include "rtthread.h"
#include "rtconfig.h"
#include "bf0_hal.h"
#include "drv_io.h"
#include "stdio.h"
#include "string.h"
#include "ulog.h"

#include "audio_mp3ctrl.h"
#include "audio_server.h"
#include "button.h"
#include "drv_flash.h"

#if RT_USING_DFS
#include "dfs_file.h"
#include "dfs_posix.h"
#endif
#include "spi_msd.h"

#define DBG_TAG "mp3_tf_player"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>
typedef struct MusicNode
{
    struct MusicNode *next; // Pointer to the next music node

    char music_path[]; // Music path
} music_node_t;

typedef enum
{
    MUSIC_EVENT_PLAY,   // Play music
    MUSIC_EVENT_PAUSE,  // Pause music
    MUSIC_EVENT_RESUME, // Resume music
    MUSIC_EVENT_STOP,   // Stop music
    MUSIC_EVENT_NEXT,   // Next music

    MUSIC_EVENT_MAX = 0xFF,
} music_event_t;

typedef enum
{
    PLAYER_STATE_IDLE,    // Player is idle
    PLAYER_STATE_PLAYING, // Player is currently playing music
    PLAYER_STATE_PAUSED,  // Player is paused
    PLAYER_STATE_STOPPED, // Player has stopped playing music
} player_state_t;

static music_node_t *music_list = NULL; // Pointer to the head of the music list
static rt_mq_t music_event_mq = NULL;   // Message queue for music events

static bool sdcard_init(void)
{
    rt_device_t msd = rt_device_find("sd0");
    if (msd == NULL)
    {
        LOG_E("Error: the flash device name (sd0) is not found.\n");
        return false;
    }

    if (dfs_mount("sd0", "/", "elm", 0, 0) != 0) // fs exist
    {
        LOG_E("mount fs on tf card to root fail\n");
        return false;
    }

    LOG_I("mount fs on tf card to root success\n");
    return true;
}

static void key1_event_handler(int32_t pin, button_action_t action)
{
    (void)pin; // Unused parameter
    log_d("Key1 action: %d\n", pin);
    static player_state_t player_state = PLAYER_STATE_IDLE;
    music_event_t event = MUSIC_EVENT_MAX;
    switch (action)
    {
    case BUTTON_LONG_PRESSED:
        switch (player_state)
        {
        case PLAYER_STATE_IDLE:
            event = MUSIC_EVENT_PLAY;
            player_state = PLAYER_STATE_PLAYING;
            break;
        default:
            event = MUSIC_EVENT_STOP;
            player_state = PLAYER_STATE_STOPPED;
        }
        break;

    case BUTTON_CLICKED:
        switch (player_state)
        {
        case PLAYER_STATE_PLAYING:
            event = MUSIC_EVENT_PAUSE;
            player_state = PLAYER_STATE_PAUSED;
            break;
        case PLAYER_STATE_PAUSED:
            event = MUSIC_EVENT_RESUME;
            player_state = PLAYER_STATE_PLAYING;
            break;
        default:
            break;
        }
    }

    if (event != MUSIC_EVENT_MAX)
    {
        rt_mq_send(music_event_mq, &event, sizeof(event));
    }
}

static void key2_event_handler(int32_t pin, button_action_t action)
{
    log_d("Key2 action: %d\n", pin);
    (void)pin; // Unused parameter
    music_event_t event;
    switch (action)
    {
    case BUTTON_CLICKED:
        event = MUSIC_EVENT_NEXT;
        break;
    default:
        return; // Ignore other actions
    }

    rt_mq_send(music_event_mq, &event, sizeof(event));
}

static bool key_init(void)
{
    HAL_PIN_Set(PAD_PA33, GPIO_A33, PIN_NOPULL, 1);
    HAL_PIN_Set(PAD_PA35, GPIO_A35, PIN_NOPULL, 1);

    button_cfg_t key1 = {
        .pin = 33,
        .active_state = BUTTON_ACTIVE_LOW,
        .mode = PIN_MODE_INPUT,
        .button_handler = key1_event_handler,
    };

    button_cfg_t key2 = {
        .pin = 35,
        .active_state = BUTTON_ACTIVE_LOW,
        .mode = PIN_MODE_INPUT,
        .button_handler = key2_event_handler,
    };

    int32_t id = button_init(&key1);
    RT_ASSERT(id >= 0);
    RT_ASSERT(SF_EOK == button_enable(id));

    id = button_init(&key2);
    RT_ASSERT(id >= 0);
    RT_ASSERT(SF_EOK == button_enable(id));
    return true;
}

static bool list_music_files(void)
{
    struct dirent *dir;
    DIR *d = opendir("/music");

    if (d == NULL)
    {
        LOG_E("Failed to open directory.\n");
        return false;
    }

    while ((dir = readdir(d)) != NULL)
    {
        // If a file beginning with `._` appears, skip it.
        if (strncmp(dir->d_name, "._", 2) == 0)
        {
            continue;
        }

        if (strstr(dir->d_name, ".mp3") || strstr(dir->d_name, ".wav"))
        {
            const char *prefix = "/music/";
            music_node_t *node = (music_node_t *)rt_malloc(
                sizeof(music_node_t) + strlen(dir->d_name) + strlen(prefix) + 1);
            if (node == NULL)
            {
                LOG_E("Memory allocation failed for music node.\n");
                closedir(d);
                return false;
            }
            // Construct the full path for the music file
            sprintf(node->music_path, "%s%s", prefix, dir->d_name);
            LOG_I("Found music file: %s\n", node->music_path);
            node->next = music_list;
            music_list = node;
        }
    }

    closedir(d);
    return true;
}

static void print_music_list(music_node_t *head)
{
    music_node_t *current = head;
    if (current == NULL)
    {
        LOG_I("No music files found.\n");
        return;
    }

    LOG_I("Available music files:\n");
    while (current != NULL)
    {
        LOG_I("- %s\n", current->music_path);
        current = current->next;
    }
}

static int play_callback_func(audio_server_callback_cmt_t cmd,
                              void *callback_userdata, uint32_t reserved)
{
    switch (cmd)
    {
    case as_callback_cmd_play_to_end:
        LOG_I("Music playback completed.\n");
        music_event_t event = MUSIC_EVENT_NEXT;
        rt_err_t err = rt_mq_send(music_event_mq, &event, sizeof(event));
        RT_ASSERT(err == RT_EOK);
        break;

    default:
        break;
    }

    return 0;
}

static void mp3_player_task(void *parameter)
{
    music_event_t event;
    mp3ctrl_handle mp3_handle = NULL; // Handle for MP3 control
    music_node_t *current_music = music_list; // Pointer to the current music node
    while (1)
    {
        if (rt_mq_recv(music_event_mq, &event, sizeof(event),
                       RT_WAITING_FOREVER) == RT_EOK)
        {
            switch (event)
            {
            case MUSIC_EVENT_PLAY:
                if (mp3_handle != NULL)
                {
                    mp3ctrl_close(mp3_handle); // Close any previous MP3 handle
                }
                mp3_handle = mp3ctrl_open(AUDIO_TYPE_LOCAL_MUSIC, current_music->music_path, play_callback_func, NULL);
                RT_ASSERT(mp3_handle);
                LOG_I("Playing music: %s\n", current_music->music_path);
                mp3ctrl_play(mp3_handle);
                break;
            case MUSIC_EVENT_PAUSE:
                if (mp3_handle == NULL)
                {
                    LOG_W("No music is currently playing.\n");
                    break;
                }
                LOG_I("Pausing music...\n");
                mp3ctrl_pause(mp3_handle);
                break;
            case MUSIC_EVENT_RESUME:
                if (mp3_handle == NULL)
                {
                    LOG_W("No music is currently paused.\n");
                    break;
                }
                LOG_I("Resuming music...\n");
                mp3ctrl_resume(mp3_handle);
                break;
            case MUSIC_EVENT_STOP:
                if (mp3_handle == NULL)
                {
                    LOG_W("No music is currently playing.\n");
                    break;
                }
                LOG_I("Stopping music: %s\n", current_music->music_path);
                mp3ctrl_close(mp3_handle);
                mp3_handle = NULL;
                current_music =
                    music_list; // Reset to the first music in the list
                break;
            case MUSIC_EVENT_NEXT:
                if (mp3_handle == NULL)
                {
                    LOG_W("No music is currently playing.\n");
                    break;
                }
                mp3ctrl_close(mp3_handle); // Close current music
                if (current_music->next != NULL)
                {
                    current_music =
                        current_music->next; // Move to the next music
                }
                else
                {
                    LOG_I("Reached the end of the music list. Looping back to "
                          "the first music.\n");
                    current_music = music_list; // Loop back to the first music
                }
                LOG_I("Playing next music: %s\n", current_music->music_path);
                music_event_t _event =
                    MUSIC_EVENT_PLAY; // Prepare to play the next music
                rt_err_t err =
                    rt_mq_send(music_event_mq, &_event, sizeof(event));
                RT_ASSERT(err == RT_EOK);
                break;
            default:
                LOG_W("Unknown event received.\n");
                break;
            }
        }
    }
}

/// Command to set/get the volume
/// Usage: volume set <0-16>
///        volume
static void volume(int argc, char **argv)
{
    if (argc < 2)
    {
        // Print current volume
        uint8_t volume = audio_server_get_private_volume(AUDIO_TYPE_LOCAL_MUSIC);
        rt_kprintf("Current volume: %d\n", volume);
    }
    else if (strcmp(argv[1], "set") == 0 && argc == 3)
    {
        // Set new volume
        uint8_t new_volume = atoi(argv[2]);
        if (new_volume > 16)
        {
            rt_kprintf("Volume must be between 0 and 16.\n");
            return;
        }
        int err = audio_server_set_private_volume(AUDIO_TYPE_LOCAL_MUSIC, new_volume);
        if (err == 0)
        {
            rt_kprintf("Volume set to %d successfully.\n", new_volume);
        }
        else
        {
            rt_kprintf("Failed to set volume. Error code: %d\n", err);
        }
    }
    else
    {
        rt_kprintf("Usage: volume set <0-16> or just 'volume' to get current volume.\n");
    }
}
MSH_CMD_EXPORT(volume, Set or get the volume for local music playback);

static void mp3_player(int argc, char **argv)
{
    if(argc < 2)
    {
        rt_kprintf("Usage: mp3_player play|pause|resume|stop|next\n");
    }
    else if(strcmp(argv[1], "play") == 0)
    {
        static player_state_t player_state = PLAYER_STATE_PLAYING;
        music_event_t event = MUSIC_EVENT_PLAY;
        rt_mq_send(music_event_mq, &event, sizeof(event));
    }
    else if(strcmp(argv[1], "pause") == 0)
    {
        static player_state_t player_state = PLAYER_STATE_PAUSED;
        music_event_t event = MUSIC_EVENT_PAUSE;
        rt_mq_send(music_event_mq, &event, sizeof(event));
    }
    else if(strcmp(argv[1], "resume") == 0)
    {
        static player_state_t player_state = PLAYER_STATE_PLAYING;
        music_event_t event = MUSIC_EVENT_RESUME;
        rt_mq_send(music_event_mq, &event, sizeof(event));
    }
    else if(strcmp(argv[1], "stop") == 0)
    {
        static player_state_t player_state = PLAYER_STATE_STOPPED;
        music_event_t event = MUSIC_EVENT_STOP;
        rt_mq_send(music_event_mq, &event, sizeof(event));
    }
    else if(strcmp(argv[1], "next") == 0)
    {
        music_event_t event = MUSIC_EVENT_NEXT;
        rt_mq_send(music_event_mq, &event, sizeof(event));
    }
}
MSH_CMD_EXPORT(mp3_player, Play MP3 music from SD card);

int main(void)
{
    rt_pin_mode(41, PIN_MODE_OUTPUT);
    rt_pin_write(41, PIN_HIGH);

    music_event_mq = rt_mq_create("music_event_mq", sizeof(music_event_t), 60,
                                  RT_IPC_FLAG_FIFO);
    if (music_event_mq == NULL)
    {
        LOG_E("Failed to create music event message queue.\n");
        return false;
    }

    if (!sdcard_init())
    {
        rt_kprintf("TF card initialization failed.\n");
        return -1;
    }

    if (!key_init())
    {
        rt_kprintf("Key initialization failed.\n");
        return -1;
    }

    if (!list_music_files())
    {
        rt_kprintf("Failed to list music files.\n");
        return -1;
    }

    audio_server_set_private_volume(AUDIO_TYPE_LOCAL_MUSIC, 10);

    rt_thread_t player_thread = rt_thread_create("mp3_player", mp3_player_task, NULL, 2048, RT_THREAD_PRIORITY_MIDDLE, RT_THREAD_TICK_DEFAULT);
    rt_err_t err = rt_thread_startup(player_thread);
    if (err != RT_EOK)
    {
        LOG_E("Failed to start MP3 player thread: %d\n", err);
        return -1;
    }

    // static player_state_t player_state = PLAYER_STATE_PLAYING;
    // music_event_t event = MUSIC_EVENT_PLAY;
    // rt_mq_send(music_event_mq, &event, sizeof(event));

    while (1)
    {
        rt_thread_mdelay(10000);
    }

    return 0;
}
