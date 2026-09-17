#include "weather_page.h"

#include <string.h>

#include <lvgl.h>

#include "ui_config.h"
#include "ui_navigation.h"
#include "ui_status_bar.h"
#include "ui_theme.h"
#include "weather_service.h"

#define WEATHER_HEADER_Y          (UI_STATUS_BAR_HEIGHT + 4)
#define WEATHER_SIDE_PADDING      16
#define WEATHER_SUMMARY_Y         100
#define WEATHER_SUMMARY_HEIGHT    72
#define WEATHER_LIST_Y            184
#define WEATHER_ROW_HEIGHT        54
#define WEATHER_UPDATE_INTERVAL_MS 250U

static lv_obj_t *weather_city_label;
static lv_obj_t *weather_status_label;
static lv_obj_t *weather_date_labels[WEATHER_SERVICE_FORECAST_DAYS];
static lv_obj_t *weather_condition_labels[WEATHER_SERVICE_FORECAST_DAYS];
static lv_obj_t *weather_temperature_labels[WEATHER_SERVICE_FORECAST_DAYS];
static weather_service_state_t weather_last_state = WEATHER_SERVICE_IDLE;
static char weather_last_city[WEATHER_SERVICE_CITY_MAX];

static lv_obj_t *weather_page_create_label(lv_obj_t *parent,
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

static void weather_page_back_clicked(lv_event_t *event)
{
    (void)event;
    ui_navigation_switch_async(UI_PAGE_HOME);
}

static void weather_page_refresh_clicked(lv_event_t *event)
{
    rt_err_t result;

    (void)event;
    result = weather_service_request();
    if (result != RT_EOK && result != -RT_EBUSY)
        weather_last_state = WEATHER_SERVICE_IDLE;
}

static const char *weather_page_weekday(const char *date)
{
    static const char *const weekdays[] = {
        "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT",
    };
    int year;
    int month;
    int day;
    int adjustment;

    if (date == RT_NULL || strlen(date) < 10U || date[4] != '-' ||
            date[7] != '-')
        return "---";
    year = (date[0] - '0') * 1000 + (date[1] - '0') * 100 +
           (date[2] - '0') * 10 + date[3] - '0';
    month = (date[5] - '0') * 10 + date[6] - '0';
    day = (date[8] - '0') * 10 + date[9] - '0';
    if (year < 2000 || month < 1 || month > 12 || day < 1 || day > 31)
        return "---";
    if (month < 3)
    {
        month += 12;
        year--;
    }
    adjustment = (day + (13 * (month + 1)) / 5 + year + year / 4 -
                  year / 100 + year / 400) % 7;
    return weekdays[(adjustment + 6) % 7];
}

static void weather_page_clear_rows(void)
{
    uint32_t index;

    for (index = 0U; index < WEATHER_SERVICE_FORECAST_DAYS; index++)
    {
        lv_label_set_text(weather_date_labels[index], "---  --/--");
        lv_label_set_text(weather_condition_labels[index], "Waiting");
        lv_label_set_text(weather_temperature_labels[index], "-- / -- C");
    }
}

static void weather_page_update(void)
{
    weather_service_snapshot_t snapshot;
    uint32_t index;

    if (weather_city_label == RT_NULL ||
            weather_service_get_snapshot(&snapshot) != RT_EOK)
        return;
    if (snapshot.state == weather_last_state &&
            strcmp(snapshot.city, weather_last_city) == 0)
        return;

    weather_last_state = snapshot.state;
    rt_snprintf(weather_last_city, sizeof(weather_last_city), "%s",
                snapshot.city);
    switch (snapshot.state)
    {
    case WEATHER_SERVICE_LOADING:
        lv_label_set_text(weather_city_label, "LOCATING BY IP");
        lv_label_set_text(weather_status_label, "FETCHING 5-DAY FORECAST");
        lv_obj_set_style_text_color(weather_status_label,
                                    lv_color_hex(UI_COLOR_WARNING), 0);
        weather_page_clear_rows();
        break;

    case WEATHER_SERVICE_READY:
        lv_label_set_text(weather_city_label, snapshot.city);
        lv_label_set_text(weather_status_label, "BLUETOOTH PAN / CURRENT IP");
        lv_obj_set_style_text_color(weather_status_label,
                                    lv_color_hex(UI_COLOR_SUCCESS), 0);
        for (index = 0U; index < WEATHER_SERVICE_FORECAST_DAYS; index++)
        {
            const weather_service_day_t *day = &snapshot.days[index];
            const char *month_day = strlen(day->date) >= 10U ?
                                    day->date + 5 : "--/--";

            lv_label_set_text_fmt(weather_date_labels[index], "%s  %.5s",
                                  weather_page_weekday(day->date), month_day);
            lv_label_set_text(weather_condition_labels[index],
                              day->condition);
            lv_label_set_text_fmt(weather_temperature_labels[index],
                                  "%d / %d C", (int)day->low_c,
                                  (int)day->high_c);
        }
        break;

    case WEATHER_SERVICE_NO_PAN:
        lv_label_set_text(weather_city_label, "PAN NOT CONNECTED");
        lv_label_set_text(weather_status_label, "ENABLE PAN IN SETTINGS");
        lv_obj_set_style_text_color(weather_status_label,
                                    lv_color_hex(UI_COLOR_ERROR), 0);
        weather_page_clear_rows();
        break;

    case WEATHER_SERVICE_DATA_ERROR:
        lv_label_set_text(weather_city_label, "WEATHER UNAVAILABLE");
        lv_label_set_text(weather_status_label, "INVALID FORECAST DATA");
        lv_obj_set_style_text_color(weather_status_label,
                                    lv_color_hex(UI_COLOR_ERROR), 0);
        weather_page_clear_rows();
        break;

    case WEATHER_SERVICE_NETWORK_ERROR:
        lv_label_set_text(weather_city_label, "NETWORK ERROR");
        lv_label_set_text(weather_status_label, "CHECK BLUETOOTH PAN");
        lv_obj_set_style_text_color(weather_status_label,
                                    lv_color_hex(UI_COLOR_ERROR), 0);
        weather_page_clear_rows();
        break;

    case WEATHER_SERVICE_IDLE:
    default:
        lv_label_set_text(weather_city_label, "WEATHER");
        lv_label_set_text(weather_status_label, "PRESS REFRESH");
        weather_page_clear_rows();
        break;
    }
}

static void weather_page_create_header(ui_page_t *page)
{
    lv_obj_t *button;
    lv_obj_t *label;

    button = lv_button_create(page->root);
    lv_obj_set_size(button, 48, 44);
    lv_obj_set_pos(button, 12, WEATHER_HEADER_Y);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_radius(button, 4, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    ui_theme_apply_focus_style(button);
    lv_obj_add_event_cb(button, weather_page_back_clicked,
                        LV_EVENT_CLICKED, RT_NULL);
    label = weather_page_create_label(button, LV_SYMBOL_LEFT,
                                      &lv_font_montserrat_24,
                                      UI_COLOR_TEXT);
    lv_obj_center(label);
    if (page->group != RT_NULL)
        lv_group_add_obj(page->group, button);

    label = weather_page_create_label(page->root, "WEATHER",
                                      &lv_font_montserrat_24,
                                      UI_COLOR_TEXT);
    lv_obj_set_pos(label, 72, WEATHER_HEADER_Y + 6);

    button = lv_button_create(page->root);
    lv_obj_set_size(button, 48, 44);
    lv_obj_set_pos(button, 420, WEATHER_HEADER_Y);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_radius(button, 4, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    ui_theme_apply_focus_style(button);
    lv_obj_add_event_cb(button, weather_page_refresh_clicked,
                        LV_EVENT_CLICKED, RT_NULL);
    label = weather_page_create_label(button, LV_SYMBOL_REFRESH,
                                      &lv_font_montserrat_22,
                                      UI_COLOR_PRIMARY);
    lv_obj_center(label);
    if (page->group != RT_NULL)
        lv_group_add_obj(page->group, button);
}

static void weather_page_create_content(ui_page_t *page)
{
    lv_obj_t *summary;
    lv_obj_t *list;
    uint32_t index;

    summary = lv_obj_create(page->root);
    lv_obj_set_size(summary, UI_SCREEN_WIDTH - WEATHER_SIDE_PADDING * 2,
                    WEATHER_SUMMARY_HEIGHT);
    lv_obj_set_pos(summary, WEATHER_SIDE_PADDING, WEATHER_SUMMARY_Y);
    lv_obj_set_style_bg_color(summary, lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(summary, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(summary, 0, 0);
    lv_obj_set_style_radius(summary, 4, 0);
    lv_obj_set_style_pad_all(summary, 0, 0);
    lv_obj_remove_flag(summary, LV_OBJ_FLAG_SCROLLABLE);

    weather_city_label = weather_page_create_label(
        summary, "WEATHER", &lv_font_montserrat_22, UI_COLOR_TEXT);
    lv_obj_set_size(weather_city_label, 416, 30);
    lv_label_set_long_mode(weather_city_label, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(weather_city_label, 16, 10);

    weather_status_label = weather_page_create_label(
        summary, "PRESS REFRESH", &lv_font_montserrat_12,
        UI_COLOR_MUTED);
    lv_obj_set_size(weather_status_label, 416, 20);
    lv_obj_set_pos(weather_status_label, 16, 43);

    list = lv_obj_create(page->root);
    lv_obj_set_size(list, UI_SCREEN_WIDTH - WEATHER_SIDE_PADDING * 2,
                    WEATHER_ROW_HEIGHT * WEATHER_SERVICE_FORECAST_DAYS);
    lv_obj_set_pos(list, WEATHER_SIDE_PADDING, WEATHER_LIST_Y);
    lv_obj_set_style_bg_color(list, lv_color_hex(UI_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_radius(list, 4, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_remove_flag(list, LV_OBJ_FLAG_SCROLLABLE);

    for (index = 0U; index < WEATHER_SERVICE_FORECAST_DAYS; index++)
    {
        lv_coord_t y = (lv_coord_t)index * WEATHER_ROW_HEIGHT;

        weather_date_labels[index] = weather_page_create_label(
            list, "---  --/--", &lv_font_montserrat_14,
            index == 0U ? UI_COLOR_PRIMARY : UI_COLOR_MUTED);
        lv_obj_set_size(weather_date_labels[index], 114, 22);
        lv_obj_set_pos(weather_date_labels[index], 14, y + 16);

        weather_condition_labels[index] = weather_page_create_label(
            list, "Waiting", &lv_font_montserrat_16, UI_COLOR_TEXT);
        lv_obj_set_size(weather_condition_labels[index], 154, 24);
        lv_label_set_long_mode(weather_condition_labels[index],
                               LV_LABEL_LONG_DOT);
        lv_obj_set_pos(weather_condition_labels[index], 140, y + 14);

        weather_temperature_labels[index] = weather_page_create_label(
            list, "-- / -- C", &lv_font_montserrat_16,
            UI_COLOR_SECONDARY);
        lv_obj_set_size(weather_temperature_labels[index], 132, 24);
        lv_obj_set_style_text_align(weather_temperature_labels[index],
                                    LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(weather_temperature_labels[index], 302, y + 14);

        if (index + 1U < WEATHER_SERVICE_FORECAST_DAYS)
        {
            lv_obj_t *separator = lv_obj_create(list);

            lv_obj_set_size(separator, 420, 1);
            lv_obj_set_pos(separator, 14, y + WEATHER_ROW_HEIGHT - 1);
            lv_obj_set_style_bg_color(separator,
                                      lv_color_hex(UI_COLOR_TRACK), 0);
            lv_obj_set_style_bg_opa(separator, LV_OPA_60, 0);
            lv_obj_set_style_border_width(separator, 0, 0);
            lv_obj_set_style_pad_all(separator, 0, 0);
            lv_obj_remove_flag(separator, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_remove_flag(separator, LV_OBJ_FLAG_CLICKABLE);
        }
    }
}

static void weather_page_on_create(ui_page_t *page)
{
    if (page == RT_NULL || page->root == RT_NULL)
        return;

    memset(weather_date_labels, 0, sizeof(weather_date_labels));
    memset(weather_condition_labels, 0, sizeof(weather_condition_labels));
    memset(weather_temperature_labels, 0,
           sizeof(weather_temperature_labels));
    weather_page_create_header(page);
    weather_page_create_content(page);
    weather_last_state = WEATHER_SERVICE_IDLE;
    weather_last_city[0] = '\0';
    weather_page_clear_rows();
}

static void weather_page_on_enter(ui_page_t *page)
{
    (void)page;
    weather_last_state = WEATHER_SERVICE_IDLE;
    weather_last_city[0] = '\0';
    (void)weather_service_request();
    weather_page_update();
}

static void weather_page_on_timer(ui_page_t *page)
{
    (void)page;
    weather_page_update();
}

static void weather_page_on_destroy(ui_page_t *page)
{
    (void)page;
    weather_city_label = RT_NULL;
    weather_status_label = RT_NULL;
    memset(weather_date_labels, 0, sizeof(weather_date_labels));
    memset(weather_condition_labels, 0, sizeof(weather_condition_labels));
    memset(weather_temperature_labels, 0,
           sizeof(weather_temperature_labels));
}

static void weather_page_on_gesture(ui_page_t *page,
                                    ui_gesture_t gesture)
{
    (void)page;
    if (gesture == UI_GESTURE_RIGHT)
        ui_navigation_switch_async(UI_PAGE_HOME);
}

static void weather_page_on_event(ui_page_t *page,
                                  const ui_event_t *event)
{
    (void)page;
    if (event != RT_NULL && event->type == UI_EVENT_KEY &&
            (event->data == LV_KEY_ESC || event->data == LV_KEY_HOME))
        ui_navigation_switch_async(UI_PAGE_HOME);
}

rt_err_t weather_page_register(void)
{
    const ui_page_t page = {
        .id = UI_PAGE_WEATHER,
        .app_id = UI_APP_NONE,
        .name = "Weather Page",
        .on_create = weather_page_on_create,
        .on_destroy = weather_page_on_destroy,
        .on_enter = weather_page_on_enter,
        .on_timer = weather_page_on_timer,
        .on_gesture = weather_page_on_gesture,
        .on_event = weather_page_on_event,
        .timer_interval_ms = WEATHER_UPDATE_INTERVAL_MS,
    };

    return ui_navigation_register_page(&page);
}
