#ifndef __UI_H__
#define __UI_H__

#ifdef __cplusplus__
extern "C"
{
#endif

#include "lvgl.h"
#include "littlevgl2rtt.h"
#include "lv_imagebutton.h"
#include "rtthread.h"

#include "bluetooth_manger.h"
#include "weather.h"
#include "chip_work_mode.h"
#include "sd.h"
#include "audio_mp3ctrl.h"
#include "audio_manager_lib.h"
#include "lora_app.h"

#ifdef PKG_USING_PKG_KEY_BOARD
#include "tca8418.h"
#include "aw21009.h"
#include "xl9555.h"
#include "aw86224.h"
#include "bme280.h"
#include "esp32c6_at_cmd.h"
#include "l76k.h"
#include "TinyGPS.h"
#include "uart_mux.h"
#include "infrared.h"
#include "ir_nec.h"
#endif

    enum
    {
        PAGE_START = 0,
        PAGE_HOME,
        PAGE_MUSIC,
        PAGE_LORA,
        PAGE_SENSOR,
        PAGE_GPS,
        PAGE_WIFI,
        PAGE_CHARGE,
        PAGE_WEATHER,
        PAGE_AUDIO,
        PAGE_FILE,
        PAGE_KEYBOARD,
        PAGE_SYSTEM,
        PAGE_SIZE,
    };
    typedef uint8_t PageType;

    enum
    {
        APP_MUSIC = 0,
        APP_LORA,
        APP_SENSOR,
        APP_GPS,
        APP_WIFI,
        APP_CHARGE,
        APP_WEATHER,
        APP_AUDIO,
        APP_FILE,
        APP_KEYBOARD,
        APP_SYSTEM,
        APP_SIZE,
    };
    typedef uint8_t AppType;

    typedef enum
    {
        GESTURE_NONE = 0,
        GESTURE_LEFT = (1 << 0),
        GESTURE_RIGHT = (1 << 1),
        GESTURE_UP = (1 << 2),
        GESTURE_DOWN = (1 << 3),
    } GestureDirection;

    typedef struct Page
    {
        lv_obj_t *root;                        // 页面根容器
        lv_group_t *group;                     // scroll_group指针
        lv_obj_t *last_group_obj;              // 上一个group对象
        lv_timer_t *timer;                     // 页面定时器
        const char *name;                      // 页面名称
        void (*group_cb)(lv_group_t *group);   // 初始化回调
        void (*page_timer)(lv_timer_t *timer); // 初始化回调
        void (*on_create)(struct Page *);      // 初始化回调
        void (*on_destroy)(struct Page *);     // 销毁回调
        void (*on_enter)(struct Page *);       // 页面进入事件
        void (*on_leave)(struct Page *);       // 页面离开事件
        bool is_created;
        PageType id;             // 页面ID
        AppType app_id;          // 应用ID
        uint16_t timer_interval; // 页面定时器间隔
        uint32_t timer_counter;  // 计数器
    } Page;

    typedef struct app_info
    {
        const lv_image_dsc_t *icon_dsc;
        const char *name;
        // info_text text;
        const char *text_title[8];
        const char *text_data[8];
        uint8_t text_count;
        uint8_t id;
    } app_info;

    extern rt_mq_t lvgl_key_mq;

    enum
    {
        MUSIC_EVENT_LAST = 0,
        MUSIC_EVENT_START,
        MUSIC_EVENT_NEXT,
        MUSIC_EVENT_VOLUME,
        MUSIC_EVENT_VOLUME_CHANGE,
        MUSIC_EVENT_MUSIC_LIST_BTN,
        MUSIC_EVENT_MUSIC_LIST_ITEM,
        MUSIC_EVENT_MUSIC_PAGE,
        LORA_EVENT_MODE,
        LORA_EVENT_FREQ,
        LORA_EVENT_POWER,
        LORA_EVENT_BW,
        LORA_EVENT_SF,
        LORA_EVENT_CR,
        LORA_EVENT_SYNC,
        LORA_EVENT_SEND_MESSAGE,
        LORA_EVENT_RECEIVE_MESSAGE,
        SENSOR_EVENT_MOTOR,
        SENSOR_EVENT_IR_SEND,
        CHARGE_EVENT_CURRENT,
        AUDIO_EVENT_BTN_RECORD,
        AUDIO_EVENT_BTN_PLAY,
        AUDIO_EVENT_BTN_ADD_VOLUME,
        AUDIO_EVENT_BTN_DOWN_VOLUME,
        FILE_EVENT_BTN_UP_ONE_LEVEL,
        FILE_EVENT_BTN_DIR,
        FILE_EVENT_BTN_FILE,
        FILE_EVENT_BTN_CLOSE,
        SYSTEM_EVENT_BLUETOOTH,
        SYSTEM_EVENT_WIFI,
        SYSTEM_EVENT_KEY_LED,
        SYSTEM_EVENT_BRIGHTNESS,
        SYSTEM_EVENT_VOLUME,
        SYSTEM_EVENT_SHOTDOWN,
        SYSTEM_EVENT_SHIP_MODE,
        SYSTEM_EVENT_RESTART,
    };
    typedef uint8_t ui_event_type_t;

    enum
    {
        UI_UPDATE_BLUETOOTH_CONNECT = 0,
        UI_UPDATE_BLUETOOTH_DISCONNECT,
        UI_UPDATE_TIME,
        UI_UPDATE_TIME_STOP,
        UI_UPDATE_HOME_BATTERY,
        UI_UPDATE_MUSIC_TITLE,
        UI_UPDATE_MUSIC_ARTIST,
        UI_UPDATE_MUSIC_ALBUM,
        UI_UPDATE_MUSIC_DURATION,
        UI_UPDATE_MUSIC_CURRENT_TIME,
        UI_UPDATE_PROGRESS,
        UI_UPDATE_PAUSE_BTN,
        UI_UPDATE_AUDIO_RECORD_BTN_START,
        UI_UPDATE_AUDIO_RECORD_BTN_ING,
        UI_UPDATE_AUDIO_RECORD_BTN_STOP,
        UI_UPDATE_AUDIO_PLAY_BTN_START,
        UI_UPDATE_AUDIO_PLAY_BTN_ING,
        UI_UPDATE_AUDIO_PLAY_BTN_STOP,
        UI_UPDATE_INIT_ERROR,
        UI_UPDATE_FILE_LIST_READY,
    };
    typedef uint8_t ui_update_type_t; // ✅ 优化：使用 uint8_t

    enum
    {
        INIT_CHARGE_ERROR = 1 << 0,
        INIT_SD_ERROR = 1 << 1,
        INIT_AUDPRC_ERROR = 1 << 2,
        INIT_BLUETOOTH_ERROR = 1 << 3,

        INIT_KEY_BOARD_ERROR = 1 << 4,
        INIT_LORA_SX1262_ERROR = 1 << 5,
        INIT_IMU_BHI260AP_ERROR = 1 << 6,
        INIT_IO_XL9555_ERROR = 1 << 7,

        INIT_SENSOR_BME280_ERROR = 1 << 8,
        INIT_MOTOR_AW86224_ERROR = 1 << 9,
        INIT_LED_AW21009_ERROR = 1 << 10,
        INIT_ESP32C6_ERROR = 1 << 11,

        INIT_GPS_L76K_ERROR = 1 << 12,
        INIT_X_ERROR = 1 << 13,
    };
    typedef uint16_t init_error_t;
    extern init_error_t init_error;

    typedef enum
    {
        ESP32C6_EVENT_SEND_CMD = 0,
        ESP32C6_EVENT_SEND_AT,
        ESP32C6_EVENT_SEND_RST,
        ESP32C6_EVENT_SEND_SNTPTIME,
        ESP32C6_EVENT_SEND_GMR,
        ESP32C6_EVENT_SEND_CIFSR,
        ESP32C6_EVENT_SEND_CWJAP,
        ESP32C6_EVENT_SEND_CWQAP,
        ESP32C6_EVENT_SEND_CWSTATE,
        ESP32C6_EVENT_SEND_GSLP,
        ESP32C6_EVENT_MAX
    } wifi_event_t;

    rt_err_t ui_init(void);

/*UI color */
#define LV_COLOR_THEME_BLACK 0x000000
#define LV_COLOR_THEME_WIHTE 0xffffff
#define LV_COLOR_THEME_GREEN 0x00ff00
#define LV_COLOR_THEME_YELLOW 0xff9d00
#define LV_COLOR_WARM_RED 0xff0027
#define LV_COLOR_BTN_BLUE 0x2195f6
#define LV_COLOR_THEME_GRAY 0xa1a1a1
#define LV_COLOR_THEME_DIM_GRAY 0x3a3a3a

    LV_FONT_DECLARE(lv_font_simsun_16_cjk);

    LV_IMG_DECLARE(_volume_RGB565A8_60x60);
    LV_IMG_DECLARE(_start_RGB565A8_120x120);
    LV_IMG_DECLARE(_battery_RGB565A8_80x80);
    LV_IMG_DECLARE(_imu_RGB565A8_80x80);
    LV_IMG_DECLARE(_gps_RGB565A8_80x80);
    LV_IMG_DECLARE(_wifi_RGB565A8_80x80);
    LV_IMG_DECLARE(_lora_RGB565A8_80x80);
    LV_IMG_DECLARE(_timer_RGB565A8_80x80);
    LV_IMG_DECLARE(_weather_RGB565A8_80x80);
    LV_IMG_DECLARE(_music_RGB565A8_80x80);
    LV_IMG_DECLARE(_recording_RGB565A8_80x80);
    LV_IMG_DECLARE(_file_RGB565A8_80x80);
    LV_IMG_DECLARE(_keyboard_RGB565A8_80x80);
    LV_IMG_DECLARE(_info_RGB565A8_80x80);
    LV_IMG_DECLARE(_cloud96_RGB565A8_120x120);
    LV_IMG_DECLARE(_cloudyday96_RGB565A8_120x120);
    LV_IMG_DECLARE(_rain96_RGB565A8_120x120);
    LV_IMG_DECLARE(_sun96_RGB565A8_120x120);

    typedef struct
    {
        lv_obj_t *progress_bar;
        lv_obj_t *status_bar_bluetooth_icon;
        lv_obj_t *status_bar_wifi_icon;
        lv_obj_t *status_bar_gps_icon;
        lv_obj_t *status_bar_time_label;
        lv_obj_t *status_bar_battery_label;
        lv_obj_t *status_bar_battery_icon;
    } home_ui_objects_t;

    typedef struct
    {
        lv_obj_t *music_name_label;
        lv_obj_t *music_artist_label;
        lv_obj_t *volume_img;
        lv_obj_t *volume_slider;
        lv_obj_t *music_pause_btn;
        lv_obj_t *music_pause_btn_label;
        lv_obj_t *voide_end_time_label;
        lv_obj_t *voide_current_time_label;
        lv_obj_t *music_progress_bar;
        // lv_obj_t *music_bar_cont[8];
    } music_ui_objects_t;

    typedef struct
    {
        lv_obj_t *lora_config_cont;
        lv_obj_t *lora_send_recevice_msg_cont;
        lv_obj_t *rx_msg_label;
        lv_obj_t *tx_msg_label;
        lv_obj_t *rx_rssi_label;
        lv_obj_t *rx_snr_label;
        lv_timer_t *lora_timer;
        lv_obj_t *focusable[9];

    } lora_ui_objects_t;

    typedef struct
    {
        lv_timer_t *sensor_timer;
        lv_obj_t *sensor_motor_btn;
        lv_obj_t *sensor_yaw_arc;
        lv_obj_t *sensor_pitch_arc;
        lv_obj_t *sensor_roll_arc;
        lv_obj_t *sensor_step_arc;
        lv_obj_t *sensor_yaw_label;
        lv_obj_t *sensor_pitch_label;
        lv_obj_t *sensor_roll_label;
        lv_obj_t *sensor_temp_label;
        lv_obj_t *sensor_press_bar;
        lv_obj_t *sensor_press_label;
        lv_obj_t *sensor_hum_bar;
        lv_obj_t *sensor_hum_label;
        lv_obj_t *sensor_step_label;
        lv_obj_t *sensor_step_bar;
        lv_obj_t *sensor_ir_send_btn;
        lv_obj_t *sensor_ir_send_msg_label;
    } sensor_ui_objects_t;

    typedef struct
    {
        lv_obj_t *gps_time;
        lv_obj_t *gps_data;
        lv_obj_t *gps_lat;
        lv_obj_t *gps_lon;
        lv_obj_t *gps_spd;
        lv_obj_t *gps_crs;
        lv_obj_t *gps_alt;
        lv_obj_t *gps_sats;
        lv_obj_t *gps_dop;
    } gps_ui_objects_t;

    typedef struct
    {
        lv_obj_t *status_label;
        lv_obj_t *cmd_ta;
        lv_obj_t *resp_ta;
        lv_obj_t *send_btn;
    } wifi_ui_objects_t;

    typedef struct
    {
        lv_obj_t *charge_arc;
        lv_obj_t *batter_voltage_percentage_label;
        lv_obj_t *charge_status_label;
        lv_obj_t *batter_voltage_mv_label;
        lv_obj_t *batter_current_ma_btn;
        lv_obj_t *batter_current_ma_label;
        lv_obj_t *batter_status_label[7];
    } charge_ui_objects_t;

    typedef struct
    {
        lv_obj_t *weather_icon;
        lv_obj_t *last_update_label;
        lv_obj_t *weather_city_label;
        lv_obj_t *weather_temp_label;
    } weather_ui_objects_t;

    typedef struct
    {
        lv_obj_t *audio_recorded_btn;
        lv_obj_t *audio_play_btn;
        lv_obj_t *audio_db_bar;
        lv_obj_t *audio_db_label;
        lv_obj_t *audio_record_time_label;
        lv_obj_t *audio_play_time_label;
        lv_obj_t *audio_vol_label;
    } audio_ui_objects_t;

    typedef struct
    {
        lv_obj_t *file_list;
        lv_obj_t *file_item;
    } file_ui_objects_t;

    typedef struct
    {
        lv_obj_t *key[20];
    } keyboard_ui_objects_t;

    typedef struct
    {
        lv_obj_t *dropdown_time_label;
        lv_obj_t *dropdown_battery_icon_bar;
        lv_obj_t *dropdown_battery_label;
        lv_obj_t *dropdown_light_slider;
        lv_obj_t *dropdown_mute_btn;
        lv_obj_t *dropdown_bluetooth_btn;
        lv_obj_t *dropdown_rf_btn;
    } dropdown_ui_objects_t;

    typedef struct
    {
        lv_obj_t *bt_btn;
        lv_obj_t *wifi_btn;
        lv_obj_t *key_led_btn;
        lv_obj_t *brightness_slider;
        lv_obj_t *volume_slider;
        lv_obj_t *shotdown_btn;
        lv_obj_t *restart_btn;
        lv_obj_t *ship_mode_btn;
        lv_obj_t *mbox;
    } system_ui_objects_t;

    typedef struct
    {
        lv_obj_t *vol_popup;
        lv_timer_t *vol_timer;
    } overall_ui_objects_t;

    typedef struct device_switch
    {
        bool wifi_enable;
        bool bluetooth_enable;
        bool bluetooth_connected;
        bool bluetooth_pan_enable;
        bool gps_enable;
        bool keyboard_led_enable;
        bool lcd_backlight_enable;
        bool lora_rx_enable;
        bool lora_tx_enable;
        bool ir_enable;
    } device_switch_t;

    extern rt_sem_t start_ui_sem;

    void music_event_artist_info_update(void *data);
    void music_event_name_info_update(void *data);
    void music_progress_update(void *data);
    void music_current_time_update(uint32_t current_ms, uint32_t duration_ms);
    void music_pause_btn_update(void *data);
    void wifi_update_resp_cb(void *data);
    void wifi_update_wifi_status(void *data);
    void volume_progress_bar(void *data);

    rt_mailbox_t get_lvgl_mb(void);
    device_switch_t *get_device_switch(void);

#ifdef __cplusplus__
}
#endif

#endif /* __UI_H__ */