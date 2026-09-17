#include "recorder_page.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <lvgl.h>
#include <ulog.h>

#include "audio_service.h"
#include "storage_service.h"
#include "platform_psram.h"
#include "mem_section.h"
#include "ui_config.h"
#include "ui_navigation.h"
#include "ui_status_bar.h"
#include "ui_theme.h"

#define RECORDER_SIDE_PADDING       24
#define RECORDER_CONTENT_WIDTH      \
    (UI_SCREEN_WIDTH - RECORDER_SIDE_PADDING * 2)
#define RECORDER_HEADER_Y           (UI_STATUS_BAR_HEIGHT + 8)
#define RECORDER_METER_Y            104
#define RECORDER_METER_HEIGHT       116
#define RECORDER_CONTROL_Y          230
#define RECORDER_LIST_Y             336
#define RECORDER_LIST_HEIGHT        (UI_SCREEN_HEIGHT - RECORDER_LIST_Y)
#define RECORDER_METER_BAR_COUNT    20U
#define RECORDER_TIMER_MS           100U
#ifdef BSP_USING_PSRAM
#define RECORDER_MAX_SCAN_ENTRIES   256U
#else
#define RECORDER_MAX_SCAN_ENTRIES   48U
#endif
#define RECORDER_FILES_PER_PAGE     12U
#define RECORDER_MAX_FILES          RECORDER_FILES_PER_PAGE
#define RECORDER_WAV_HEADER_SIZE    44ULL
#define RECORDER_WAV_BYTES_PER_SEC  32000ULL
#define RECORDER_DIRECTORY_NAME     "record"

typedef struct
{
    char name[STORAGE_ENTRY_NAME_MAX];
    char path[STORAGE_PATH_MAX];
    char detail[64];
    uint64_t size_bytes;
} recorder_file_t;

#ifdef BSP_USING_PSRAM
L2_NON_RET_BSS_SECT_BEGIN(factory_recorder_scan_entries)
L2_NON_RET_BSS_SECT(
    factory_recorder_scan_entries,
    ALIGN(4) static storage_entry_t
        recorder_scan_entries[RECORDER_MAX_SCAN_ENTRIES]);
L2_NON_RET_BSS_SECT_END
#endif

static const uint8_t recorder_meter_heights[RECORDER_METER_BAR_COUNT] = {
    14U, 20U, 28U, 38U, 48U, 58U, 42U, 30U, 52U, 64U,
    56U, 40U, 32U, 46U, 60U, 50U, 36U, 26U, 18U, 12U,
};

static ui_page_t *recorder_page_owner;
static lv_obj_t *recorder_back_button;
static lv_obj_t *recorder_storage_label;
static lv_obj_t *recorder_db_label;
static lv_obj_t *recorder_timer_label;
static lv_obj_t *recorder_state_label;
static lv_obj_t *recorder_meter_bars[RECORDER_METER_BAR_COUNT];
static lv_obj_t *recorder_record_button;
static lv_obj_t *recorder_record_icon;
static lv_obj_t *recorder_record_caption;
static lv_obj_t *recorder_preview_button;
static lv_obj_t *recorder_preview_icon;
static lv_obj_t *recorder_preview_caption;
static lv_obj_t *recorder_stop_button;
static lv_obj_t *recorder_save_button;
static lv_obj_t *recorder_list_count_label;
static lv_obj_t *recorder_previous_page_button;
static lv_obj_t *recorder_next_page_button;
static lv_obj_t *recorder_file_list;
static lv_obj_t *recorder_play_buttons[RECORDER_MAX_FILES];
static lv_obj_t *recorder_play_icons[RECORDER_MAX_FILES];
static lv_obj_t *recorder_delete_buttons[RECORDER_MAX_FILES];
static recorder_file_t *recorder_files;
static size_t recorder_file_count;
static size_t recorder_total_file_count;
static size_t recorder_page_index;
static bool recorder_scan_limited;
static int32_t recorder_playing_index = -1;
static bool recorder_session_active;
static bool recorder_segment_running;
static bool recorder_list_refresh_pending;
static bool recorder_save_requested;
static bool recorder_preview_playing;
static bool recorder_pin_saved_file;
static uint32_t recorder_elapsed_ms;
static rt_tick_t recorder_segment_start_ms;
static char recorder_active_path[STORAGE_PATH_MAX];

static void recorder_scan_files(void);
static void recorder_update_status(void);

static lv_obj_t *recorder_create_label(lv_obj_t *parent,
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
    lv_obj_set_style_text_letter_space(label, 0, 0);
    return label;
}

