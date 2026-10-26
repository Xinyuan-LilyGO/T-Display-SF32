#include "storage_page.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <lvgl.h>

#include "lv_font.h"
#include "storage_service.h"
#include "platform_psram.h"
#include "ui_config.h"
#include "ui_navigation.h"
#include "ui_status_bar.h"
#include "ui_theme.h"

#define STORAGE_PAGE_SIDE_PADDING  16
#define STORAGE_PAGE_HEADER_Y      (UI_STATUS_BAR_HEIGHT + 4)
#define STORAGE_PAGE_LOCATION_Y    100
#define STORAGE_PAGE_LIST_Y        160
#define STORAGE_PAGE_LIST_HEIGHT   272
#define STORAGE_PAGE_CONTENT_WIDTH (UI_SCREEN_WIDTH - 32)
#define STORAGE_PAGE_ROW_HEIGHT    56
#define STORAGE_PAGE_MAX_ENTRIES   20U
#define STORAGE_PAGE_DISPLAY_NAME_MAX 288U
#define STORAGE_PAGE_DISPLAY_PATH_MAX 384U

static ui_page_t *storage_page_owner;
static lv_obj_t *storage_back_button;
static lv_obj_t *storage_refresh_button;
static lv_obj_t *storage_up_button;
static lv_obj_t *storage_path_label;
static lv_obj_t *storage_usage_label;
static lv_obj_t *storage_list;
static lv_obj_t *storage_footer_label;
static lv_obj_t *storage_entry_buttons[STORAGE_PAGE_MAX_ENTRIES];
static storage_entry_t *storage_entries;
static size_t storage_entry_count;
static int32_t storage_selected_index = -1;
static char storage_current_path[STORAGE_PATH_MAX] = "/";

