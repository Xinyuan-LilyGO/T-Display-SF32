#include "calendar_page.h"

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include <lvgl.h>

#include "time_service.h"
#include "ui_config.h"
#include "ui_navigation.h"
#include "ui_status_bar.h"
#include "ui_theme.h"

#define CALENDAR_HEADER_Y       (UI_STATUS_BAR_HEIGHT + 4)
#define CALENDAR_CELL_COUNT     42U
#define CALENDAR_COLUMN_COUNT    7U
#define CALENDAR_UPDATE_MS    1000U

static const char *const calendar_month_names[12] = {
    "JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE",
    "JULY", "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER",
};
static const char *const calendar_week_names[7] = {
    "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT",
};

static lv_obj_t *calendar_date_label;
static lv_obj_t *calendar_day_label;
static lv_obj_t *calendar_month_label;
static lv_obj_t *calendar_sync_label;
static lv_obj_t *calendar_cells[CALENDAR_CELL_COUNT];
static int calendar_display_year = -1;
static int calendar_display_month = -1;
static int calendar_display_day = -1;

static lv_obj_t *calendar_page_create_label(lv_obj_t *parent,
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

static bool calendar_page_is_leap_year(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static int calendar_page_days_in_month(int year, int month)
{
    static const uint8_t days[12] = {
        31U, 28U, 31U, 30U, 31U, 30U,
        31U, 31U, 30U, 31U, 30U, 31U,
    };

    if (month == 2 && calendar_page_is_leap_year(year))
        return 29;
    return days[month - 1];
}

static int calendar_page_first_weekday(int year, int month)
{
    int adjusted_year = year;
    int adjusted_month = month;
    int century;
    int year_of_century;
    int weekday;

    if (adjusted_month < 3)
    {
        adjusted_month += 12;
        adjusted_year--;
    }
    century = adjusted_year / 100;
    year_of_century = adjusted_year % 100;
    weekday = (1 + (13 * (adjusted_month + 1)) / 5 +
               year_of_century + year_of_century / 4 + century / 4 +
               5 * century) % 7;
    return (weekday + 6) % 7;
}

static void calendar_page_show_waiting(void)
{
    lv_label_set_text(calendar_date_label, "--");
    lv_label_set_text(calendar_day_label, "WAITING FOR TIME");
    lv_label_set_text(calendar_month_label, "CALENDAR");
    lv_label_set_text(calendar_sync_label, "WAITING FOR TIME SYNC");
    lv_obj_set_style_text_color(calendar_sync_label,
                                lv_color_hex(UI_COLOR_WARNING), 0);
    for (uint32_t index = 0U; index < CALENDAR_CELL_COUNT; index++)
    {
        lv_label_set_text(calendar_cells[index], "");
        lv_obj_set_style_bg_opa(calendar_cells[index], LV_OPA_TRANSP, 0);
    }
    calendar_display_year = -1;
    calendar_display_month = -1;
    calendar_display_day = -1;
}

static void calendar_page_update(void)
{
    static const char *const day_names[7] = {
        "SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY",
        "THURSDAY", "FRIDAY", "SATURDAY",
    };
    time_t now;
    struct tm *current;
    int year;
    int month;
    int day;
    int first_weekday;
    int month_days;

    if (calendar_date_label == RT_NULL)
        return;
    if (!time_service_is_valid())
    {
        calendar_page_show_waiting();
        return;
    }

    now = time(RT_NULL);
    current = localtime(&now);
    if (current == RT_NULL)
    {
        calendar_page_show_waiting();
        return;
    }

    year = current->tm_year + 1900;
    month = current->tm_mon + 1;
    day = current->tm_mday;
    if (year == calendar_display_year && month == calendar_display_month &&
            day == calendar_display_day)
        return;

    calendar_display_year = year;
    calendar_display_month = month;
    calendar_display_day = day;
    lv_label_set_text_fmt(calendar_date_label, "%02d", day);
    lv_label_set_text(calendar_day_label, day_names[current->tm_wday]);
    lv_label_set_text_fmt(calendar_month_label, "%s  %d",
                          calendar_month_names[month - 1], year);
    lv_label_set_text(calendar_sync_label, "UTC+8  TIME SYNCED");
    lv_obj_set_style_text_color(calendar_sync_label,
                                lv_color_hex(UI_COLOR_SUCCESS), 0);

    first_weekday = calendar_page_first_weekday(year, month);
    month_days = calendar_page_days_in_month(year, month);
    for (uint32_t index = 0U; index < CALENDAR_CELL_COUNT; index++)
    {
        int cell_day = (int)index - first_weekday + 1;
        bool selected = cell_day == day;

        if (cell_day < 1 || cell_day > month_days)
            lv_label_set_text(calendar_cells[index], "");
        else
            lv_label_set_text_fmt(calendar_cells[index], "%d", cell_day);
        lv_obj_set_style_bg_color(calendar_cells[index],
                                  lv_color_hex(UI_COLOR_PRIMARY), 0);
        lv_obj_set_style_bg_opa(calendar_cells[index],
                                selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(
            calendar_cells[index],
            lv_color_hex(selected ? UI_COLOR_BACKGROUND : UI_COLOR_TEXT), 0);
    }
}

static void calendar_page_back_clicked(lv_event_t *event)
{
    (void)event;
    ui_navigation_switch_async(UI_PAGE_HOME);
}

static void calendar_page_on_create(ui_page_t *page)
{
    lv_obj_t *back;
    lv_obj_t *label;
    lv_obj_t *summary;

    if (page == RT_NULL || page->root == RT_NULL)
        return;

    back = lv_button_create(page->root);
    lv_obj_set_size(back, 48, 44);
    lv_obj_set_pos(back, 12, CALENDAR_HEADER_Y);
    lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(back, 0, 0);
    lv_obj_set_style_radius(back, 4, 0);
    lv_obj_set_style_pad_all(back, 0, 0);
    ui_theme_apply_focus_style(back);
    lv_obj_add_event_cb(back, calendar_page_back_clicked,
                        LV_EVENT_CLICKED, RT_NULL);
    label = calendar_page_create_label(back, LV_SYMBOL_LEFT,
                                       &lv_font_montserrat_24,
                                       UI_COLOR_TEXT);
    lv_obj_center(label);
    if (page->group != RT_NULL)
        lv_group_add_obj(page->group, back);

    label = calendar_page_create_label(page->root, "CALENDAR",
                                       &lv_font_montserrat_24,
                                       UI_COLOR_TEXT);
    lv_obj_set_pos(label, 72, CALENDAR_HEADER_Y + 6);

    calendar_sync_label = calendar_page_create_label(
        page->root, "WAITING FOR TIME", &lv_font_montserrat_12,
        UI_COLOR_WARNING);
    lv_obj_set_size(calendar_sync_label, 220, 22);
    lv_obj_set_style_text_align(calendar_sync_label,
                                LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(calendar_sync_label, 242, CALENDAR_HEADER_Y + 11);

    summary = lv_obj_create(page->root);
    lv_obj_set_size(summary, 448, 100);
    lv_obj_set_pos(summary, 16, 96);
    lv_obj_set_style_bg_color(summary, lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(summary, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(summary, 0, 0);
    lv_obj_set_style_radius(summary, 4, 0);
    lv_obj_set_style_pad_all(summary, 0, 0);
    lv_obj_remove_flag(summary, LV_OBJ_FLAG_SCROLLABLE);

    calendar_date_label = calendar_page_create_label(
        summary, "--", &lv_font_montserrat_48, UI_COLOR_PRIMARY);
    lv_obj_set_size(calendar_date_label, 100, 58);
    lv_obj_set_style_text_align(calendar_date_label,
                                LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(calendar_date_label, 14, 17);

    calendar_day_label = calendar_page_create_label(
        summary, "WAITING FOR TIME", &lv_font_montserrat_20,
        UI_COLOR_TEXT);
    lv_obj_set_size(calendar_day_label, 300, 30);
    lv_obj_set_pos(calendar_day_label, 130, 23);

    calendar_month_label = calendar_page_create_label(
        summary, "CALENDAR", &lv_font_montserrat_14, UI_COLOR_MUTED);
    lv_obj_set_size(calendar_month_label, 300, 24);
    lv_obj_set_pos(calendar_month_label, 130, 58);

    for (uint32_t column = 0U; column < CALENDAR_COLUMN_COUNT; column++)
    {
        label = calendar_page_create_label(
            page->root, calendar_week_names[column],
            &lv_font_montserrat_12,
            column == 0U || column == 6U ?
                UI_COLOR_SECONDARY : UI_COLOR_MUTED);
        lv_obj_set_size(label, 52, 20);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(label, 31 + (int32_t)column * 61, 211);
    }

    for (uint32_t index = 0U; index < CALENDAR_CELL_COUNT; index++)
    {
        uint32_t row = index / CALENDAR_COLUMN_COUNT;
        uint32_t column = index % CALENDAR_COLUMN_COUNT;

        calendar_cells[index] = calendar_page_create_label(
            page->root, "", &lv_font_montserrat_16, UI_COLOR_TEXT);
        lv_obj_set_size(calendar_cells[index], 40, 34);
        lv_obj_set_pos(calendar_cells[index],
                       37 + (int32_t)column * 61,
                       240 + (int32_t)row * 38);
        lv_obj_set_style_text_align(calendar_cells[index],
                                    LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(calendar_cells[index], 7, 0);
        lv_obj_set_style_radius(calendar_cells[index], 4, 0);
    }

    calendar_display_year = -1;
    calendar_display_month = -1;
    calendar_display_day = -1;
    calendar_page_update();
}

static void calendar_page_on_destroy(ui_page_t *page)
{
    (void)page;
    calendar_date_label = RT_NULL;
    calendar_day_label = RT_NULL;
    calendar_month_label = RT_NULL;
    calendar_sync_label = RT_NULL;
    for (uint32_t index = 0U; index < CALENDAR_CELL_COUNT; index++)
        calendar_cells[index] = RT_NULL;
}

static void calendar_page_on_timer(ui_page_t *page)
{
    (void)page;
    calendar_page_update();
}

static void calendar_page_on_gesture(ui_page_t *page,
                                     ui_gesture_t gesture)
{
    (void)page;
    if (gesture == UI_GESTURE_RIGHT)
        ui_navigation_switch_async(UI_PAGE_HOME);
}

static void calendar_page_on_event(ui_page_t *page,
                                   const ui_event_t *event)
{
    (void)page;
    if (event != RT_NULL && event->type == UI_EVENT_KEY &&
            (event->data == LV_KEY_ESC || event->data == LV_KEY_HOME))
        ui_navigation_switch_async(UI_PAGE_HOME);
}

rt_err_t calendar_page_register(void)
{
    const ui_page_t page = {
        .id = UI_PAGE_CALENDAR,
        .app_id = UI_APP_NONE,
        .name = "Calendar Page",
        .on_create = calendar_page_on_create,
        .on_destroy = calendar_page_on_destroy,
        .on_timer = calendar_page_on_timer,
        .on_gesture = calendar_page_on_gesture,
        .on_event = calendar_page_on_event,
        .timer_interval_ms = CALENDAR_UPDATE_MS,
    };

    return ui_navigation_register_page(&page);
}
