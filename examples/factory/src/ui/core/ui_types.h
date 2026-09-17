#ifndef FACTORY_UI_TYPES_H
#define FACTORY_UI_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    UI_PAGE_START = 0,
    UI_PAGE_DEVICE_ERROR,
    UI_PAGE_HOME,
    UI_PAGE_MUSIC,
    UI_PAGE_RECORDER,
    UI_PAGE_LORA,
    UI_PAGE_CODEX,
    UI_PAGE_WEATHER,
    UI_PAGE_CHARGE,
    UI_PAGE_STORAGE,
    UI_PAGE_SENSORS,
    UI_PAGE_GPS,
    UI_PAGE_ESP32C6,
    UI_PAGE_POMODORO,
    UI_PAGE_CALENDAR,
    UI_PAGE_CUBE,
    UI_PAGE_SNAKE,
    UI_PAGE_TETRIS,
    UI_PAGE_INFRARED,
    UI_PAGE_INPUT,
    UI_PAGE_SETTINGS,
    UI_PAGE_COUNT,
} ui_page_id_t;

typedef enum
{
    UI_APP_NONE = 0,
    UI_APP_COUNT,
} ui_app_id_t;

typedef enum
{
    UI_GESTURE_NONE  = 0,
    UI_GESTURE_LEFT  = (1U << 0),
    UI_GESTURE_RIGHT = (1U << 1),
    UI_GESTURE_UP    = (1U << 2),
    UI_GESTURE_DOWN  = (1U << 3),
} ui_gesture_t;

typedef enum
{
    UI_EVENT_NONE = 0,
    UI_EVENT_PAGE_SWITCH,
    UI_EVENT_KEY,
    UI_EVENT_BATTERY_CHANGED,
    UI_EVENT_BLUETOOTH_CHANGED,
    UI_EVENT_WIFI_CHANGED,
    UI_EVENT_GPS_CHANGED,
    UI_EVENT_TIME_CHANGED,
    UI_EVENT_STORAGE_CHANGED,
    UI_EVENT_SENSOR_CHANGED,
    UI_EVENT_INPUT_CHANGED,
    UI_EVENT_VOLUME_CHANGED,
    UI_EVENT_POMODORO_COMPLETE,
    UI_EVENT_LOW_BATTERY_CHANGED,
    UI_EVENT_INIT_STATUS_CHANGED,
    UI_EVENT_CUSTOM,
    UI_EVENT_COUNT,
} ui_event_type_t;

typedef enum
{
    UI_STATUS_STATE_OFF = 0,
    UI_STATUS_STATE_IDLE,
    UI_STATUS_STATE_ACTIVE,
    UI_STATUS_STATE_COUNT,
} ui_status_state_t;

#define UI_EVENT_BATTERY_PERCENT_MASK 0xFFU
#define UI_EVENT_BATTERY_CHARGING     (1UL << 8)
#define UI_EVENT_LOW_BATTERY_VOLTAGE_MASK 0xFFFFU
#define UI_EVENT_LOW_BATTERY_SECONDS_SHIFT 16U
#define UI_EVENT_LOW_BATTERY_SECONDS_MASK  0xFFU
#define UI_EVENT_LOW_BATTERY_ACTIVE        (1UL << 31)
#define UI_EVENT_TIME_HOUR_SHIFT      8U
#define UI_EVENT_TIME_MINUTE_MASK     0xFFU
#define UI_EVENT_INPUT_SOURCE_SHIFT   24U
#define UI_EVENT_INPUT_ACTION_SHIFT   16U
#define UI_EVENT_INPUT_CODE_MASK      0xFFFFU
#define UI_EVENT_KEY_CODE_MASK        0xFFFFU
#define UI_EVENT_KEY_LONG_PRESSED     (1UL << 31)

typedef struct
{
    ui_event_type_t type;
    uintptr_t data;
} ui_event_t;

typedef struct ui_page ui_page_t;

typedef void (*ui_page_lifecycle_cb_t)(ui_page_t *page);
typedef void (*ui_page_group_cb_t)(lv_group_t *group);
typedef void (*ui_page_timer_cb_t)(ui_page_t *page);
typedef void (*ui_page_gesture_cb_t)(ui_page_t *page,
                                     ui_gesture_t gesture);
typedef void (*ui_page_event_cb_t)(ui_page_t *page,
                                   const ui_event_t *event);

struct ui_page
{
    lv_obj_t *root;
    lv_group_t *group;
    lv_obj_t *last_group_object;

    const char *name;
    ui_page_id_t id;
    ui_app_id_t app_id;
    bool is_created;

    ui_page_group_cb_t group_cb;
    ui_page_timer_cb_t on_timer;
    ui_page_gesture_cb_t on_gesture;
    ui_page_event_cb_t on_event;
    ui_page_lifecycle_cb_t on_create;
    ui_page_lifecycle_cb_t on_destroy;
    ui_page_lifecycle_cb_t on_enter;
    ui_page_lifecycle_cb_t on_leave;

    uint32_t timer_interval_ms;
    uint32_t timer_elapsed_ms;
    void *user_data;
};

#ifdef __cplusplus
}
#endif

#endif /* FACTORY_UI_TYPES_H */
