#include "music_page.h"

#include <stdbool.h>
#include <stdint.h>

#include <lvgl.h>

#include "audio_service.h"
#include "bluetooth_service.h"
#include "ui_config.h"
#include "ui_navigation.h"
#include "ui_status_bar.h"
#include "ui_theme.h"

#define MUSIC_SIDE_PADDING     24
#define MUSIC_CONTENT_WIDTH    (UI_SCREEN_WIDTH - MUSIC_SIDE_PADDING * 2)
#define MUSIC_HEADER_Y         (UI_STATUS_BAR_HEIGHT + 8)
#define MUSIC_MODE_Y           (UI_STATUS_BAR_HEIGHT + 64)
#define MUSIC_ARTWORK_Y        (UI_STATUS_BAR_HEIGHT + 122)
#define MUSIC_PROGRESS_Y       (UI_STATUS_BAR_HEIGHT + 274)
#define MUSIC_CONTROLS_Y       (UI_STATUS_BAR_HEIGHT + 328)
#define MUSIC_UPDATE_INTERVAL_MS 250U
#define MUSIC_LIST_ROW_HEIGHT 56
#define MUSIC_LIST_VISIBLE_ROWS 8U

LV_IMAGE_DECLARE(music_icon);

typedef enum
{
    MUSIC_SOURCE_BLUETOOTH = 0,
    MUSIC_SOURCE_SD_CARD,
    MUSIC_SOURCE_COUNT,
} music_source_t;

static lv_obj_t *source_buttons[MUSIC_SOURCE_COUNT];
static lv_obj_t *source_labels[MUSIC_SOURCE_COUNT];
static lv_obj_t *back_button;
static lv_obj_t *track_source_label;
static lv_obj_t *track_title_label;
static lv_obj_t *track_artist_label;
static lv_obj_t *track_detail_label;
static lv_obj_t *track_progress;
static lv_obj_t *track_elapsed_label;
static lv_obj_t *track_duration_label;
static lv_obj_t *previous_button;
static lv_obj_t *play_pause_button;
static lv_obj_t *play_pause_icon;
static lv_obj_t *next_button;
static lv_obj_t *mode_button;
static lv_obj_t *mode_icon;
static lv_obj_t *list_button;
static lv_obj_t *music_list_view;
static lv_obj_t *music_list_close_button;
static lv_obj_t *music_list_scroll;
static lv_obj_t *music_list_canvas;
static lv_obj_t *music_list_rows[MUSIC_LIST_VISIBLE_ROWS];
static lv_obj_t *music_list_name_labels[MUSIC_LIST_VISIBLE_ROWS];
static lv_obj_t *music_list_index_labels[MUSIC_LIST_VISIBLE_ROWS];
static int32_t music_list_track_indices[MUSIC_LIST_VISIBLE_ROWS];
static size_t music_list_first_index = (size_t)-1;
static ui_page_t *music_page_owner;
static music_source_t selected_source = MUSIC_SOURCE_BLUETOOTH;

static void music_page_update_bluetooth(void);
static void music_page_update_sd(void);
static void music_page_close_list(void);