static void recorder_style_button(lv_obj_t *button, uint32_t background)
{
    lv_obj_set_style_bg_color(button, lv_color_hex(background), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_radius(button, 4, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_opa(button, LV_OPA_40, LV_STATE_DISABLED);
    ui_theme_apply_focus_style(button);
    lv_obj_set_style_bg_color(button, lv_color_hex(UI_COLOR_TRACK),
                              LV_STATE_PRESSED);
}

static void recorder_set_enabled(lv_obj_t *object, bool enabled)
{
    if (object == RT_NULL)
        return;
    if (enabled)
        lv_obj_remove_state(object, LV_STATE_DISABLED);
    else
        lv_obj_add_state(object, LV_STATE_DISABLED);
}

static void recorder_set_state(const char *text, uint32_t color)
{
    lv_label_set_text(recorder_state_label, text);
    lv_obj_set_style_text_color(recorder_state_label,
                                lv_color_hex(color), 0);
}

static uint32_t recorder_current_elapsed_ms(bool recording,
                                            bool paused)
{
    uint32_t elapsed = recorder_elapsed_ms;

    if (recording && !paused && recorder_segment_running)
        elapsed += (uint32_t)(rt_tick_get_millisecond() -
                              recorder_segment_start_ms);
    return elapsed;
}

static void recorder_format_duration(char *buffer, size_t buffer_size,
                                     uint32_t milliseconds)
{
    uint32_t total_seconds = milliseconds / 1000U;
    uint32_t hours = total_seconds / 3600U;

    rt_snprintf(buffer, buffer_size, "%02u:%02u:%02u",
                (unsigned int)hours,
                (unsigned int)((total_seconds / 60U) % 60U),
                (unsigned int)(total_seconds % 60U));
}

static void recorder_format_short_duration(char *buffer,
                                           size_t buffer_size,
                                           uint64_t size_bytes)
{
    uint64_t data_bytes = size_bytes > RECORDER_WAV_HEADER_SIZE ?
                          size_bytes - RECORDER_WAV_HEADER_SIZE : 0ULL;
    uint32_t seconds = (uint32_t)(data_bytes /
                                  RECORDER_WAV_BYTES_PER_SEC);

    rt_snprintf(buffer, buffer_size, "%02u:%02u",
                (unsigned int)(seconds / 60U),
                (unsigned int)(seconds % 60U));
}

static void recorder_format_size(char *buffer, size_t buffer_size,
                                 uint64_t bytes)
{
    const uint64_t kilobyte = 1024ULL;
    const uint64_t megabyte = 1024ULL * 1024ULL;

    if (bytes >= megabyte)
    {
        rt_snprintf(buffer, buffer_size, "%u.%u MB",
                    (unsigned int)(bytes / megabyte),
                    (unsigned int)((bytes % megabyte) * 10ULL /
                                   megabyte));
    }
    else if (bytes >= kilobyte)
    {
        rt_snprintf(buffer, buffer_size, "%u KB",
                    (unsigned int)(bytes / kilobyte));
    }
    else
    {
        rt_snprintf(buffer, buffer_size, "%u B",
                    (unsigned int)bytes);
    }
}

static bool recorder_is_wav_file(const storage_entry_t *entry)
{
    size_t length;

    if (entry == RT_NULL || entry->is_directory ||
            strncmp(entry->name, "REC_", 4U) != 0)
    {
        return false;
    }
    length = strlen(entry->name);
    return length > 4U && strcmp(entry->name + length - 4U, ".wav") == 0;
}

static bool recorder_build_directory_path(char *output,
                                          size_t output_size)
{
    const char *mount_path = storage_service_get_mount_path();
    int length;

    if (strcmp(mount_path, "/") == 0)
        length = rt_snprintf(output, output_size, "/%s",
                             RECORDER_DIRECTORY_NAME);
    else
        length = rt_snprintf(output, output_size, "%s/%s",
                             mount_path, RECORDER_DIRECTORY_NAME);
    return length >= 0 && (size_t)length < output_size;
}

static bool recorder_build_path(char *output, size_t output_size,
                                const char *name)
{
    char directory[STORAGE_PATH_MAX];
    int length;

    if (!recorder_build_directory_path(directory, sizeof(directory)))
        return false;
    length = rt_snprintf(output, output_size, "%s/%s", directory, name);
    return length >= 0 && (size_t)length < output_size;
}

static rt_err_t recorder_ensure_directory(char *path, size_t path_size)
{
    if (!recorder_build_directory_path(path, path_size))
        return -RT_EINVAL;
    return storage_service_ensure_directory(path);
}

static bool recorder_build_new_path(void)
{
    char directory[STORAGE_PATH_MAX];
    char name[64];
    time_t now = time(RT_NULL);
    struct tm *local_time = localtime(&now);
    uint32_t tick = (uint32_t)rt_tick_get_millisecond();

    if (recorder_ensure_directory(directory, sizeof(directory)) != RT_EOK)
        return false;

    if (local_time != RT_NULL && local_time->tm_year >= 120)
    {
        rt_snprintf(name, sizeof(name),
                    "REC_%04d%02d%02d_%02d%02d%02d_%08X.wav",
                    local_time->tm_year + 1900,
                    local_time->tm_mon + 1,
                    local_time->tm_mday,
                    local_time->tm_hour,
                    local_time->tm_min,
                    local_time->tm_sec,
                    (unsigned int)tick);
    }
    else
    {
        rt_snprintf(name, sizeof(name), "REC_T%010u.wav",
                    (unsigned int)tick);
    }
    return recorder_build_path(recorder_active_path,
                               sizeof(recorder_active_path), name);
}

static void recorder_update_meter(int db)
{
    uint32_t active_count;
    uint32_t index;

    if (db <= -60)
        active_count = 0U;
    else if (db >= 0)
        active_count = RECORDER_METER_BAR_COUNT;
    else
        active_count = (uint32_t)(db + 60) *
                       RECORDER_METER_BAR_COUNT / 60U;

    for (index = 0U; index < RECORDER_METER_BAR_COUNT; index++)
    {
        bool active = index < active_count;
        uint32_t color = UI_COLOR_PRIMARY;

        if (index >= 18U)
            color = UI_COLOR_ERROR;
        else if (index >= 14U)
            color = UI_COLOR_WARNING;
        lv_obj_set_style_bg_color(recorder_meter_bars[index],
                                  lv_color_hex(active ? color :
                                               UI_COLOR_TRACK), 0);
        lv_obj_set_style_bg_opa(recorder_meter_bars[index],
                                active ? LV_OPA_COVER : LV_OPA_60, 0);
    }
}

static void recorder_add_group_objects(void)
{
    size_t index;

    if (recorder_page_owner == RT_NULL ||
            recorder_page_owner->group == RT_NULL)
    {
        return;
    }

    lv_group_remove_all_objs(recorder_page_owner->group);
    lv_group_add_obj(recorder_page_owner->group, recorder_back_button);
    lv_group_add_obj(recorder_page_owner->group, recorder_record_button);
    lv_group_add_obj(recorder_page_owner->group, recorder_preview_button);
    lv_group_add_obj(recorder_page_owner->group, recorder_stop_button);
    lv_group_add_obj(recorder_page_owner->group, recorder_save_button);
    lv_group_add_obj(recorder_page_owner->group,
                     recorder_previous_page_button);
    lv_group_add_obj(recorder_page_owner->group,
                     recorder_next_page_button);
    for (index = 0U; index < recorder_file_count; index++)
    {
        lv_group_add_obj(recorder_page_owner->group,
                         recorder_play_buttons[index]);
        lv_group_add_obj(recorder_page_owner->group,
                         recorder_delete_buttons[index]);
    }
}

static void recorder_stop_activity(void)
{
    audio_service_status_t status;

    if (audio_service_get_status(&status) != RT_EOK)
        return;
    if (status.recording)
        audio_service_record_stop();
    if (status.playing || status.play_stopping)
        audio_service_play_stop();
    recorder_playing_index = -1;
    recorder_preview_playing = false;
}

static void recorder_go_home(void)
{
    audio_service_status_t status;

    if (audio_service_get_status(&status) == RT_EOK &&
            status.record_saving)
    {
        return;
    }
    ui_navigation_switch_async(UI_PAGE_HOME);
}

static void recorder_back_clicked(lv_event_t *event)
{
    (void)event;
    recorder_go_home();
}

static void recorder_record_clicked(lv_event_t *event)
{
    audio_service_status_t status;
    rt_err_t result;

    (void)event;
    if (audio_service_get_status(&status) != RT_EOK)
    {
        recorder_set_state("AUDIO ERROR", UI_COLOR_ERROR);
        return;
    }
    if (status.recording)
    {
        if (status.record_paused)
        {
            result = audio_service_record_resume();
            if (result == RT_EOK)
            {
                recorder_segment_start_ms = rt_tick_get_millisecond();
                recorder_segment_running = true;
            }
        }
        else
        {
            recorder_elapsed_ms = recorder_current_elapsed_ms(true, false);
            recorder_segment_running = false;
            result = audio_service_record_pause();
        }
        if (result != RT_EOK)
            recorder_set_state("CONTROL ERROR", UI_COLOR_ERROR);
        recorder_update_status();
        return;
    }
    if (status.record_stopping || status.record_saving ||
            status.play_stopping)
    {
        return;
    }
    if (status.playing)
    {
        audio_service_play_stop();
        recorder_playing_index = -1;
        recorder_preview_playing = false;
    }
    result = audio_service_record_start();
    if (result != RT_EOK)
    {
        LOG_E("Start RAM recording failed: %d", result);
        recorder_set_state("START FAILED", UI_COLOR_ERROR);
        return;
    }
    recorder_session_active = true;
    recorder_list_refresh_pending = false;
    recorder_save_requested = false;
    recorder_elapsed_ms = 0U;
    recorder_segment_start_ms = rt_tick_get_millisecond();
    recorder_segment_running = true;
    recorder_update_status();
}

static void recorder_stop_clicked(lv_event_t *event)
{
    audio_service_status_t status;

    (void)event;
    if (audio_service_get_status(&status) != RT_EOK)
        return;
    if (status.recording)
    {
        if (!status.record_paused)
        {
            recorder_elapsed_ms =
                recorder_current_elapsed_ms(true, false);
            recorder_segment_running = false;
        }
        audio_service_record_stop();
    }
    else if (status.playing || status.play_stopping)
    {
        audio_service_play_stop();
        recorder_playing_index = -1;
        recorder_preview_playing = false;
    }
    recorder_update_status();
}

static void recorder_save_clicked(lv_event_t *event)
{
    audio_service_status_t status;
    rt_err_t result;

    (void)event;
    if (audio_service_get_status(&status) != RT_EOK ||
            !status.record_available || status.recording ||
            status.record_stopping || status.record_saving ||
            status.playing || status.play_stopping ||
            !storage_service_is_ready())
    {
        return;
    }
    if (!recorder_build_new_path())
    {
        recorder_set_state("PATH ERROR", UI_COLOR_ERROR);
        return;
    }

    result = audio_service_record_save(recorder_active_path);
    if (result != RT_EOK)
    {
        LOG_E("Start saving recording '%s' failed: %d",
              recorder_active_path, result);
        recorder_set_state("SAVE FAILED", UI_COLOR_ERROR);
        return;
    }
    recorder_save_requested = true;
    recorder_update_status();
}

static void recorder_preview_clicked(lv_event_t *event)
{
    audio_service_status_t status;
    rt_err_t result;

    (void)event;
    if (audio_service_get_status(&status) != RT_EOK ||
            !status.record_available || status.recording ||
            status.record_stopping || status.record_saving ||
            status.play_stopping)
    {
        return;
    }

    if (status.playing && recorder_preview_playing)
    {
        result = status.play_paused ? audio_service_play_resume() :
                                      audio_service_play_pause();
    }
    else
    {
        if (status.playing)
            audio_service_play_stop();
        recorder_playing_index = -1;
        result = audio_service_record_play();
        if (result == RT_EOK)
            recorder_preview_playing = true;
    }
    if (result != RT_EOK)
    {
        LOG_E("Control RAM recording preview failed: %d", result);
        recorder_preview_playing = false;
        recorder_set_state("PREVIEW FAILED", UI_COLOR_ERROR);
    }
    recorder_update_status();
}

static void recorder_play_clicked(lv_event_t *event)
{
    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(event);
    audio_service_status_t status;
    rt_err_t result;

    if (index >= recorder_file_count ||
            audio_service_get_status(&status) != RT_EOK ||
            status.recording || status.record_stopping ||
            status.record_saving ||
            status.play_stopping)
    {
        return;
    }

    if (status.playing && recorder_playing_index == (int32_t)index)
    {
        result = status.play_paused ? audio_service_play_resume() :
                                      audio_service_play_pause();
    }
    else
    {
        if (status.playing)
            audio_service_play_stop();
        result = audio_service_play_start(recorder_files[index].path);
        if (result == RT_EOK)
        {
            recorder_playing_index = (int32_t)index;
            recorder_preview_playing = false;
        }
    }
    if (result != RT_EOK)
    {
        LOG_E("Control recording playback failed: %d", result);
        recorder_set_state("PLAY FAILED", UI_COLOR_ERROR);
    }
    recorder_update_status();
}

static void recorder_delete_clicked(lv_event_t *event)
{
    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(event);
    audio_service_status_t status;
    rt_err_t result;

    if (index >= recorder_file_count ||
            audio_service_get_status(&status) != RT_EOK ||
            status.recording || status.record_stopping ||
            status.record_saving || status.playing ||
            status.play_stopping)
    {
        return;
    }

    result = storage_service_delete_file(recorder_files[index].path);
    if (result != RT_EOK)
    {
        LOG_E("Delete recording '%s' failed: %d",
              recorder_files[index].path, result);
        recorder_set_state("DELETE FAILED", UI_COLOR_ERROR);
        return;
    }
    recorder_list_refresh_pending = true;
}

static void recorder_create_header(ui_page_t *page)
{
    lv_obj_t *icon;
    lv_obj_t *title;

    recorder_back_button = lv_button_create(page->root);
    lv_obj_set_size(recorder_back_button, 48, 44);
    lv_obj_set_pos(recorder_back_button, 16, RECORDER_HEADER_Y);
    recorder_style_button(recorder_back_button, UI_COLOR_BACKGROUND);
    lv_obj_set_style_bg_opa(recorder_back_button, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(recorder_back_button, recorder_back_clicked,
                        LV_EVENT_CLICKED, RT_NULL);

    icon = recorder_create_label(recorder_back_button, LV_SYMBOL_LEFT,
                                 &lv_font_montserrat_24, UI_COLOR_TEXT);
    lv_obj_center(icon);

    title = recorder_create_label(page->root, "RECORDER",
                                  &lv_font_montserrat_24, UI_COLOR_TEXT);
    lv_obj_set_size(title, 220, 34);
    lv_obj_set_pos(title, 76, RECORDER_HEADER_Y + 5);

    recorder_storage_label = recorder_create_label(
        page->root, "", &lv_font_montserrat_14, UI_COLOR_SUCCESS);
    lv_obj_set_size(recorder_storage_label, 140, 24);
    lv_obj_set_style_text_align(recorder_storage_label,
                                LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(recorder_storage_label,
                   UI_SCREEN_WIDTH - RECORDER_SIDE_PADDING - 140,
                   RECORDER_HEADER_Y + 9);
}

static void recorder_create_meter(ui_page_t *page)
{
    lv_obj_t *panel;
    lv_obj_t *caption;
    uint32_t index;

    panel = lv_obj_create(page->root);
    lv_obj_set_size(panel, RECORDER_CONTENT_WIDTH,
                    RECORDER_METER_HEIGHT);
    lv_obj_set_pos(panel, RECORDER_SIDE_PADDING, RECORDER_METER_Y);
    lv_obj_set_style_bg_color(panel, lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, 4, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    caption = recorder_create_label(panel, "INPUT LEVEL",
                                    &lv_font_montserrat_14,
                                    UI_COLOR_MUTED);
    lv_obj_set_pos(caption, 16, 12);

    recorder_db_label = recorder_create_label(
        panel, "-- dB", &lv_font_montserrat_34, UI_COLOR_TEXT);
    lv_obj_set_size(recorder_db_label, 132, 38);
    lv_obj_set_pos(recorder_db_label, 16, 38);

    recorder_timer_label = recorder_create_label(
        panel, "00:00:00", &lv_font_montserrat_14, UI_COLOR_MUTED);
    lv_obj_set_pos(recorder_timer_label, 18, 84);

    recorder_state_label = recorder_create_label(
        panel, "READY", &lv_font_montserrat_14, UI_COLOR_SUCCESS);
    lv_obj_set_size(recorder_state_label, 116, 20);
    lv_obj_set_style_text_align(recorder_state_label,
                                LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(recorder_state_label,
                   RECORDER_CONTENT_WIDTH - 132, 12);

    for (index = 0U; index < RECORDER_METER_BAR_COUNT; index++)
    {
        recorder_meter_bars[index] = lv_obj_create(panel);
        lv_obj_set_size(recorder_meter_bars[index], 8,
                        recorder_meter_heights[index]);
        lv_obj_set_pos(recorder_meter_bars[index],
                       148 + (int32_t)index * 13,
                       100 - recorder_meter_heights[index]);
        lv_obj_set_style_bg_color(recorder_meter_bars[index],
                                  lv_color_hex(UI_COLOR_TRACK), 0);
        lv_obj_set_style_bg_opa(recorder_meter_bars[index], LV_OPA_60, 0);
        lv_obj_set_style_border_width(recorder_meter_bars[index], 0, 0);
        lv_obj_set_style_radius(recorder_meter_bars[index], 2, 0);
        lv_obj_set_style_pad_all(recorder_meter_bars[index], 0, 0);
        lv_obj_remove_flag(recorder_meter_bars[index],
                           LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(recorder_meter_bars[index],
                           LV_OBJ_FLAG_CLICKABLE);
    }
}

static lv_obj_t *recorder_create_control(ui_page_t *page,
                                         int32_t x,
                                         const char *symbol,
                                         const char *caption,
                                         uint32_t background,
                                         bool circular,
                                         lv_obj_t **icon_output,
                                         lv_obj_t **caption_output)
{
    lv_obj_t *button = lv_button_create(page->root);
    lv_obj_t *icon;
    lv_obj_t *label;

    lv_obj_set_size(button, 58, 58);
    lv_obj_set_pos(button, x, RECORDER_CONTROL_Y);
    recorder_style_button(button, background);
    if (circular)
        lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, 0);

    icon = recorder_create_label(button, symbol,
                                 &lv_font_montserrat_24, UI_COLOR_TEXT);
    lv_obj_center(icon);

    label = recorder_create_label(page->root, caption,
                                  &lv_font_montserrat_12,
                                  UI_COLOR_MUTED);
    lv_obj_set_size(label, 78, 18);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(label, x - 10, RECORDER_CONTROL_Y + 62);

    if (icon_output != RT_NULL)
        *icon_output = icon;
    if (caption_output != RT_NULL)
        *caption_output = label;
    return button;
}

static void recorder_create_controls(ui_page_t *page)
{
    recorder_record_button = recorder_create_control(
        page, 67, LV_SYMBOL_BULLET, "RECORD", UI_COLOR_ERROR, true,
        &recorder_record_icon, &recorder_record_caption);
    recorder_preview_button = recorder_create_control(
        page, 163, LV_SYMBOL_PLAY, "PREVIEW", UI_COLOR_PRIMARY, false,
        &recorder_preview_icon, &recorder_preview_caption);
    recorder_stop_button = recorder_create_control(
        page, 259, LV_SYMBOL_STOP, "STOP", UI_COLOR_PANEL, false,
        RT_NULL, RT_NULL);
    recorder_save_button = recorder_create_control(
        page, 355, LV_SYMBOL_SAVE, "SAVE", UI_COLOR_SUCCESS, false,
        RT_NULL, RT_NULL);

    lv_obj_add_event_cb(recorder_record_button, recorder_record_clicked,
                        LV_EVENT_CLICKED, RT_NULL);
    lv_obj_add_event_cb(recorder_preview_button, recorder_preview_clicked,
                        LV_EVENT_CLICKED, RT_NULL);
    lv_obj_add_event_cb(recorder_stop_button, recorder_stop_clicked,
                        LV_EVENT_CLICKED, RT_NULL);
    lv_obj_add_event_cb(recorder_save_button, recorder_save_clicked,
                        LV_EVENT_CLICKED, RT_NULL);
}

static lv_obj_t *recorder_create_file_action(lv_obj_t *row,
                                             int32_t x,
                                             const char *symbol,
                                             uint32_t color,
                                             lv_event_cb_t callback,
                                             size_t index,
                                             lv_obj_t **icon_output)
{
    lv_obj_t *button = lv_button_create(row);
    lv_obj_t *icon;

    lv_obj_set_size(button, 42, 42);
    lv_obj_set_pos(button, x, 8);
    recorder_style_button(button, UI_COLOR_TRACK);
    lv_obj_set_style_bg_opa(button, LV_OPA_40, 0);
    lv_obj_add_flag(button, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)index);
    icon = recorder_create_label(button, symbol,
                                 &lv_font_montserrat_18, color);
    lv_obj_center(icon);
    if (icon_output != RT_NULL)
        *icon_output = icon;
    return button;
}

static void recorder_create_file_row(const recorder_file_t *file,
                                     size_t index)
{
    lv_obj_t *row;
    lv_obj_t *file_icon;
    lv_obj_t *name;
    lv_obj_t *detail;

    row = lv_obj_create(recorder_file_list);
    lv_obj_set_size(row, RECORDER_CONTENT_WIDTH, 58);
    lv_obj_set_style_bg_color(row, lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 4, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    file_icon = recorder_create_label(row, LV_SYMBOL_FILE,
                                      &lv_font_montserrat_18,
                                      UI_COLOR_SECONDARY);
    lv_obj_set_pos(file_icon, 14, 19);

    name = recorder_create_label(row, file->name,
                                 &lv_font_montserrat_14, UI_COLOR_TEXT);
    lv_obj_set_size(name, 236, 20);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(name, 44, 8);

    detail = recorder_create_label(row, file->detail,
                                   &lv_font_montserrat_12,
                                   UI_COLOR_MUTED);
    lv_obj_set_size(detail, 236, 18);
    lv_label_set_long_mode(detail, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(detail, 44, 32);

    recorder_play_buttons[index] = recorder_create_file_action(
        row, 326, LV_SYMBOL_PLAY, UI_COLOR_SUCCESS,
        recorder_play_clicked, index, &recorder_play_icons[index]);
    recorder_delete_buttons[index] = recorder_create_file_action(
        row, 378, LV_SYMBOL_TRASH, UI_COLOR_ERROR,
        recorder_delete_clicked, index, RT_NULL);
}

static void recorder_previous_page_clicked(lv_event_t *event)
{
    (void)event;
    if (recorder_page_index == 0U)
        return;
    recorder_page_index--;
    recorder_scan_files();
    recorder_update_status();
}

static void recorder_next_page_clicked(lv_event_t *event)
{
    size_t page_count = (recorder_total_file_count +
                         RECORDER_FILES_PER_PAGE - 1U) /
                        RECORDER_FILES_PER_PAGE;

    (void)event;
    if (recorder_page_index + 1U >= page_count)
        return;
    recorder_page_index++;
    recorder_scan_files();
    recorder_update_status();
}

static lv_obj_t *recorder_create_page_button(ui_page_t *page, int32_t x,
                                             const char *symbol,
                                             lv_event_cb_t callback)
{
    lv_obj_t *button = lv_button_create(page->root);
    lv_obj_t *icon;

    lv_obj_set_size(button, 34, 30);
    lv_obj_set_pos(button, x, 302);
    recorder_style_button(button, UI_COLOR_PANEL);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, RT_NULL);
    icon = recorder_create_label(button, symbol,
                                 &lv_font_montserrat_16, UI_COLOR_TEXT);
    lv_obj_center(icon);
    return button;
}

static void recorder_create_file_list(ui_page_t *page)
{
    lv_obj_t *title;

    title = recorder_create_label(page->root, "RECORDINGS",
                                  &lv_font_montserrat_16, UI_COLOR_TEXT);
    lv_obj_set_pos(title, RECORDER_SIDE_PADDING, 310);

    recorder_list_count_label = recorder_create_label(
        page->root, "0 FILES", &lv_font_montserrat_12, UI_COLOR_MUTED);
    lv_obj_set_size(recorder_list_count_label, 126, 18);
    lv_obj_set_style_text_align(recorder_list_count_label,
                                LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(recorder_list_count_label, 218, 312);

    recorder_previous_page_button = recorder_create_page_button(
        page, 354, LV_SYMBOL_PREV, recorder_previous_page_clicked);
    recorder_next_page_button = recorder_create_page_button(
        page, 398, LV_SYMBOL_NEXT, recorder_next_page_clicked);

    recorder_file_list = lv_obj_create(page->root);
    lv_obj_set_size(recorder_file_list, UI_SCREEN_WIDTH,
                    RECORDER_LIST_HEIGHT);
    lv_obj_set_pos(recorder_file_list, 0, RECORDER_LIST_Y);
    lv_obj_set_style_bg_opa(recorder_file_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(recorder_file_list, 0, 0);
    lv_obj_set_style_radius(recorder_file_list, 0, 0);
    lv_obj_set_style_pad_left(recorder_file_list,
                              RECORDER_SIDE_PADDING, 0);
    lv_obj_set_style_pad_right(recorder_file_list,
                               RECORDER_SIDE_PADDING, 0);
    lv_obj_set_style_pad_top(recorder_file_list, 0, 0);
    lv_obj_set_style_pad_bottom(recorder_file_list, 12, 0);
    lv_obj_set_style_pad_row(recorder_file_list, 8, 0);
    lv_obj_set_scroll_dir(recorder_file_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(recorder_file_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_remove_flag(recorder_file_list,
                       LV_OBJ_FLAG_SCROLL_ELASTIC |
                       LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_obj_set_flex_flow(recorder_file_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(recorder_file_list, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
}

static void recorder_scan_files(void)
{
    char directory[STORAGE_PATH_MAX];
#ifdef BSP_USING_PSRAM
    storage_entry_t *scan_entries = recorder_scan_entries;
#else
    storage_entry_t *scan_entries = RT_NULL;
#endif
    size_t scan_count = 0U;
    size_t index;
    size_t recording_index;
    size_t page_start;
    size_t page_count;
    size_t pinned_index = RECORDER_MAX_SCAN_ENTRIES;
    bool limited = false;
    rt_err_t result;

    if (recorder_file_list == RT_NULL)
        return;
    if (recorder_page_owner != RT_NULL &&
            recorder_page_owner->group != RT_NULL)
    {
        lv_group_remove_all_objs(recorder_page_owner->group);
    }
    lv_obj_clean(recorder_file_list);
    recorder_file_count = 0U;
    recorder_total_file_count = 0U;
    recorder_scan_limited = false;
    memset(recorder_play_buttons, 0, sizeof(recorder_play_buttons));
    memset(recorder_play_icons, 0, sizeof(recorder_play_icons));
    memset(recorder_delete_buttons, 0, sizeof(recorder_delete_buttons));

    if (recorder_files == RT_NULL)
    {
        result = -RT_ENOMEM;
    }
    else
    {
        memset(recorder_files, 0,
               RECORDER_MAX_FILES * sizeof(*recorder_files));
        result = recorder_ensure_directory(directory, sizeof(directory));
    }
    if (result == RT_EOK)
    {
#ifndef BSP_USING_PSRAM
        scan_entries = t_sf32_psram_calloc(RECORDER_MAX_SCAN_ENTRIES,
                                           sizeof(*scan_entries));
        if (scan_entries == RT_NULL)
            result = -RT_ENOMEM;
#endif
    }
    if (result == RT_EOK)
    {
        result = storage_service_list_directory(
            directory, scan_entries,
            RECORDER_MAX_SCAN_ENTRIES, &scan_count, &limited);
    }
    if (result == RT_EOK)
    {
        if (recorder_pin_saved_file)
        {
            const char *saved_name = strrchr(recorder_active_path, '/');
            size_t newest_recording_index = RECORDER_MAX_SCAN_ENTRIES;

            saved_name = saved_name == RT_NULL ? recorder_active_path :
                                                 saved_name + 1;
            for (index = 0U; index < scan_count; index++)
            {
                if (!recorder_is_wav_file(&scan_entries[index]))
                    continue;
                newest_recording_index = index;
                if (strcmp(scan_entries[index].name, saved_name) == 0)
                    pinned_index = index;
            }
            if (pinned_index < scan_count &&
                    newest_recording_index < scan_count &&
                    pinned_index != newest_recording_index)
            {
                storage_entry_t temporary =
                    scan_entries[newest_recording_index];

                scan_entries[newest_recording_index] =
                    scan_entries[pinned_index];
                scan_entries[pinned_index] = temporary;
            }
            recorder_pin_saved_file = false;
        }
        for (index = 0U; index < scan_count; index++)
        {
            if (recorder_is_wav_file(&scan_entries[index]))
                recorder_total_file_count++;
        }
        page_count = (recorder_total_file_count +
                      RECORDER_FILES_PER_PAGE - 1U) /
                     RECORDER_FILES_PER_PAGE;
        if (page_count == 0U)
            recorder_page_index = 0U;
        else if (recorder_page_index >= page_count)
            recorder_page_index = page_count - 1U;
        page_start = recorder_page_index * RECORDER_FILES_PER_PAGE;
        recording_index = 0U;

        for (index = scan_count; index > 0U; index--)
        {
            const storage_entry_t *entry = &scan_entries[index - 1U];
            recorder_file_t *file;
            char duration[20];
            char size[20];

            if (!recorder_is_wav_file(entry))
                continue;
            if (recording_index++ < page_start)
                continue;
            if (recorder_file_count >= RECORDER_FILES_PER_PAGE)
                break;
            file = &recorder_files[recorder_file_count];
            rt_strncpy(file->name, entry->name, sizeof(file->name) - 1U);
            file->name[sizeof(file->name) - 1U] = '\0';
            if (!recorder_build_path(file->path, sizeof(file->path),
                                     entry->path_name))
            {
                continue;
            }
            file->size_bytes = entry->size_bytes;
            recorder_format_short_duration(duration, sizeof(duration),
                                           file->size_bytes);
            recorder_format_size(size, sizeof(size), file->size_bytes);
            rt_snprintf(file->detail, sizeof(file->detail), "%s  |  %s",
                        duration, size);
            recorder_create_file_row(file, recorder_file_count);
            recorder_file_count++;
        }
    }
#ifndef BSP_USING_PSRAM
    if (scan_entries != RT_NULL)
        t_sf32_psram_free(scan_entries);
#endif
    recorder_scan_limited = limited;

    if (result == -RT_ENOMEM)
    {
        lv_obj_t *empty = recorder_create_label(
            recorder_file_list, "Not enough memory",
            &lv_font_montserrat_14, UI_COLOR_ERROR);
        lv_obj_set_size(empty, RECORDER_CONTENT_WIDTH, 40);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
    }
    else if (result != RT_EOK)
    {
        lv_obj_t *empty = recorder_create_label(
            recorder_file_list, "TF card unavailable",
            &lv_font_montserrat_14, UI_COLOR_ERROR);
        lv_obj_set_size(empty, RECORDER_CONTENT_WIDTH, 40);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
    }
    else if (recorder_file_count == 0U)
    {
        lv_obj_t *empty = recorder_create_label(
            recorder_file_list, "No recordings",
            &lv_font_montserrat_14, UI_COLOR_MUTED);
        lv_obj_set_size(empty, RECORDER_CONTENT_WIDTH, 40);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
    }

    page_count = (recorder_total_file_count +
                  RECORDER_FILES_PER_PAGE - 1U) /
                 RECORDER_FILES_PER_PAGE;
    if (page_count == 0U)
    {
        lv_label_set_text(recorder_list_count_label, "0 FILES");
    }
    else
    {
        lv_label_set_text_fmt(recorder_list_count_label, "%u/%u  %u%s",
                              (unsigned int)(recorder_page_index + 1U),
                              (unsigned int)page_count,
                              (unsigned int)recorder_total_file_count,
                              recorder_scan_limited ? "+" : "");
    }
    recorder_add_group_objects();
    lv_obj_scroll_to_y(recorder_file_list, 0, LV_ANIM_OFF);
}

static void recorder_update_status(void)
{
    audio_service_status_t status;
    bool storage_ready = storage_service_is_ready();
    bool audio_ready = audio_service_get_status(&status) == RT_EOK;
    bool controls_busy;
    bool page_controls_busy;
    uint32_t elapsed;
    int db = -100;
    char duration[20];
    size_t index;

    lv_label_set_text(recorder_storage_label,
                      storage_ready ? LV_SYMBOL_SD_CARD "  TF CARD" :
                                      LV_SYMBOL_SD_CARD "  NO CARD");
    lv_obj_set_style_text_color(recorder_storage_label,
                                lv_color_hex(storage_ready ?
                                    UI_COLOR_SUCCESS : UI_COLOR_ERROR), 0);

    if (!audio_ready)
    {
        memset(&status, 0, sizeof(status));
        status.current_db = -100;
        recorder_set_state("AUDIO ERROR", UI_COLOR_ERROR);
    }
    else if (status.recording && status.record_paused)
    {
        recorder_set_state("PAUSED", UI_COLOR_WARNING);
    }
    else if (status.recording)
    {
        recorder_set_state("RECORDING", UI_COLOR_ERROR);
    }
    else if (status.record_stopping)
    {
        recorder_set_state("STOPPING", UI_COLOR_WARNING);
    }
    else if (status.record_saving)
    {
        recorder_set_state("SAVING", UI_COLOR_WARNING);
    }
    else if (status.play_stopping)
    {
        recorder_set_state("STOPPING", UI_COLOR_WARNING);
    }
    else if (status.playing && status.play_paused)
    {
        recorder_set_state(recorder_preview_playing ?
                           "PREVIEW PAUSED" : "PLAY PAUSED",
                           UI_COLOR_WARNING);
    }
    else if (status.playing)
    {
        recorder_set_state(recorder_preview_playing ?
                           "PREVIEWING" : "PLAYING",
                           UI_COLOR_SUCCESS);
    }
    else if (status.record_save_error && status.record_available)
    {
        recorder_set_state("SAVE FAILED", UI_COLOR_ERROR);
    }
    else if (status.record_full)
    {
        recorder_set_state("15 SEC LIMIT", UI_COLOR_WARNING);
    }
    else if (status.record_available)
    {
        recorder_set_state("UNSAVED", UI_COLOR_WARNING);
    }
    else if (!storage_ready)
    {
        recorder_set_state("NO TF CARD", UI_COLOR_ERROR);
    }
    else
    {
        recorder_set_state("READY", UI_COLOR_SUCCESS);
    }

    if (status.recording && !status.record_paused)
        db = status.current_db;
    lv_label_set_text_fmt(recorder_db_label,
                          db <= -100 ? "-- dB" : "%d dB", db);
    recorder_update_meter(db);

    elapsed = status.record_duration_ms;
    recorder_format_duration(duration, sizeof(duration), elapsed);
    lv_label_set_text(recorder_timer_label, duration);

    if (status.recording && status.record_paused)
    {
        lv_label_set_text(recorder_record_icon, LV_SYMBOL_PLAY);
        lv_label_set_text(recorder_record_caption, "RESUME");
    }
    else if (status.recording)
    {
        lv_label_set_text(recorder_record_icon, LV_SYMBOL_PAUSE);
        lv_label_set_text(recorder_record_caption, "PAUSE");
    }
    else
    {
        lv_label_set_text(recorder_record_icon, LV_SYMBOL_BULLET);
        lv_label_set_text(recorder_record_caption, "RECORD");
    }

    if (recorder_preview_playing && status.playing)
    {
        lv_label_set_text(recorder_preview_icon,
                          status.play_paused ? LV_SYMBOL_PLAY :
                                               LV_SYMBOL_PAUSE);
        lv_label_set_text(recorder_preview_caption,
                          status.play_paused ? "RESUME" : "PAUSE");
    }
    else
    {
        lv_label_set_text(recorder_preview_icon, LV_SYMBOL_PLAY);
        lv_label_set_text(recorder_preview_caption, "PREVIEW");
    }

    recorder_set_enabled(recorder_record_button,
                         audio_ready && !status.record_stopping &&
                         !status.record_saving && !status.play_stopping);
    recorder_set_enabled(recorder_preview_button,
                         audio_ready && status.record_available &&
                         !status.recording && !status.record_stopping &&
                         !status.record_saving && !status.play_stopping);
    recorder_set_enabled(recorder_back_button,
                         !audio_ready || !status.record_saving);
    recorder_set_enabled(recorder_stop_button,
                         audio_ready && (status.recording ||
                          status.playing || status.play_stopping));
    recorder_set_enabled(recorder_save_button,
                         audio_ready && storage_ready &&
                         status.record_available && !status.recording &&
                         !status.record_stopping && !status.record_saving &&
                         !status.playing && !status.play_stopping);

    controls_busy = !audio_ready || status.recording ||
                    status.record_stopping || status.record_saving ||
                    status.play_stopping;
    for (index = 0U; index < recorder_file_count; index++)
    {
        bool current = recorder_playing_index == (int32_t)index;

        recorder_set_enabled(recorder_play_buttons[index], !controls_busy);
        recorder_set_enabled(recorder_delete_buttons[index],
                             !controls_busy && !status.playing);
        lv_label_set_text(recorder_play_icons[index],
                          current && status.playing && !status.play_paused ?
                              LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    }
    page_controls_busy = controls_busy || status.playing;
    recorder_set_enabled(recorder_previous_page_button,
                         !page_controls_busy && recorder_page_index > 0U);
    recorder_set_enabled(recorder_next_page_button,
                         !page_controls_busy &&
                         (recorder_page_index + 1U) *
                             RECORDER_FILES_PER_PAGE <
                             recorder_total_file_count);
}

static void recorder_page_on_create(ui_page_t *page)
{
    if (page == RT_NULL || page->root == RT_NULL)
        return;

    recorder_page_owner = page;
    recorder_elapsed_ms = 0U;
    recorder_session_active = false;
    recorder_segment_running = false;
    recorder_list_refresh_pending = false;
    recorder_save_requested = false;
    recorder_total_file_count = 0U;
    recorder_page_index = 0U;
    recorder_scan_limited = false;
    recorder_pin_saved_file = false;
    recorder_playing_index = -1;
    recorder_preview_playing = false;
    recorder_files = t_sf32_psram_calloc(RECORDER_MAX_FILES,
                                         sizeof(*recorder_files));
    if (recorder_files == RT_NULL)
        LOG_E("Cannot allocate recorder file list");
    if (page->group != RT_NULL)
        lv_group_set_wrap(page->group, true);
    recorder_create_header(page);
    recorder_create_meter(page);
    recorder_create_controls(page);
    recorder_create_file_list(page);
}

static void recorder_page_on_enter(ui_page_t *page)
{
    audio_service_status_t status;

    (void)page;
    recorder_save_requested =
        audio_service_get_status(&status) == RT_EOK &&
        status.record_saving;
    recorder_scan_files();
    recorder_update_status();
}

static void recorder_page_on_leave(ui_page_t *page)
{
    (void)page;
    recorder_stop_activity();
}

static void recorder_page_on_destroy(ui_page_t *page)
{
    (void)page;
    recorder_page_owner = RT_NULL;
    recorder_back_button = RT_NULL;
    recorder_storage_label = RT_NULL;
    recorder_db_label = RT_NULL;
    recorder_timer_label = RT_NULL;
    recorder_state_label = RT_NULL;
    recorder_record_button = RT_NULL;
    recorder_record_icon = RT_NULL;
    recorder_record_caption = RT_NULL;
    recorder_preview_button = RT_NULL;
    recorder_preview_icon = RT_NULL;
    recorder_preview_caption = RT_NULL;
    recorder_stop_button = RT_NULL;
    recorder_save_button = RT_NULL;
    recorder_list_count_label = RT_NULL;
    recorder_previous_page_button = RT_NULL;
    recorder_next_page_button = RT_NULL;
    recorder_file_list = RT_NULL;
    recorder_file_count = 0U;
    recorder_total_file_count = 0U;
    recorder_page_index = 0U;
    recorder_scan_limited = false;
    recorder_playing_index = -1;
    recorder_preview_playing = false;
    recorder_session_active = false;
    recorder_segment_running = false;
    recorder_list_refresh_pending = false;
    recorder_save_requested = false;
    recorder_pin_saved_file = false;
    if (recorder_files != RT_NULL)
    {
        t_sf32_psram_free(recorder_files);
        recorder_files = RT_NULL;
    }
    memset(recorder_meter_bars, 0, sizeof(recorder_meter_bars));
    memset(recorder_play_buttons, 0, sizeof(recorder_play_buttons));
    memset(recorder_play_icons, 0, sizeof(recorder_play_icons));
    memset(recorder_delete_buttons, 0, sizeof(recorder_delete_buttons));
}

static void recorder_page_on_timer(ui_page_t *page)
{
    audio_service_status_t status;

    (void)page;
    if (audio_service_get_status(&status) == RT_EOK)
    {
        if (recorder_session_active && !status.recording &&
                recorder_segment_running)
        {
            recorder_elapsed_ms +=
                (uint32_t)(rt_tick_get_millisecond() -
                           recorder_segment_start_ms);
            recorder_segment_running = false;
        }
        if (recorder_session_active && !status.recording &&
                !status.record_stopping)
        {
            recorder_session_active = false;
        }
        if (recorder_save_requested && !status.record_saving)
        {
            recorder_save_requested = false;
            if (!status.record_save_error)
            {
                recorder_page_index = 0U;
                recorder_pin_saved_file = true;
                recorder_list_refresh_pending = true;
            }
        }
        if (recorder_playing_index >= 0 && !status.playing &&
                !status.play_stopping)
        {
            recorder_playing_index = -1;
        }
        if (recorder_preview_playing && !status.playing &&
                !status.play_stopping)
        {
            recorder_preview_playing = false;
        }
        if (recorder_list_refresh_pending && !status.recording &&
                !status.record_stopping && !status.record_saving &&
                !status.playing &&
                !status.play_stopping)
        {
            recorder_list_refresh_pending = false;
            recorder_scan_files();
        }
    }
    recorder_update_status();
}

static void recorder_page_on_gesture(ui_page_t *page,
                                     ui_gesture_t gesture)
{
    (void)page;
    if (gesture == UI_GESTURE_RIGHT)
        recorder_go_home();
}

static void recorder_page_on_event(ui_page_t *page,
                                   const ui_event_t *event)
{
    (void)page;
    if (event == RT_NULL || event->type != UI_EVENT_KEY)
        return;
    if (event->data == LV_KEY_ESC || event->data == LV_KEY_HOME)
        recorder_go_home();
}

rt_err_t recorder_page_register(void)
{
    const ui_page_t page = {
        .id = UI_PAGE_RECORDER,
        .app_id = UI_APP_NONE,
        .name = "Recorder Page",
        .on_create = recorder_page_on_create,
        .on_destroy = recorder_page_on_destroy,
        .on_enter = recorder_page_on_enter,
        .on_leave = recorder_page_on_leave,
        .on_timer = recorder_page_on_timer,
        .on_gesture = recorder_page_on_gesture,
        .on_event = recorder_page_on_event,
        .timer_interval_ms = RECORDER_TIMER_MS,
    };

    return ui_navigation_register_page(&page);
}