static lv_obj_t *storage_page_create_label(lv_obj_t *parent,
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

static void storage_page_style_container(lv_obj_t *object,
                                         uint32_t color,
                                         lv_opa_t opacity)
{
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(object, opacity, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_radius(object, 4, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_shadow_width(object, 0, 0);
}

static void storage_page_style_button(lv_obj_t *button,
                                      uint32_t color)
{
    storage_page_style_container(button, color, LV_OPA_COVER);
    ui_theme_apply_focus_style(button);
}

static lv_obj_t *storage_page_create_header_button(
    lv_obj_t *parent, lv_coord_t x, const char *symbol,
    lv_event_cb_t callback)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *icon;

    lv_obj_set_size(button, 48, 44);
    lv_obj_set_pos(button, x, STORAGE_PAGE_HEADER_Y);
    storage_page_style_button(button, UI_COLOR_BACKGROUND);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, RT_NULL);
    icon = storage_page_create_label(button, symbol,
                                     &lv_font_montserrat_24,
                                     UI_COLOR_TEXT);
    lv_obj_center(icon);
    return button;
}

static void storage_page_format_size(char *buffer, size_t buffer_size,
                                     uint64_t bytes)
{
    const uint64_t kilobyte = 1024ULL;
    const uint64_t megabyte = 1024ULL * 1024ULL;
    const uint64_t gigabyte = 1024ULL * 1024ULL * 1024ULL;

    if (bytes >= gigabyte)
    {
        uint32_t whole = (uint32_t)(bytes / gigabyte);
        uint32_t decimal = (uint32_t)((bytes % gigabyte) * 10ULL /
                                      gigabyte);

        rt_snprintf(buffer, buffer_size, "%u.%u GB",
                    (unsigned int)whole, (unsigned int)decimal);
    }
    else if (bytes >= megabyte)
    {
        rt_snprintf(buffer, buffer_size, "%u MB",
                    (unsigned int)(bytes / megabyte));
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

static bool storage_page_is_root(void)
{
    return strcmp(storage_current_path,
                  storage_service_get_mount_path()) == 0;
}

static void storage_page_parent_path(void)
{
    char *separator;

    if (storage_page_is_root())
        return;

    separator = strrchr(storage_current_path, '/');
    if (separator == RT_NULL || separator == storage_current_path)
    {
        rt_strncpy(storage_current_path,
                   storage_service_get_mount_path(),
                   sizeof(storage_current_path) - 1U);
        storage_current_path[sizeof(storage_current_path) - 1U] = '\0';
    }
    else
    {
        *separator = '\0';
    }
}

static bool storage_page_build_child_path(const char *name)
{
    char path[STORAGE_PATH_MAX];
    int length;

    if (strcmp(storage_current_path, "/") == 0)
        length = rt_snprintf(path, sizeof(path), "/%s", name);
    else
        length = rt_snprintf(path, sizeof(path), "%s/%s",
                             storage_current_path, name);
    if (length < 0 || (size_t)length >= sizeof(path))
        return false;

    memcpy(storage_current_path, path, (size_t)length + 1U);
    return true;
}

static void storage_page_row_focused(lv_event_t *event)
{
    lv_obj_t *row = lv_event_get_target_obj(event);

    lv_obj_scroll_to_view(row, LV_ANIM_ON);
}

static void storage_page_add_group_objects(void)
{
    size_t index;

    if (storage_page_owner == RT_NULL ||
            storage_page_owner->group == RT_NULL)
        return;

    lv_group_remove_all_objs(storage_page_owner->group);
    lv_group_add_obj(storage_page_owner->group, storage_back_button);
    lv_group_add_obj(storage_page_owner->group, storage_up_button);
    lv_group_add_obj(storage_page_owner->group, storage_refresh_button);
    for (index = 0U; index < storage_entry_count; index++)
        lv_group_add_obj(storage_page_owner->group,
                         storage_entry_buttons[index]);

    if (storage_entry_count > 0U)
        lv_group_focus_obj(storage_entry_buttons[0]);
    else
        lv_group_focus_obj(storage_refresh_button);
}

static void storage_page_update_usage(void)
{
    storage_info_t info;

    if (storage_service_get_info(&info) == RT_EOK &&
            info.mounted && info.total_bytes > 0U)
    {
        uint64_t used = info.total_bytes - info.free_bytes;
        uint32_t percent = (uint32_t)(used * 100ULL /
                                      info.total_bytes);

        lv_label_set_text_fmt(storage_usage_label, "%u%% used",
                              (unsigned int)percent);
        lv_obj_set_style_text_color(storage_usage_label,
                                    lv_color_hex(UI_COLOR_MUTED), 0);
    }
    else
    {
        lv_label_set_text(storage_usage_label, "Unavailable");
        lv_obj_set_style_text_color(storage_usage_label,
                                    lv_color_hex(UI_COLOR_ERROR), 0);
    }
}

static void storage_page_select_file(size_t index)
{
    char size_text[24];
    char display_name[STORAGE_PAGE_DISPLAY_NAME_MAX];
    size_t row;

    if (index >= storage_entry_count ||
            storage_entries[index].is_directory)
        return;

    storage_selected_index = (int32_t)index;
    for (row = 0U; row < storage_entry_count; row++)
    {
        lv_obj_set_style_bg_opa(
            storage_entry_buttons[row],
            row == index ? LV_OPA_30 : LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(storage_entry_buttons[row],
                                  lv_color_hex(UI_COLOR_PRIMARY), 0);
    }

    storage_page_format_size(size_text, sizeof(size_text),
                             storage_entries[index].size_bytes);
    storage_service_name_to_utf8(storage_entries[index].name,
                                 display_name, sizeof(display_name));
    lv_label_set_text_fmt(storage_footer_label, "%s  |  %s",
                          display_name, size_text);
    lv_obj_set_style_text_color(storage_footer_label,
                                lv_color_hex(UI_COLOR_TEXT), 0);
}

static void storage_page_scan(void);

static void storage_page_row_clicked(lv_event_t *event)
{
    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(event);

    if (index >= storage_entry_count)
        return;
    if (storage_entries[index].is_directory)
    {
        if (storage_page_build_child_path(
                storage_entries[index].path_name))
            storage_page_scan();
        else
        {
            lv_label_set_text(storage_footer_label,
                              "Path is too long to open");
            lv_obj_set_style_text_color(storage_footer_label,
                                        lv_color_hex(UI_COLOR_ERROR), 0);
        }
    }
    else
    {
        storage_page_select_file(index);
    }
}

static void storage_page_create_row(size_t index)
{
    storage_entry_t *entry = &storage_entries[index];
    lv_obj_t *row;
    lv_obj_t *icon;
    lv_obj_t *name;
    lv_obj_t *type;
    char detail[24];
    char display_name[STORAGE_PAGE_DISPLAY_NAME_MAX];

    row = lv_button_create(storage_list);
    lv_obj_set_size(row, STORAGE_PAGE_CONTENT_WIDTH,
                    STORAGE_PAGE_ROW_HEIGHT);
    storage_page_style_button(row, UI_COLOR_PANEL);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM,
                                 LV_PART_MAIN);
    lv_obj_set_style_border_color(row, lv_color_hex(UI_COLOR_TRACK),
                                  LV_PART_MAIN);
    lv_obj_add_event_cb(row, storage_page_row_clicked,
                        LV_EVENT_CLICKED, (void *)(uintptr_t)index);
    lv_obj_add_event_cb(row, storage_page_row_focused,
                        LV_EVENT_FOCUSED, RT_NULL);

    icon = storage_page_create_label(
        row, entry->is_directory ? LV_SYMBOL_DIRECTORY : LV_SYMBOL_FILE,
        &lv_font_montserrat_24,
        entry->is_directory ? UI_COLOR_WARNING : UI_COLOR_PRIMARY);
    lv_obj_set_pos(icon, 14, 15);

    storage_service_name_to_utf8(entry->name, display_name,
                                 sizeof(display_name));
    name = storage_page_create_label(row, display_name,
                                     &lv_font_source_han_sans_sc_16_cjk,
                                     UI_COLOR_TEXT);
    lv_obj_set_size(name, 274, 26);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(name, 54, 8);

    if (entry->is_directory)
        rt_snprintf(detail, sizeof(detail), "Folder");
    else
        storage_page_format_size(detail, sizeof(detail),
                                 entry->size_bytes);
    type = storage_page_create_label(
        row, entry->is_directory ? "Directory" : detail,
        &lv_font_montserrat_12, UI_COLOR_MUTED);
    lv_obj_set_size(type, 274, 18);
    lv_obj_set_pos(type, 54, 33);

    if (entry->is_directory)
    {
        lv_obj_t *open_icon = storage_page_create_label(
            row, LV_SYMBOL_RIGHT, &lv_font_montserrat_16,
            UI_COLOR_MUTED);

        lv_obj_align(open_icon, LV_ALIGN_RIGHT_MID, -14, 0);
    }
    else
    {
        lv_obj_t *kind = storage_page_create_label(
            row, "FILE", &lv_font_montserrat_12, UI_COLOR_MUTED);

        lv_obj_align(kind, LV_ALIGN_RIGHT_MID, -14, 0);
    }
    storage_entry_buttons[index] = row;
}

static void storage_page_show_empty(const char *title,
                                    const char *detail,
                                    uint32_t color)
{
    lv_obj_t *icon;
    lv_obj_t *title_label;
    lv_obj_t *detail_label;

    icon = storage_page_create_label(storage_list, LV_SYMBOL_SD_CARD,
                                     &lv_font_montserrat_30, color);
    lv_obj_set_width(icon, STORAGE_PAGE_CONTENT_WIDTH);
    lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
    title_label = storage_page_create_label(storage_list, title,
                                            &lv_font_montserrat_18,
                                            UI_COLOR_TEXT);
    lv_obj_set_width(title_label, STORAGE_PAGE_CONTENT_WIDTH);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);
    detail_label = storage_page_create_label(storage_list, detail,
                                             &lv_font_montserrat_14,
                                             UI_COLOR_MUTED);
    lv_obj_set_width(detail_label, STORAGE_PAGE_CONTENT_WIDTH);
    lv_obj_set_style_text_align(detail_label, LV_TEXT_ALIGN_CENTER, 0);
}

static void storage_page_scan(void)
{
    rt_err_t result;
    bool limited = false;
    size_t index;
    char display_path[STORAGE_PAGE_DISPLAY_PATH_MAX];

    storage_entry_count = 0U;
    storage_selected_index = -1;
    if (storage_page_owner != RT_NULL &&
            storage_page_owner->group != RT_NULL)
    {
        lv_group_remove_all_objs(storage_page_owner->group);
    }
    memset(storage_entry_buttons, 0, sizeof(storage_entry_buttons));
    lv_obj_clean(storage_list);
    storage_service_name_to_utf8(storage_current_path, display_path,
                                 sizeof(display_path));
    lv_label_set_text(storage_path_label, display_path);
    storage_page_update_usage();

    if (!storage_service_is_ready())
    {
        storage_page_show_empty("TF card unavailable",
                                "Insert a mounted FAT card",
                                UI_COLOR_ERROR);
        lv_label_set_text(storage_footer_label,
                          "Storage service is not ready");
        lv_obj_set_style_text_color(storage_footer_label,
                                    lv_color_hex(UI_COLOR_ERROR), 0);
        storage_page_add_group_objects();
        return;
    }

    if (storage_entries == RT_NULL)
    {
        storage_page_show_empty("Not enough memory",
                                "Cannot allocate directory list",
                                UI_COLOR_ERROR);
        lv_label_set_text(storage_footer_label,
                          "Directory buffer allocation failed");
        lv_obj_set_style_text_color(storage_footer_label,
                                    lv_color_hex(UI_COLOR_ERROR), 0);
        storage_page_add_group_objects();
        return;
    }

    result = storage_service_list_directory(
        storage_current_path, storage_entries,
        STORAGE_PAGE_MAX_ENTRIES, &storage_entry_count, &limited);
    if (result != RT_EOK)
    {
        storage_page_show_empty("Cannot open folder",
                                storage_current_path,
                                UI_COLOR_ERROR);
        lv_label_set_text(storage_footer_label,
                          "Directory scan failed");
        lv_obj_set_style_text_color(storage_footer_label,
                                    lv_color_hex(UI_COLOR_ERROR), 0);
        storage_page_add_group_objects();
        return;
    }

    if (storage_entry_count == 0U)
    {
        storage_page_show_empty("This folder is empty",
                                "No files or folders found",
                                UI_COLOR_MUTED);
    }
    else
    {
        for (index = 0U; index < storage_entry_count; index++)
            storage_page_create_row(index);
    }

    if (limited)
    {
        if (storage_entry_count >= STORAGE_PAGE_MAX_ENTRIES)
        {
            lv_label_set_text_fmt(
                storage_footer_label,
                "Showing first %u items - additional items hidden",
                (unsigned int)STORAGE_PAGE_MAX_ENTRIES);
        }
        else
        {
            lv_label_set_text_fmt(
                storage_footer_label,
                "Scan limited after %u item%s",
                (unsigned int)storage_entry_count,
                storage_entry_count == 1U ? "" : "s");
        }
        lv_obj_set_style_text_color(storage_footer_label,
                                    lv_color_hex(UI_COLOR_WARNING), 0);
    }
    else
    {
        lv_label_set_text_fmt(storage_footer_label, "%u item%s",
                              (unsigned int)storage_entry_count,
                              storage_entry_count == 1U ? "" : "s");
        lv_obj_set_style_text_color(storage_footer_label,
                                    lv_color_hex(UI_COLOR_MUTED), 0);
    }
    storage_page_add_group_objects();
}

static void storage_page_back_clicked(lv_event_t *event)
{
    (void)event;
    ui_navigation_switch_async(UI_PAGE_HOME);
}

static void storage_page_refresh_clicked(lv_event_t *event)
{
    (void)event;
    storage_page_scan();
}

static void storage_page_up_clicked(lv_event_t *event)
{
    (void)event;
    if (!storage_page_is_root())
    {
        storage_page_parent_path();
        storage_page_scan();
    }
}

static void storage_page_create_header(ui_page_t *page)
{
    lv_obj_t *title;

    storage_back_button = storage_page_create_header_button(
        page->root, 12, LV_SYMBOL_LEFT, storage_page_back_clicked);
    title = storage_page_create_label(page->root, "FILES",
                                      &lv_font_montserrat_24,
                                      UI_COLOR_TEXT);
    lv_obj_set_size(title, 220, 34);
    lv_obj_set_pos(title, 72, STORAGE_PAGE_HEADER_Y + 6);
    storage_refresh_button = storage_page_create_header_button(
        page->root, UI_SCREEN_WIDTH - 60, LV_SYMBOL_REFRESH,
        storage_page_refresh_clicked);
}

static void storage_page_create_location(lv_obj_t *parent)
{
    lv_obj_t *location = lv_obj_create(parent);
    lv_obj_t *up_icon;

    lv_obj_set_size(location, STORAGE_PAGE_CONTENT_WIDTH, 52);
    lv_obj_set_pos(location, STORAGE_PAGE_SIDE_PADDING,
                   STORAGE_PAGE_LOCATION_Y);
    storage_page_style_container(location, UI_COLOR_PANEL,
                                 LV_OPA_COVER);
    lv_obj_remove_flag(location, LV_OBJ_FLAG_SCROLLABLE);

    storage_up_button = lv_button_create(location);
    lv_obj_set_size(storage_up_button, 44, 44);
    lv_obj_set_pos(storage_up_button, 4, 4);
    storage_page_style_button(storage_up_button, UI_COLOR_PANEL);
    lv_obj_add_event_cb(storage_up_button, storage_page_up_clicked,
                        LV_EVENT_CLICKED, RT_NULL);
    up_icon = storage_page_create_label(storage_up_button,
                                        LV_SYMBOL_UP,
                                        &lv_font_montserrat_20,
                                        UI_COLOR_TEXT);
    lv_obj_center(up_icon);

    storage_path_label = storage_page_create_label(
        location, "/", &lv_font_source_han_sans_sc_16_cjk, UI_COLOR_TEXT);
    lv_obj_set_size(storage_path_label, 270, 24);
    lv_label_set_long_mode(storage_path_label, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(storage_path_label, 58, 14);

    storage_usage_label = storage_page_create_label(
        location, "", &lv_font_montserrat_12, UI_COLOR_MUTED);
    lv_obj_set_size(storage_usage_label, 100, 20);
    lv_obj_set_style_text_align(storage_usage_label,
                                LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(storage_usage_label, 336, 16);
}

static void storage_page_on_create(ui_page_t *page)
{
    const char *mount_path;

    if (page == RT_NULL || page->root == RT_NULL)
        return;

    storage_page_owner = page;
    mount_path = storage_service_get_mount_path();
    rt_strncpy(storage_current_path, mount_path,
               sizeof(storage_current_path) - 1U);
    storage_current_path[sizeof(storage_current_path) - 1U] = '\0';
    storage_entries = t_sf32_psram_calloc(STORAGE_PAGE_MAX_ENTRIES,
                                          sizeof(*storage_entries));
    if (page->group != RT_NULL)
        lv_group_set_wrap(page->group, true);

    storage_page_create_header(page);
    storage_page_create_location(page->root);

    storage_list = lv_obj_create(page->root);
    lv_obj_set_size(storage_list, STORAGE_PAGE_CONTENT_WIDTH,
                    STORAGE_PAGE_LIST_HEIGHT);
    lv_obj_set_pos(storage_list, STORAGE_PAGE_SIDE_PADDING,
                   STORAGE_PAGE_LIST_Y);
    storage_page_style_container(storage_list, UI_COLOR_PANEL,
                                 LV_OPA_COVER);
    lv_obj_set_style_pad_all(storage_list, 0, 0);
    lv_obj_set_style_pad_row(storage_list, 0, 0);
    lv_obj_set_flex_flow(storage_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(storage_list, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(storage_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(storage_list, LV_SCROLLBAR_MODE_AUTO);

    storage_footer_label = storage_page_create_label(
        page->root, "", &lv_font_source_han_sans_sc_16_cjk, UI_COLOR_MUTED);
    lv_obj_set_size(storage_footer_label, STORAGE_PAGE_CONTENT_WIDTH, 22);
    lv_label_set_long_mode(storage_footer_label, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(storage_footer_label, STORAGE_PAGE_SIDE_PADDING, 442);
}

static void storage_page_on_enter(ui_page_t *page)
{
    (void)page;
    storage_page_scan();
}

static void storage_page_on_destroy(ui_page_t *page)
{
    (void)page;
    storage_page_owner = RT_NULL;
    storage_back_button = RT_NULL;
    storage_refresh_button = RT_NULL;
    storage_up_button = RT_NULL;
    storage_path_label = RT_NULL;
    storage_usage_label = RT_NULL;
    storage_list = RT_NULL;
    storage_footer_label = RT_NULL;
    storage_entry_count = 0U;
    storage_selected_index = -1;
    if (storage_entries != RT_NULL)
    {
        t_sf32_psram_free(storage_entries);
        storage_entries = RT_NULL;
    }
    memset(storage_entry_buttons, 0, sizeof(storage_entry_buttons));
}

static void storage_page_go_back(ui_page_t *page)
{
    (void)page;
    if (storage_page_is_root())
    {
        ui_navigation_switch_async(UI_PAGE_HOME);
    }
    else
    {
        storage_page_parent_path();
        storage_page_scan();
    }
}

static void storage_page_on_gesture(ui_page_t *page,
                                    ui_gesture_t gesture)
{
    if (gesture == UI_GESTURE_RIGHT)
        storage_page_go_back(page);
}

static void storage_page_on_event(ui_page_t *page,
                                  const ui_event_t *event)
{
    if (page == RT_NULL || event == RT_NULL ||
            event->type != UI_EVENT_KEY)
        return;

    if (event->data == LV_KEY_HOME)
        ui_navigation_switch_async(UI_PAGE_HOME);
    else if (event->data == LV_KEY_ESC)
        storage_page_go_back(page);
}

rt_err_t storage_page_register(void)
{
    const ui_page_t page = {
        .id = UI_PAGE_STORAGE,
        .app_id = UI_APP_NONE,
        .name = "Storage Page",
        .on_create = storage_page_on_create,
        .on_enter = storage_page_on_enter,
        .on_destroy = storage_page_on_destroy,
        .on_gesture = storage_page_on_gesture,
        .on_event = storage_page_on_event,
    };

    return ui_navigation_register_page(&page);
}