static lv_obj_t *music_page_create_label(lv_obj_t *parent,
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

static void music_page_style_button(lv_obj_t *button, uint32_t color)
{
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_radius(button, 4, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_opa(button, LV_OPA_40, LV_STATE_DISABLED);
    ui_theme_apply_focus_style(button);
}

static void music_page_set_controls_enabled(bool enabled)
{
    lv_obj_t *buttons[] = {
        previous_button,
        play_pause_button,
        next_button,
    };
    uint32_t index;

    for (index = 0U; index < sizeof(buttons) / sizeof(buttons[0]); index++)
    {
        if (buttons[index] == RT_NULL)
            continue;
        if (enabled)
            lv_obj_remove_state(buttons[index], LV_STATE_DISABLED);
        else
            lv_obj_add_state(buttons[index], LV_STATE_DISABLED);
    }
}

static const char *music_page_mode_name(audio_music_mode_t mode)
{
    switch (mode)
    {
    case AUDIO_MUSIC_MODE_REPEAT_ONE:
        return "REPEAT ONE";
    case AUDIO_MUSIC_MODE_SHUFFLE:
        return "SHUFFLE";
    case AUDIO_MUSIC_MODE_ORDER:
    default:
        return "PLAY IN ORDER";
    }
}

static const char *music_page_mode_symbol(audio_music_mode_t mode)
{
    switch (mode)
    {
    case AUDIO_MUSIC_MODE_REPEAT_ONE:
        return LV_SYMBOL_LOOP;
    case AUDIO_MUSIC_MODE_SHUFFLE:
        return LV_SYMBOL_SHUFFLE;
    case AUDIO_MUSIC_MODE_ORDER:
    default:
        return LV_SYMBOL_LIST;
    }
}

static void music_page_set_sd_tools_enabled(bool enabled)
{
    lv_obj_t *buttons[] = {mode_button, list_button};
    size_t index;

    for (index = 0U; index < sizeof(buttons) / sizeof(buttons[0]); index++)
    {
        if (buttons[index] == RT_NULL)
            continue;
        if (enabled)
            lv_obj_remove_state(buttons[index], LV_STATE_DISABLED);
        else
            lv_obj_add_state(buttons[index], LV_STATE_DISABLED);
    }
}

static void music_page_format_time(char *buffer, size_t buffer_size,
                                   uint32_t milliseconds)
{
    uint32_t total_seconds = milliseconds / 1000U;

    rt_snprintf(buffer, buffer_size, "%02u:%02u",
                (unsigned int)(total_seconds / 60U),
                (unsigned int)(total_seconds % 60U));
}

static void music_page_update_bluetooth(void)
{
    bluetooth_service_status_t status;
    bool connected;
    char elapsed[16];
    char duration[16];
    uint32_t position;
    int32_t progress = 0;

    if (selected_source != MUSIC_SOURCE_BLUETOOTH ||
            track_title_label == RT_NULL)
    {
        return;
    }

    if (bluetooth_service_get_status(&status) != RT_EOK)
        rt_memset(&status, 0, sizeof(status));
    connected = status.enabled && status.a2dp_connected &&
                status.avrcp_connected;

    lv_label_set_text(track_source_label,
                      connected ? "BLUETOOTH / CONNECTED" :
                                  "BLUETOOTH / DISCONNECTED");
    lv_label_set_text(track_title_label,
                      status.track_title[0] != '\0' ?
                          status.track_title :
                          (connected ? "Waiting for track" :
                                       "No phone connected"));
    lv_label_set_text(track_artist_label,
                      status.track_artist[0] != '\0' ?
                          status.track_artist : "Unknown artist");
    lv_label_set_text(track_detail_label,
                      status.track_album[0] != '\0' ?
                          status.track_album :
                          (connected ? "Waiting for AVRCP metadata" :
                                       "Connect a phone in Settings"));

    position = status.track_position_ms;
    if (status.track_duration_ms > 0U)
    {
        if (position > status.track_duration_ms)
            position = status.track_duration_ms;
        progress = (int32_t)(((uint64_t)position * 1000ULL) /
                             status.track_duration_ms);
        music_page_format_time(duration, sizeof(duration),
                               status.track_duration_ms);
        lv_label_set_text(track_duration_label, duration);
    }
    else
    {
        lv_label_set_text(track_duration_label, "--:--");
    }
    music_page_format_time(elapsed, sizeof(elapsed), position);
    lv_label_set_text(track_elapsed_label, elapsed);
    lv_bar_set_range(track_progress, 0, 1000);
    lv_bar_set_value(track_progress, progress, LV_ANIM_OFF);

    lv_label_set_text(play_pause_icon,
                      status.music_state == BLUETOOTH_MUSIC_PLAYING ?
                          LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    music_page_set_controls_enabled(connected);
    music_page_set_sd_tools_enabled(false);
}

static void music_page_update_sd(void)
{
    audio_music_status_t status;
    char track_name[AUDIO_MUSIC_TRACK_NAME_MAX];
    char detail[64];
    char elapsed[16];
    char duration[16];
    int32_t progress = 0;

    if (selected_source != MUSIC_SOURCE_SD_CARD ||
            track_title_label == RT_NULL)
    {
        return;
    }
    if (audio_service_music_get_status(&status) != RT_EOK)
        rt_memset(&status, 0, sizeof(status));

    rt_snprintf(detail, sizeof(detail), "SD CARD / %u TRACKS",
                (unsigned int)status.track_count);
    lv_label_set_text(track_source_label, detail);
    if (status.current_index >= 0 &&
            audio_service_music_get_track_name(
                (size_t)status.current_index, track_name,
                sizeof(track_name)) == RT_EOK)
    {
        lv_label_set_text(track_title_label, track_name);
    }
    else
    {
        lv_label_set_text(track_title_label,
                          status.track_count > 0U ?
                          "Select a song" : "No music found");
    }
    lv_label_set_text(track_artist_label, music_page_mode_name(status.mode));
    lv_label_set_text(track_detail_label, "/music");
    if (mode_icon != RT_NULL)
        lv_label_set_text(mode_icon, music_page_mode_symbol(status.mode));

    music_page_format_time(elapsed, sizeof(elapsed),
                           status.position_seconds * 1000U);
    lv_label_set_text(track_elapsed_label, elapsed);
    if (status.duration_seconds > 0U)
    {
        music_page_format_time(duration, sizeof(duration),
                               status.duration_seconds * 1000U);
        lv_label_set_text(track_duration_label, duration);
        progress = (int32_t)(((uint64_t)status.position_seconds * 1000ULL) /
                             status.duration_seconds);
        if (progress > 1000)
            progress = 1000;
    }
    else
    {
        lv_label_set_text(track_duration_label, "--:--");
    }
    lv_bar_set_value(track_progress, progress, LV_ANIM_OFF);
    lv_label_set_text(play_pause_icon,
                      status.playing && !status.paused ?
                      LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    music_page_set_controls_enabled(status.track_count > 0U);
    music_page_set_sd_tools_enabled(status.track_count > 0U);
}

static void music_page_apply_source(music_source_t source)
{
    uint32_t index;

    if (source >= MUSIC_SOURCE_COUNT)
        return;
    selected_source = source;

    for (index = 0U; index < MUSIC_SOURCE_COUNT; index++)
    {
        bool selected = index == (uint32_t)source;

        lv_obj_set_style_bg_color(
            source_buttons[index],
            lv_color_hex(selected ? UI_COLOR_PRIMARY : UI_COLOR_PANEL), 0);
        lv_obj_set_style_text_color(
            source_labels[index],
            lv_color_hex(selected ? UI_COLOR_BACKGROUND : UI_COLOR_MUTED), 0);
    }

    if (source == MUSIC_SOURCE_BLUETOOTH)
    {
        audio_music_status_t music_status;

        if (audio_service_music_get_status(&music_status) == RT_EOK &&
                music_status.playing && !music_status.paused)
        {
            audio_service_music_pause();
        }
        music_page_update_bluetooth();
    }
    else
    {
        audio_service_music_scan();
        music_page_update_sd();
    }
}

static void music_page_source_clicked(lv_event_t *event)
{
    music_source_t source =
        (music_source_t)(uintptr_t)lv_event_get_user_data(event);

    music_page_apply_source(source);
}

static void music_page_back_clicked(lv_event_t *event)
{
    (void)event;
    ui_navigation_switch_async(UI_PAGE_HOME);
}

static void music_page_previous_clicked(lv_event_t *event)
{
    (void)event;
    if (selected_source == MUSIC_SOURCE_SD_CARD)
    {
        bluetooth_service_prepare_local_audio();
        audio_service_music_previous();
    }
    else
        bluetooth_service_music_previous();
}

static void music_page_play_pause_clicked(lv_event_t *event)
{
    bluetooth_service_status_t status;
    audio_music_status_t music_status;

    (void)event;
    if (selected_source == MUSIC_SOURCE_SD_CARD)
    {
        if (audio_service_music_get_status(&music_status) != RT_EOK ||
                music_status.track_count == 0U)
        {
            return;
        }
        if (!music_status.playing)
        {
            bluetooth_service_prepare_local_audio();
            audio_service_music_play_index(
                music_status.current_index >= 0 ?
                (size_t)music_status.current_index : 0U);
        }
        else if (music_status.paused)
        {
            bluetooth_service_prepare_local_audio();
            audio_service_music_resume();
        }
        else
        {
            audio_service_music_pause();
        }
        music_page_update_sd();
        return;
    }
    if (bluetooth_service_get_status(&status) != RT_EOK)
        return;

    if (status.music_state == BLUETOOTH_MUSIC_PLAYING)
        bluetooth_service_music_pause();
    else
        bluetooth_service_music_play();
}

static void music_page_next_clicked(lv_event_t *event)
{
    (void)event;
    if (selected_source == MUSIC_SOURCE_SD_CARD)
    {
        bluetooth_service_prepare_local_audio();
        audio_service_music_next();
    }
    else
        bluetooth_service_music_next();
}

static void music_page_mode_clicked(lv_event_t *event)
{
    (void)event;
    if (selected_source != MUSIC_SOURCE_SD_CARD)
        return;
    audio_service_music_cycle_mode();
    music_page_update_sd();
}

static void music_page_add_main_group_objects(void)
{
    size_t index;

    if (music_page_owner == RT_NULL || music_page_owner->group == RT_NULL)
        return;
    lv_group_remove_all_objs(music_page_owner->group);
    if (back_button != RT_NULL)
        lv_group_add_obj(music_page_owner->group, back_button);
    for (index = 0U; index < MUSIC_SOURCE_COUNT; index++)
    {
        if (source_buttons[index] != RT_NULL)
            lv_group_add_obj(music_page_owner->group, source_buttons[index]);
    }
    if (mode_button != RT_NULL)
        lv_group_add_obj(music_page_owner->group, mode_button);
    if (previous_button != RT_NULL)
        lv_group_add_obj(music_page_owner->group, previous_button);
    if (play_pause_button != RT_NULL)
        lv_group_add_obj(music_page_owner->group, play_pause_button);
    if (next_button != RT_NULL)
        lv_group_add_obj(music_page_owner->group, next_button);
    if (list_button != RT_NULL)
        lv_group_add_obj(music_page_owner->group, list_button);
}

static void music_page_refresh_list_rows(void)
{
    audio_music_status_t status;
    lv_coord_t scroll_y;
    size_t first_index;
    size_t slot;

    if (music_list_scroll == RT_NULL || music_list_canvas == RT_NULL ||
            audio_service_music_get_status(&status) != RT_EOK)
    {
        return;
    }
    scroll_y = lv_obj_get_scroll_y(music_list_scroll);
    if (scroll_y < 0)
        scroll_y = 0;
    first_index = (size_t)scroll_y / MUSIC_LIST_ROW_HEIGHT;
    if (first_index > 0U)
        first_index--;
    if (first_index == music_list_first_index)
        return;
    music_list_first_index = first_index;

    for (slot = 0U; slot < MUSIC_LIST_VISIBLE_ROWS; slot++)
    {
        size_t track_index = first_index + slot;
        char name[AUDIO_MUSIC_TRACK_NAME_MAX];
        char number[16];
        bool selected;

        if (track_index >= status.track_count)
        {
            music_list_track_indices[slot] = -1;
            lv_obj_add_flag(music_list_rows[slot], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        music_list_track_indices[slot] = (int32_t)track_index;
        lv_obj_remove_flag(music_list_rows[slot], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_y(music_list_rows[slot],
                     (lv_coord_t)(track_index * MUSIC_LIST_ROW_HEIGHT));
        if (audio_service_music_get_track_name(track_index, name,
                                               sizeof(name)) != RT_EOK)
        {
            name[0] = '\0';
        }
        rt_snprintf(number, sizeof(number), "%u",
                    (unsigned int)(track_index + 1U));
        lv_label_set_text(music_list_index_labels[slot], number);
        lv_label_set_text(music_list_name_labels[slot], name);
        selected = status.current_index == (int32_t)track_index;
        lv_obj_set_style_bg_color(
            music_list_rows[slot],
            lv_color_hex(selected ? UI_COLOR_PRIMARY : UI_COLOR_PANEL), 0);
        lv_obj_set_style_text_color(
            music_list_name_labels[slot],
            lv_color_hex(selected ? UI_COLOR_BACKGROUND : UI_COLOR_TEXT), 0);
    }
}

static void music_page_list_scrolled(lv_event_t *event)
{
    (void)event;
    music_page_refresh_list_rows();
}

static void music_page_list_row_clicked(lv_event_t *event)
{
    size_t slot = (size_t)(uintptr_t)lv_event_get_user_data(event);
    int32_t track_index;

    if (slot >= MUSIC_LIST_VISIBLE_ROWS)
        return;
    track_index = music_list_track_indices[slot];
    if (track_index < 0)
        return;
    bluetooth_service_prepare_local_audio();
    audio_service_music_play_index((size_t)track_index);
    music_page_close_list();
    music_page_update_sd();
}

static void music_page_close_list_clicked(lv_event_t *event)
{
    (void)event;
    music_page_close_list();
}

static void music_page_close_list(void)
{
    if (music_list_view != RT_NULL)
        lv_obj_delete(music_list_view);
    music_list_view = RT_NULL;
    music_list_close_button = RT_NULL;
    music_list_scroll = RT_NULL;
    music_list_canvas = RT_NULL;
    rt_memset(music_list_rows, 0, sizeof(music_list_rows));
    rt_memset(music_list_name_labels, 0,
              sizeof(music_list_name_labels));
    rt_memset(music_list_index_labels, 0,
              sizeof(music_list_index_labels));
    music_list_first_index = (size_t)-1;
    music_page_add_main_group_objects();
}

static void music_page_open_list(void)
{
    audio_music_status_t status;
    lv_obj_t *title;
    lv_obj_t *count_label;
    lv_coord_t content_height;
    char count_text[32];
    size_t slot;

    if (music_page_owner == RT_NULL || music_page_owner->root == RT_NULL ||
            music_list_view != RT_NULL ||
            audio_service_music_get_status(&status) != RT_EOK)
    {
        return;
    }
    music_list_view = lv_obj_create(music_page_owner->root);
    lv_obj_set_size(music_list_view, UI_SCREEN_WIDTH, UI_SCREEN_HEIGHT);
    lv_obj_set_pos(music_list_view, 0, 0);
    lv_obj_set_style_bg_color(music_list_view,
                              lv_color_hex(UI_COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(music_list_view, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(music_list_view, 0, 0);
    lv_obj_set_style_radius(music_list_view, 0, 0);
    lv_obj_set_style_pad_all(music_list_view, 0, 0);
    lv_obj_remove_flag(music_list_view, LV_OBJ_FLAG_SCROLLABLE);

    music_list_close_button = lv_button_create(music_list_view);
    lv_obj_set_size(music_list_close_button, 48, 44);
    lv_obj_set_pos(music_list_close_button, 16, MUSIC_HEADER_Y);
    music_page_style_button(music_list_close_button, UI_COLOR_BACKGROUND);
    lv_obj_set_style_bg_opa(music_list_close_button, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(music_list_close_button,
                        music_page_close_list_clicked,
                        LV_EVENT_CLICKED, RT_NULL);
    title = music_page_create_label(music_list_close_button,
                                    LV_SYMBOL_LEFT,
                                    &lv_font_montserrat_24,
                                    UI_COLOR_TEXT);
    lv_obj_center(title);

    title = music_page_create_label(music_list_view, "MUSIC LIBRARY",
                                    &lv_font_montserrat_20,
                                    UI_COLOR_TEXT);
    lv_obj_set_pos(title, 76, MUSIC_HEADER_Y + 7);
    rt_snprintf(count_text, sizeof(count_text), "%u TRACKS",
                (unsigned int)status.track_count);
    count_label = music_page_create_label(music_list_view, count_text,
                                          &lv_font_montserrat_12,
                                          UI_COLOR_MUTED);
    lv_obj_set_size(count_label, 110, 20);
    lv_obj_set_style_text_align(count_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(count_label, 350, MUSIC_HEADER_Y + 12);

    music_list_scroll = lv_obj_create(music_list_view);
    lv_obj_set_size(music_list_scroll, MUSIC_CONTENT_WIDTH, 390);
    lv_obj_set_pos(music_list_scroll, MUSIC_SIDE_PADDING, 78);
    lv_obj_set_style_bg_opa(music_list_scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(music_list_scroll, 0, 0);
    lv_obj_set_style_radius(music_list_scroll, 0, 0);
    lv_obj_set_style_pad_all(music_list_scroll, 0, 0);
    lv_obj_set_scroll_dir(music_list_scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(music_list_scroll, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_event_cb(music_list_scroll, music_page_list_scrolled,
                        LV_EVENT_SCROLL, RT_NULL);

    music_list_canvas = lv_obj_create(music_list_scroll);
    content_height = (lv_coord_t)(status.track_count *
                                  MUSIC_LIST_ROW_HEIGHT);
    if (content_height < 390)
        content_height = 390;
    lv_obj_set_size(music_list_canvas, MUSIC_CONTENT_WIDTH,
                    content_height);
    lv_obj_set_pos(music_list_canvas, 0, 0);
    lv_obj_set_style_bg_opa(music_list_canvas, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(music_list_canvas, 0, 0);
    lv_obj_set_style_radius(music_list_canvas, 0, 0);
    lv_obj_set_style_pad_all(music_list_canvas, 0, 0);
    lv_obj_remove_flag(music_list_canvas, LV_OBJ_FLAG_SCROLLABLE);

    for (slot = 0U; slot < MUSIC_LIST_VISIBLE_ROWS; slot++)
    {
        music_list_rows[slot] = lv_button_create(music_list_canvas);
        lv_obj_set_size(music_list_rows[slot], MUSIC_CONTENT_WIDTH, 52);
        music_page_style_button(music_list_rows[slot], UI_COLOR_PANEL);
        lv_obj_add_flag(music_list_rows[slot], LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        lv_obj_add_event_cb(music_list_rows[slot],
                            music_page_list_row_clicked,
                            LV_EVENT_CLICKED,
                            (void *)(uintptr_t)slot);
        music_list_index_labels[slot] = music_page_create_label(
            music_list_rows[slot], "", &lv_font_montserrat_14,
            UI_COLOR_MUTED);
        lv_obj_set_size(music_list_index_labels[slot], 34, 22);
        lv_obj_set_pos(music_list_index_labels[slot], 14, 15);
        music_list_name_labels[slot] = music_page_create_label(
            music_list_rows[slot], "", &lv_font_simsun_16_cjk,
            UI_COLOR_TEXT);
        lv_obj_set_size(music_list_name_labels[slot], 360, 24);
        lv_label_set_long_mode(music_list_name_labels[slot],
                               LV_LABEL_LONG_DOT);
        lv_obj_set_pos(music_list_name_labels[slot], 52, 13);
    }
    music_list_first_index = (size_t)-1;
    lv_obj_update_layout(music_list_scroll);
    if (status.current_index > 0)
    {
        lv_obj_scroll_to_y(
            music_list_scroll,
            (lv_coord_t)(status.current_index * MUSIC_LIST_ROW_HEIGHT),
            LV_ANIM_OFF);
    }
    music_page_refresh_list_rows();
    lv_obj_move_foreground(music_list_view);

    if (music_page_owner->group != RT_NULL)
    {
        lv_group_remove_all_objs(music_page_owner->group);
        lv_group_add_obj(music_page_owner->group,
                         music_list_close_button);
        for (slot = 0U; slot < MUSIC_LIST_VISIBLE_ROWS; slot++)
            lv_group_add_obj(music_page_owner->group,
                             music_list_rows[slot]);
        lv_group_focus_obj(music_list_close_button);
    }
}

static void music_page_list_clicked(lv_event_t *event)
{
    (void)event;
    if (selected_source == MUSIC_SOURCE_SD_CARD)
        music_page_open_list();
}

static lv_obj_t *music_page_add_icon_button(lv_obj_t *parent,
                                            lv_coord_t x,
                                            lv_coord_t size,
                                            const char *symbol,
                                            uint32_t background,
                                            uint32_t foreground)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *icon;

    lv_obj_set_size(button, size, size);
    lv_obj_set_pos(button, x, MUSIC_CONTROLS_Y);
    music_page_style_button(button, background);
    icon = music_page_create_label(button, symbol,
                                   &lv_font_montserrat_24, foreground);
    lv_obj_center(icon);
    return button;
}

static void music_page_create_header(ui_page_t *page)
{
    lv_obj_t *back_icon;
    lv_obj_t *title;

    back_button = lv_button_create(page->root);
    lv_obj_set_size(back_button, 48, 44);
    lv_obj_set_pos(back_button, 16, MUSIC_HEADER_Y);
    music_page_style_button(back_button, UI_COLOR_BACKGROUND);
    lv_obj_set_style_bg_opa(back_button, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(back_button, music_page_back_clicked,
                        LV_EVENT_CLICKED, RT_NULL);

    back_icon = music_page_create_label(back_button, LV_SYMBOL_LEFT,
                                        &lv_font_montserrat_24,
                                        UI_COLOR_TEXT);
    lv_obj_center(back_icon);

    title = music_page_create_label(page->root, "MUSIC",
                                    &lv_font_montserrat_24,
                                    UI_COLOR_TEXT);
    lv_obj_set_size(title, 220, 34);
    lv_obj_set_pos(title, 76, MUSIC_HEADER_Y + 5);

    if (page->group != RT_NULL)
        lv_group_add_obj(page->group, back_button);
}

static void music_page_create_source_control(ui_page_t *page)
{
    static const char *source_text[MUSIC_SOURCE_COUNT] = {
        LV_SYMBOL_BLUETOOTH "  Bluetooth",
        LV_SYMBOL_SD_CARD "  SD Card",
    };
    lv_obj_t *container;
    uint32_t index;

    container = lv_obj_create(page->root);
    lv_obj_set_size(container, MUSIC_CONTENT_WIDTH, 44);
    lv_obj_set_pos(container, MUSIC_SIDE_PADDING, MUSIC_MODE_Y);
    lv_obj_set_style_bg_color(container, lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_radius(container, 4, 0);
    lv_obj_set_style_pad_all(container, 3, 0);
    lv_obj_set_style_pad_column(container, 4, 0);
    lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);

    for (index = 0U; index < MUSIC_SOURCE_COUNT; index++)
    {
        source_buttons[index] = lv_button_create(container);
        lv_obj_set_size(source_buttons[index], 211, 38);
        lv_obj_set_flex_grow(source_buttons[index], 1);
        music_page_style_button(source_buttons[index], UI_COLOR_PANEL);
        lv_obj_add_event_cb(source_buttons[index],
                            music_page_source_clicked,
                            LV_EVENT_CLICKED,
                            (void *)(uintptr_t)index);

        source_labels[index] = music_page_create_label(
            source_buttons[index], source_text[index],
            &lv_font_montserrat_16, UI_COLOR_MUTED);
        lv_obj_center(source_labels[index]);

        if (page->group != RT_NULL)
            lv_group_add_obj(page->group, source_buttons[index]);
    }
}

static void music_page_create_track(ui_page_t *page)
{
    lv_obj_t *artwork;
    lv_obj_t *image;

    artwork = lv_obj_create(page->root);
    lv_obj_set_size(artwork, 128, 128);
    lv_obj_set_pos(artwork, MUSIC_SIDE_PADDING, MUSIC_ARTWORK_Y);
    lv_obj_set_style_bg_color(artwork, lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(artwork, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(artwork, 0, 0);
    lv_obj_set_style_radius(artwork, 4, 0);
    lv_obj_set_style_pad_all(artwork, 0, 0);
    lv_obj_remove_flag(artwork, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(artwork, LV_OBJ_FLAG_CLICKABLE);

    image = lv_image_create(artwork);
    lv_image_set_src(image, &music_icon);
    lv_obj_center(image);

    track_source_label = music_page_create_label(
        page->root, "", &lv_font_montserrat_14, UI_COLOR_PRIMARY);
    lv_obj_set_size(track_source_label, 280, 22);
    lv_obj_set_pos(track_source_label, 176, MUSIC_ARTWORK_Y + 4);

    track_title_label = music_page_create_label(
        page->root, "", &lv_font_simsun_16_cjk,
        UI_COLOR_TEXT);
    lv_obj_set_size(track_title_label, 280, 34);
    lv_label_set_long_mode(track_title_label, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(track_title_label, 176, MUSIC_ARTWORK_Y + 34);

    track_artist_label = music_page_create_label(
        page->root, "", &lv_font_montserrat_16, UI_COLOR_MUTED);
    lv_obj_set_size(track_artist_label, 280, 24);
    lv_label_set_long_mode(track_artist_label, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(track_artist_label, 176, MUSIC_ARTWORK_Y + 72);

    track_detail_label = music_page_create_label(
        page->root, "", &lv_font_montserrat_14, UI_COLOR_MUTED);
    lv_obj_set_size(track_detail_label, 280, 22);
    lv_label_set_long_mode(track_detail_label, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(track_detail_label, 176, MUSIC_ARTWORK_Y + 101);
}

static void music_page_create_progress(ui_page_t *page)
{
    track_progress = lv_bar_create(page->root);
    lv_obj_set_size(track_progress, MUSIC_CONTENT_WIDTH, 8);
    lv_obj_set_pos(track_progress, MUSIC_SIDE_PADDING, MUSIC_PROGRESS_Y);
    lv_bar_set_range(track_progress, 0, 1000);
    lv_bar_set_value(track_progress, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(track_progress, lv_color_hex(UI_COLOR_TRACK),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(track_progress, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(track_progress, lv_color_hex(UI_COLOR_PRIMARY),
                              LV_PART_INDICATOR);
    lv_obj_set_style_radius(track_progress, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(track_progress, 4, LV_PART_INDICATOR);

    track_elapsed_label = music_page_create_label(
        page->root, "00:00", &lv_font_montserrat_14, UI_COLOR_TEXT);
    lv_obj_set_size(track_elapsed_label, 80, 20);
    lv_obj_set_pos(track_elapsed_label, MUSIC_SIDE_PADDING,
                   MUSIC_PROGRESS_Y + 18);

    track_duration_label = music_page_create_label(
        page->root, "--:--", &lv_font_montserrat_14, UI_COLOR_MUTED);
    lv_obj_set_size(track_duration_label, 80, 20);
    lv_obj_set_style_text_align(track_duration_label,
                                LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(track_duration_label,
                   UI_SCREEN_WIDTH - MUSIC_SIDE_PADDING - 80,
                   MUSIC_PROGRESS_Y + 18);
}

static void music_page_create_controls(ui_page_t *page)
{
    mode_button = music_page_add_icon_button(
        page->root, 24, 64, LV_SYMBOL_LIST,
        UI_COLOR_PANEL, UI_COLOR_TEXT);
    previous_button = music_page_add_icon_button(
        page->root, 112, 64, LV_SYMBOL_PREV,
        UI_COLOR_PANEL, UI_COLOR_TEXT);
    play_pause_button = music_page_add_icon_button(
        page->root, 202, 76, LV_SYMBOL_PLAY,
        UI_COLOR_PRIMARY, UI_COLOR_BACKGROUND);
    next_button = music_page_add_icon_button(
        page->root, 304, 64, LV_SYMBOL_NEXT,
        UI_COLOR_PANEL, UI_COLOR_TEXT);
    list_button = music_page_add_icon_button(
        page->root, 392, 64, LV_SYMBOL_DIRECTORY,
        UI_COLOR_PANEL, UI_COLOR_TEXT);
    mode_icon = lv_obj_get_child(mode_button, 0);
    play_pause_icon = lv_obj_get_child(play_pause_button, 0);

    lv_obj_add_event_cb(mode_button, music_page_mode_clicked,
                        LV_EVENT_CLICKED, RT_NULL);
    lv_obj_add_event_cb(previous_button, music_page_previous_clicked,
                        LV_EVENT_CLICKED, RT_NULL);
    lv_obj_add_event_cb(play_pause_button, music_page_play_pause_clicked,
                        LV_EVENT_CLICKED, RT_NULL);
    lv_obj_add_event_cb(next_button, music_page_next_clicked,
                        LV_EVENT_CLICKED, RT_NULL);
    lv_obj_add_event_cb(list_button, music_page_list_clicked,
                        LV_EVENT_CLICKED, RT_NULL);

    if (page->group != RT_NULL)
    {
        lv_group_add_obj(page->group, mode_button);
        lv_group_add_obj(page->group, previous_button);
        lv_group_add_obj(page->group, play_pause_button);
        lv_group_add_obj(page->group, next_button);
        lv_group_add_obj(page->group, list_button);
    }
}

static void music_page_on_create(ui_page_t *page)
{
    if (page == RT_NULL || page->root == RT_NULL)
        return;

    music_page_owner = page;
    if (page->group != RT_NULL)
        lv_group_set_wrap(page->group, true);
    music_page_create_header(page);
    music_page_create_source_control(page);
    music_page_create_track(page);
    music_page_create_progress(page);
    music_page_create_controls(page);
    music_page_add_main_group_objects();
    music_page_apply_source(selected_source);
}

static void music_page_on_destroy(ui_page_t *page)
{
    (void)page;
    music_page_owner = RT_NULL;
    back_button = RT_NULL;
    source_buttons[MUSIC_SOURCE_BLUETOOTH] = RT_NULL;
    source_buttons[MUSIC_SOURCE_SD_CARD] = RT_NULL;
    source_labels[MUSIC_SOURCE_BLUETOOTH] = RT_NULL;
    source_labels[MUSIC_SOURCE_SD_CARD] = RT_NULL;
    track_source_label = RT_NULL;
    track_title_label = RT_NULL;
    track_artist_label = RT_NULL;
    track_detail_label = RT_NULL;
    track_progress = RT_NULL;
    track_elapsed_label = RT_NULL;
    track_duration_label = RT_NULL;
    previous_button = RT_NULL;
    play_pause_button = RT_NULL;
    play_pause_icon = RT_NULL;
    next_button = RT_NULL;
    mode_button = RT_NULL;
    mode_icon = RT_NULL;
    list_button = RT_NULL;
    music_list_view = RT_NULL;
    music_list_close_button = RT_NULL;
    music_list_scroll = RT_NULL;
    music_list_canvas = RT_NULL;
    rt_memset(music_list_rows, 0, sizeof(music_list_rows));
    rt_memset(music_list_name_labels, 0,
              sizeof(music_list_name_labels));
    rt_memset(music_list_index_labels, 0,
              sizeof(music_list_index_labels));
    music_list_first_index = (size_t)-1;
}

static void music_page_on_enter(ui_page_t *page)
{
    (void)page;
    music_page_apply_source(selected_source);
}

static void music_page_on_timer(ui_page_t *page)
{
    (void)page;
    if (selected_source == MUSIC_SOURCE_SD_CARD)
        music_page_update_sd();
    else
        music_page_update_bluetooth();
}

static void music_page_on_gesture(ui_page_t *page,
                                  ui_gesture_t gesture)
{
    (void)page;
    if (gesture == UI_GESTURE_RIGHT)
    {
        if (music_list_view != RT_NULL)
            music_page_close_list();
        else
            ui_navigation_switch_async(UI_PAGE_HOME);
    }
}

static void music_page_on_event(ui_page_t *page,
                                const ui_event_t *event)
{
    (void)page;
    if (event == RT_NULL || event->type != UI_EVENT_KEY)
        return;
    if (event->data == LV_KEY_ESC && music_list_view != RT_NULL)
        music_page_close_list();
    else if (event->data == LV_KEY_ESC || event->data == LV_KEY_HOME)
        ui_navigation_switch_async(UI_PAGE_HOME);
}

rt_err_t music_page_register(void)
{
    const ui_page_t page = {
        .id = UI_PAGE_MUSIC,
        .app_id = UI_APP_NONE,
        .name = "Music Page",
        .on_create = music_page_on_create,
        .on_destroy = music_page_on_destroy,
        .on_enter = music_page_on_enter,
        .on_timer = music_page_on_timer,
        .on_gesture = music_page_on_gesture,
        .on_event = music_page_on_event,
        .timer_interval_ms = MUSIC_UPDATE_INTERVAL_MS,
    };

    return ui_navigation_register_page(&page);
}
