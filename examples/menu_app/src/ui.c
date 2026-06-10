#include "ui.h"
#include "log.h"
#include "lv_timer.h"
#include "bts2_app_interface.h"
#include "rtc_time.h"

#include <stdlib.h>
#include <time.h>
#include <math.h>
#include "ff.h"
#include "rtthread.h"
#include "config.h"

#if defined(PKG_USING_BHI260AP)
#include "bhi260ap.h"
#elif defined(PKG_USING_ICM20948)
#include "icm20948_driver.h"
#endif

#ifdef PKG_USING_SGM41562B
#include "sgm41562b.h"
#endif

static void lvgl_task_entry(void *parameter);
static void get_time_async(void *parameter);

static void menu_app_ui_init(void);
static void switch_to_page(Page *page);

static void start_page_on_create(struct Page *page);
static void status_bar_init(lv_obj_t *scr);
static void app_icon_ui_init(struct Page *page);
static void home_page_on_create(struct Page *page);
static void music_page_on_create(struct Page *page);
static void lora_page_on_create(struct Page *page);
static void sensor_page_on_create(struct Page *page);
static void gps_page_on_create(struct Page *page);
static void wifi_page_on_create(struct Page *page);
static void charge_page_on_create(struct Page *page);
static void weather_page_on_create(struct Page *page);
static void aduio_page_on_create(struct Page *page);
static void file_page_on_create(struct Page *page);
static void keyboard_page_on_create(struct Page *page);
static void system_page_on_create(struct Page *page);

static void page_on_enter(Page *page);
static void page_on_leave(Page *page);
static void page_on_destroy(Page *page);
static lv_obj_t *create_info_card(lv_obj_t *parent, const char *title, const char *value, lv_coord_t x, lv_coord_t y);

/*********event***********/
static void gesture_event_cb(lv_event_t *e);
static void scroll_event_cb(lv_event_t *e);
static void app_icon_event_cb(lv_event_t *e);
static void music_event_cb(lv_event_t *e);
static void lora_event_cb(lv_event_t *e);
static void sensor_event_cb(lv_event_t *e);
static void wifi_event_cb(lv_event_t *e);
static void charge_event_cb(lv_event_t *e);
static void weather_event_cb(lv_event_t *e);
static void aduio_event_cb(lv_event_t *e);
static void close_file_event_cb(lv_event_t *e);
static void file_event_cb(lv_event_t *e);
static void keyboard_event_cb(lv_event_t *e);
static void system_event_cb(lv_event_t *e);
static void ship_mode_confirm_dialog(lv_event_t *e);

/*********anim***********/
static void anim_size_width_cb(void *var, int32_t v);
static void anim_size_height_cb(void *var, int32_t v);
static void anim_size_cb(void *var, int32_t v);
static void anim_x_cb(void *var, int32_t v);
static void anim_y_cb(void *var, int32_t v);
static void delete_obj_timer_cb(lv_timer_t *timer);
static void anim_ready_cb(lv_anim_t *a);
static void anim_zoom(lv_obj_t *obj, uint32_t duration, uint32_t start_width, uint32_t end_width, uint32_t start_height, uint32_t end_height);
static void anim_height(lv_obj_t *obj, uint32_t duration, uint32_t start_height, uint32_t end_height);
static void anim_y(lv_obj_t *obj, uint32_t duration, uint32_t start_y, uint32_t end_y);
static void anim_x(lv_obj_t *obj, uint32_t duration, uint32_t start_x, uint32_t end_x);

/*********timer************/
static void timer_manager_callback(lv_timer_t *timer);
static void home_timer_callback(lv_timer_t *timer);
static void music_timer_callback(lv_timer_t *timer);
static void lora_send_thread_entry(void *parameter);
static void sensor_timer_callback(lv_timer_t *timer);
static void gps_timer_callback(lv_timer_t *timer);
static void charge_timer_callback(lv_timer_t *timer);
static void weather_timer_callback(lv_timer_t *timer);
static void audio_timer_callback(lv_timer_t *timer);
static void file_timer_callback(lv_timer_t *timer);
static void file_scan_thread_entry(void *parameter);

/*********gropu_cb************/
static void home_focus_cb(lv_group_t *group);

/*********other***********/
int32_t get_voltage(void);
uint8_t get_charge_percent(void);
static void format_time_str(char *buf, int ms);
static void radio_rx_callback(lora_rx_info_t *info);
static uint32_t keycode_to_lvgl(uint8_t keycode);

/**********object***********/
static const app_info app_list[APP_SIZE] = {
    {&music_icon, "MUSIC", {"Use:", "Function:"}, {"Connect BT", "Bluetooth player"}, 2, APP_MUSIC},
    {&lora_icon, "LORA", {"Chip:", "Frq:", "Power:", "Protocol"}, {"SX1262", "868~915Mhz", "+22dbm", "SPI"}, 4, APP_LORA},
    {&imu_icon, "Sensor", {"Chip:", "Dof:", "Device", "Protocol"}, {"BHI260AP/BME280", "6-Dof/Hygrothermograph", "Motor/Infrared", "I2C"}, 4, APP_SENSOR},
    {&gps_icon, "GPS", {"Chip:", "Protocol"}, {"L76K", "Uart"}, 2, APP_GPS},
    {&wifi_icon, "WIFI", {"Chip:", "Protocol"}, {"ESP32C6", "Uart"}, 2, APP_WIFI},
    {&battery_icon, "CHARGE", {"Chip:", "Uvlo:", "Ibat:", "Protocol:"}, {"Sgm41562b", "3.0V", "200mA/400mA", "I2C"}, 4, APP_CHARGE},
    {&weather_icon, "WEATHER", {"Use:", "City:", "Country:", "Timezone:"}, {"Connect BT and Open Pan", "ShenZhen", "China", "+8"}, 4, APP_WEATHER},
    {&record_icon, "AUDIO", {"Function:"}, {"Record of audio and play it."}, 1, APP_AUDIO},
    {&file_icon, "FILE", {"Function:", "Protocol:"}, {"SD card file directory", "SPI"}, 2, APP_FILE},
    {&keyboard_icon, "KEYBOARD", {"Function:", "Protocol:"}, {"Test keyboard", "I2C"}, 2, APP_KEYBOARD},
    {&info_icon, "SYSTEM", {"MCU:", "BLUETOOTH:", "LVGL:", "RTOS:", "Version:", "Flash:"}, {"SF32LB52X", "BT/BLE 5.3", "9.2.0", "RT-Thread", "V1.0.0", "16MB"}, 5, APP_SYSTEM},
};

Page page_list[PAGE_SIZE] = {
    [PAGE_START] = {
        .id = PAGE_START,
        .app_id = -1,
        .name = "Start Page",
        .page_timer = NULL,
        .timer_interval = 0,
        .on_create = start_page_on_create,
        .on_enter = page_on_enter,
        .on_leave = page_on_leave,
        .on_destroy = page_on_destroy,
        .is_created = false,
    },
    [PAGE_HOME] = {
        .id = PAGE_HOME,
        .app_id = -1,
        .name = "Home Page",
        .group_cb = home_focus_cb,
        .page_timer = home_timer_callback,
        .timer_interval = 200,
        .on_create = home_page_on_create,
        .on_enter = page_on_enter,
        .on_leave = page_on_leave,
        .on_destroy = page_on_destroy,
        .is_created = false,
    },
    [PAGE_MUSIC] = {
        .id = PAGE_MUSIC,
        .app_id = APP_MUSIC,
        .name = "Music Page",
        .page_timer = music_timer_callback,
        .timer_interval = 1000,
        .on_create = music_page_on_create,
        .on_enter = page_on_enter,
        .on_leave = page_on_leave,
        .on_destroy = page_on_destroy,
        .is_created = false,
    },
    [PAGE_LORA] = {
        .id = PAGE_LORA,
        .app_id = APP_LORA,
        .name = "lora Page",
        .page_timer = NULL,
        .timer_interval = 0,
        .on_create = lora_page_on_create,
        .on_enter = page_on_enter,
        .on_leave = page_on_leave,
        .on_destroy = page_on_destroy,
        .is_created = false,
    },
    [PAGE_SENSOR] = {
        .id = PAGE_SENSOR,
        .app_id = APP_SENSOR,
        .name = "sensor Page",
        .page_timer = sensor_timer_callback,
        .timer_interval = 100,
        .on_create = sensor_page_on_create,
        .on_enter = page_on_enter,
        .on_leave = page_on_leave,
        .on_destroy = page_on_destroy,
        .is_created = false,
    },
    [PAGE_GPS] = {
        .id = PAGE_GPS,
        .app_id = APP_GPS,
        .name = "gps Page",
        .page_timer = gps_timer_callback,
        .timer_interval = 1000,
        .on_create = gps_page_on_create,
        .on_enter = page_on_enter,
        .on_leave = page_on_leave,
        .on_destroy = page_on_destroy,
        .is_created = false,
    },
    [PAGE_WIFI] = {
        .id = PAGE_WIFI,
        .app_id = APP_WIFI,
        .name = "wifi Page",
        .page_timer = NULL,
        .timer_interval = 0,
        .on_create = wifi_page_on_create,
        .on_enter = page_on_enter,
        .on_leave = page_on_leave,
        .on_destroy = page_on_destroy,
        .is_created = false,
    },
    [PAGE_CHARGE] = {
        .id = PAGE_CHARGE,
        .app_id = APP_CHARGE,
        .name = "charge Page",
        .page_timer = charge_timer_callback,
        .timer_interval = 2000,
        .on_create = charge_page_on_create,
        .on_enter = page_on_enter,
        .on_leave = page_on_leave,
        .on_destroy = page_on_destroy,
        .is_created = false,
    },
    [PAGE_WEATHER] = {
        .id = PAGE_WEATHER,
        .app_id = APP_WEATHER,
        .name = "weather Page",
        .page_timer = weather_timer_callback,
        .timer_interval = 60000,
        .on_create = weather_page_on_create,
        .on_enter = page_on_enter,
        .on_leave = page_on_leave,
        .on_destroy = page_on_destroy,
        .is_created = false,
    },
    [PAGE_AUDIO] = {
        .id = PAGE_AUDIO,
        .app_id = APP_AUDIO,
        .name = "aduio Page",
        .page_timer = audio_timer_callback,
        .timer_interval = 10,
        .on_create = aduio_page_on_create,
        .on_enter = page_on_enter,
        .on_leave = page_on_leave,
        .on_destroy = page_on_destroy,
        .is_created = false,
    },
    [PAGE_FILE] = {
        .id = PAGE_FILE,
        .app_id = APP_FILE,
        .name = "file Page",
        .page_timer = file_timer_callback,
        .timer_interval = 200,
        .on_create = file_page_on_create,
        .on_enter = page_on_enter,
        .on_leave = page_on_leave,
        .on_destroy = page_on_destroy,
        .is_created = false,
    },
    [PAGE_KEYBOARD] = {
        .id = PAGE_KEYBOARD,
        .app_id = APP_KEYBOARD,
        .name = "keyboard Page",
        .page_timer = RT_NULL,
        .timer_interval = 0,
        .on_create = keyboard_page_on_create,
        .on_enter = page_on_enter,
        .on_leave = page_on_leave,
        .on_destroy = page_on_destroy,
        .is_created = false,
    },
    [PAGE_SYSTEM] = {
        .id = PAGE_SYSTEM,
        .app_id = APP_SYSTEM,
        .name = "system Page",
        .page_timer = NULL,
        .timer_interval = 0,
        .on_create = system_page_on_create,
        .on_enter = page_on_enter,
        .on_leave = page_on_leave,
        .on_destroy = page_on_destroy,
        .is_created = false,
    },
};

Page *current_page = NULL;
Page *last_page = NULL;
static rt_thread_t lvgl_task = RT_NULL;
static rt_thread_t lvgl_key_board_task = RT_NULL;
static rt_thread_t time_thread = RT_NULL;
static bool time_thread_created = false;

static lv_style_t screen_style;
static home_ui_objects_t home_ui;
static music_ui_objects_t music_ui;
static lora_ui_objects_t lora_ui;
static sensor_ui_objects_t sensor_ui;
static gps_ui_objects_t gps_ui;
static wifi_ui_objects_t wifi_ui;
static charge_ui_objects_t charge_ui;
static weather_ui_objects_t weather_ui;
static audio_ui_objects_t audio_ui;
static file_ui_objects_t file_ui;
static keyboard_ui_objects_t keyboard_ui;
static dropdown_ui_objects_t dropdown_ui;
static system_ui_objects_t system_ui;
static overall_ui_objects_t overall_ui;
lv_obj_t *last_group_obj;

int32_t progress_bar_y = 0;
ui_event_type_t ui_event_t;
device_switch_t device_switch = {0};

lora_rx_info_t rx_info;
static uint16_t lora_tx_count = 0;
static char lora_tx_display_buf[256] = {0};      /* TX string for async UI update */
static lora_rx_info_t lora_rx_display_buf = {0}; /* RX info for async UI update */
int32_t voltage = 0;
int32_t voltage_percent = 0;

static char current_dir[256] = "/"; // 当前目录路径

/* 异步文件扫描控制 */
static rt_thread_t file_scan_thread = RT_NULL;
static volatile bool file_scan_done = false;

#define UI_QUEUE_SIZE 10
rt_mailbox_t lvgl_mb = RT_NULL;
static rt_mutex_t lvgl_mutex = RT_NULL;
static rt_mutex_t page_mutex = RT_NULL;
rt_sem_t start_ui_sem = RT_NULL;

#ifdef PKG_USING_PKG_KEY_BOARD
static lv_indev_t *kb_indev;
static lv_indev_data_t kb_data = {0};
static void keyboard_read_cb(lv_indev_t *indev, lv_indev_data_t *data);
rt_mq_t lvgl_key_mq;
static key_board_event_msg_t key_msg = {0};
#endif

rt_err_t ui_init(void)
{
    rt_err_t ret = RT_EOK;

    ret = littlevgl2rtt_init("lcd");
    if (ret != RT_EOK)
    {
        return ret;
    }

#ifdef PKG_USING_PKG_KEY_BOARD
    kb_indev = lv_indev_create();
    lv_indev_set_type(kb_indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_mode(kb_indev, LV_INDEV_MODE_EVENT);
#endif

    lvgl_mb = rt_mb_create("ui_mb", UI_QUEUE_SIZE, RT_IPC_FLAG_FIFO); // 创建一个消息队列
    if (lvgl_mb == RT_NULL)
    {
        return -RT_ERROR;
    }

    lvgl_mutex = rt_mutex_create("lvgl_mutex", RT_IPC_FLAG_FIFO);
    if (lvgl_mutex == RT_NULL)
    {
        log_e("Failed to create scroll mutex");
        return -RT_ERROR;
    }

    page_mutex = rt_mutex_create("page_mutex", RT_IPC_FLAG_FIFO);
    if (page_mutex == RT_NULL)
    {
        log_e("Failed to create scroll mutex");
        return -RT_ERROR;
    }

    start_ui_sem = rt_sem_create("start_ui_sem", 0, RT_IPC_FLAG_FIFO);
    if (start_ui_sem == RT_NULL)
    {
        log_e("Failed to create start_ui_sem");
        return -RT_ERROR;
    }

#ifdef PKG_USING_PKG_KEY_BOARD
    lv_indev_set_read_cb(kb_indev, keyboard_read_cb);
#endif

    lvgl_task = rt_thread_create("lvgl_task", lvgl_task_entry, RT_NULL, 8 * 1024, RT_THREAD_PRIORITY_HIGH, 10);
    if (lvgl_task != RT_NULL)
    {
        rt_thread_startup(lvgl_task);
    }
    menu_app_ui_init();
}

#ifdef PKG_USING_PKG_KEY_BOARD
static void keyboard_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    *data = kb_data;
}
#endif

static void lvgl_task_entry(void *parameter)
{
    rt_uint32_t ms;
    rt_uint32_t time;

    while (1)
    {
#ifdef PKG_USING_PKG_KEY_BOARD
        rt_device_t lcd_device;
        lvgl_key_mq = key_board_get_mq();
        if (lvgl_key_mq)
        {
            if (rt_mq_recv(lvgl_key_mq, &key_msg, sizeof(key_board_event_msg_t), RT_WAITING_NO) == RT_EOK)
            {
                if (current_page->id != PAGE_KEYBOARD)
                {
                    if (!key_msg.is_long_press)
                    {
                        log_d("key_msg.code:%d", key_msg.code);
                        switch (key_msg.code)
                        {
                        case KEY_PREV:
                        case KEY_NEXT:
                        case KEY_1:
                        case KEY_3:
                        case KEY_ENTER: // enter app
                        {
                            kb_data.key = keycode_to_lvgl(key_msg.code);
                            kb_data.state = LV_INDEV_STATE_PR;
                            lv_indev_read(kb_indev);
                            kb_data.state = LV_INDEV_STATE_REL;
                            lv_indev_read(kb_indev);
                            break;
                        }
                        case KEY_HOME: // back
                            switch_to_page(&page_list[PAGE_HOME]);
                            break;
                        case KEY_COLSE:
                            audio_volume_up();
                            lv_async_call(volume_progress_bar, (void *)audio_get_volume());
                            break;
                        case KEY_STOP:
                            audio_volume_down();
                            lv_async_call(volume_progress_bar, (void *)audio_get_volume());
                            break;
                        }
                    }
                }
                else
                {
                    if (!key_msg.is_long_press)
                    {
                        log_d("key_msg.code = %d", key_msg.code);
                        if (key_msg.code == KEY_HOME)
                        {
                            switch_to_page(&page_list[PAGE_HOME]);
                        }
                        for (int i = 0; i < 20; i++)
                        {
                            int index = (int)lv_obj_get_user_data(keyboard_ui.key[i]);
                            if (key_msg.code == index)
                            {
                                // log_d("index = %d",index);
                                lv_color_t get_color = lv_obj_get_style_bg_color(keyboard_ui.key[i], LV_PART_MAIN | LV_STATE_DEFAULT);
                                if (lv_color_eq(get_color, lv_color_hex(LV_COLOR_WARM_RED)))
                                {
                                    lv_obj_set_style_bg_color(keyboard_ui.key[i], lv_color_hex(0x0077ff), LV_PART_MAIN | LV_STATE_DEFAULT);
                                }
                                else
                                {
                                    lv_obj_set_style_bg_color(keyboard_ui.key[i], lv_color_hex(LV_COLOR_WARM_RED), LV_PART_MAIN | LV_STATE_DEFAULT);
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }
#endif
        rt_mutex_take(lvgl_mutex, RT_WAITING_FOREVER);
        ms = lv_task_handler();
        rt_mutex_release(lvgl_mutex);
        rt_thread_mdelay(ms);
    }
}

#ifdef PKG_USING_PKG_KEY_BOARD
static uint32_t keycode_to_lvgl(uint8_t keycode)
{
    static const uint32_t map[] = {
        [KEY_1] = LV_KEY_DOWN,
        [KEY_3] = LV_KEY_UP,
        [KEY_PREV] = LV_KEY_PREV,
        [KEY_NEXT] = LV_KEY_NEXT,
        [KEY_ENTER] = LV_KEY_ENTER,
    };
    return (keycode < sizeof(map) / sizeof(map[0])) ? map[keycode] : 0;
}
#endif

static void get_time_async(void *parameter)
{
    static date_time_t current_dt;
    while (1)
    {
        log_d("get_time_async\n");
        if (get_time(&current_dt) == RT_EOK)
        {
            get_date_time(&current_dt);
        }
        rt_thread_mdelay(10000);
    }
}

/**********page init***********/
static void menu_app_ui_init(void)
{
    lv_style_init(&screen_style);
    lv_style_set_size(&screen_style, 480, 480);
    lv_style_set_border_width(&screen_style, 0);
    lv_style_set_pad_all(&screen_style, 0);
    lv_style_set_opa(&screen_style, LV_OPA_COVER);
    lv_style_set_bg_color(&screen_style, lv_color_hex(LV_COLOR_THEME_BLACK));
    lv_style_set_text_color(&screen_style, lv_color_hex(LV_COLOR_THEME_WIHTE));

    current_page = &page_list[PAGE_START];
    last_page = &page_list[PAGE_START];

    lv_timer_t *timer = lv_timer_create(timer_manager_callback, 10, NULL);
    page_on_enter(&page_list[PAGE_START]);
}

static void timer_manager_callback(lv_timer_t *timer)
{
    static uint32_t last_tick = 0;
    uint32_t current_tick = rt_tick_get();
    uint32_t elapsed_ms = (current_tick - last_tick) * (1000 / RT_TICK_PER_SECOND);

    if (elapsed_ms < 10)
        return; // 最小间隔10ms

    rt_mutex_take(page_mutex, RT_WAITING_FOREVER);
    Page *page = current_page;

    if (current_page == NULL)
    {
        rt_mutex_release(page_mutex);
        last_tick = current_tick;
        return;
    }

    if (page->page_timer && page->timer_interval > 0 && page->is_created)
    {
        page->timer_counter += elapsed_ms;
        if (page->timer_counter >= page->timer_interval)
        {
            page->timer_counter = 0;
            if (current_page && current_page->id == page->id)
            {
                page->page_timer(timer);
            }
        }
    }
    last_tick = current_tick;
    rt_mutex_release(page_mutex);
}

static void vol_popup_del_cb(lv_timer_t *t)
{
    if (overall_ui.vol_popup)
    {
        lv_obj_del(overall_ui.vol_popup);
        overall_ui.vol_popup = NULL;
    }
    overall_ui.vol_timer = NULL;
}

void volume_progress_bar(void *data)
{
    int volume;
    if (data != NULL)
        volume = (int)(uintptr_t)data;
    else
        volume = audio_get_volume();
    if (volume < 0)
        volume = 0;
    if (volume > 15)
        volume = 15;

    if (overall_ui.vol_popup)
    {
        lv_obj_t *bar = (lv_obj_t *)lv_obj_get_user_data(overall_ui.vol_popup);
        lv_obj_t *label = lv_obj_get_child(overall_ui.vol_popup, -1);
        if (bar)
            lv_bar_set_value(bar, volume, LV_ANIM_ON);
        if (label)
            lv_label_set_text_fmt(label, "%d", volume);
        if (overall_ui.vol_timer)
        {
            lv_timer_reset(overall_ui.vol_timer);
            lv_timer_set_repeat_count(overall_ui.vol_timer, 1);
        }
        return;
    }

    lv_obj_t *scr = lv_scr_act();

    overall_ui.vol_popup = lv_obj_create(scr);
    lv_obj_set_size(overall_ui.vol_popup, 320, 70);
    lv_obj_align(overall_ui.vol_popup, LV_ALIGN_TOP_MID, 0, 70);
    lv_obj_set_style_bg_opa(overall_ui.vol_popup, LV_OPA_20, 0);
    lv_obj_set_style_bg_color(overall_ui.vol_popup, lv_color_hex(LV_COLOR_THEME_BLACK), 0);
    lv_obj_set_style_border_width(overall_ui.vol_popup, 0, 0);
    lv_obj_set_style_radius(overall_ui.vol_popup, 16, 0);
    lv_obj_set_style_shadow_width(overall_ui.vol_popup, 10, 0);
    lv_obj_set_style_pad_all(overall_ui.vol_popup, 0, 0);
    lv_obj_clear_flag(overall_ui.vol_popup, LV_OBJ_FLAG_SCROLLABLE);

    /* 铃铛图标 */
    lv_obj_t *icon = lv_label_create(overall_ui.vol_popup);
    lv_label_set_text(icon, LV_SYMBOL_BELL);
    lv_obj_set_style_text_color(icon, lv_color_hex(LV_COLOR_THEME_WIHTE), 0);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_24, 0);
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 16, 0);

    /* 音量进度条 */
    lv_obj_t *bar = lv_bar_create(overall_ui.vol_popup);
    lv_obj_set_size(bar, 180, 8);
    lv_obj_align(bar, LV_ALIGN_CENTER, 16, 0);
    lv_bar_set_range(bar, 0, 15);
    lv_bar_set_value(bar, volume, LV_ANIM_ON);

    lv_obj_set_style_radius(bar, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bar, LV_OPA_40, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bar, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_shadow_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bar, lv_color_hex(LV_COLOR_THEME_GREEN), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bar, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    /* 存 bar 指针便于快速更新 */
    lv_obj_set_user_data(overall_ui.vol_popup, bar);

    /* 音量数值（永远是最后一个child） */
    lv_obj_t *label = lv_label_create(overall_ui.vol_popup);
    lv_label_set_text_fmt(label, "%d", volume);
    lv_obj_set_style_text_color(label, lv_color_hex(LV_COLOR_THEME_WIHTE), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_align(label, LV_ALIGN_RIGHT_MID, -20, 0);

    /* 1.8 秒后自动消失 */
    overall_ui.vol_timer = lv_timer_create(vol_popup_del_cb, 1800, NULL);
    lv_timer_set_repeat_count(overall_ui.vol_timer, 1);
}

/**********start page **********/
static void start_page_on_create(struct Page *page)
{
    log_d("start_page_on_create\n");

    lv_obj_t *start_progress_bar = lv_obj_create(page->root);
    lv_obj_set_size(start_progress_bar, 10, 10);
    lv_obj_align(start_progress_bar, LV_ALIGN_CENTER, 0, 60);
    lv_obj_set_scrollbar_mode(start_progress_bar, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_border_width(start_progress_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(start_progress_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(start_progress_bar, lv_color_hex(LV_COLOR_THEME_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(start_progress_bar, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(start_progress_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *lilygo_label = lv_label_create(page->root);
    lv_obj_set_pos(lilygo_label, 90, 300);
    lv_obj_set_size(lilygo_label, 300, 10);
    lv_label_set_text(lilygo_label, "LILYGO");
    lv_label_set_long_mode(lilygo_label, LV_LABEL_LONG_WRAP);

    lv_obj_set_style_border_width(lilygo_label, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(lilygo_label, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lilygo_label, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lilygo_label, &lv_font_montserrat_48, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(lilygo_label, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(lilygo_label, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_line_space(lilygo_label, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(lilygo_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(lilygo_label, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(lilygo_label, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(lilygo_label, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, start_progress_bar);
    lv_anim_set_values(&a, 0, 240);
    lv_anim_set_duration(&a, 2500);
    lv_anim_set_delay(&a, 200);
    lv_anim_set_exec_cb(&a, anim_size_width_cb);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_start(&a);

    lv_anim_t move_up;
    lv_anim_init(&move_up);
    lv_anim_set_var(&move_up, lilygo_label);
    lv_anim_set_values(&move_up, 280, 200);
    lv_anim_set_delay(&move_up, 200);
    lv_anim_set_duration(&move_up, 2500);
    lv_anim_set_exec_cb(&move_up, anim_y_cb);
    lv_anim_set_path_cb(&move_up, lv_anim_path_overshoot);
    lv_anim_start(&move_up);

    lv_anim_t size_anim;
    lv_anim_init(&size_anim);
    lv_anim_set_var(&size_anim, lilygo_label);
    lv_anim_set_values(&size_anim, 0, 100);
    lv_anim_set_delay(&size_anim, 200);
    lv_anim_set_duration(&size_anim, 2500);
    lv_anim_set_exec_cb(&size_anim, anim_size_height_cb);
    lv_anim_set_path_cb(&size_anim, lv_anim_path_ease_out);
    lv_anim_set_ready_cb(&size_anim, anim_ready_cb);
    lv_anim_start(&size_anim);
}

static void anim_ready_cb(lv_anim_t *a)
{
    log_d("anim_ready_cb");
#ifdef PKG_USING_PKG_KEY_BOARD
    aw86224_play_ram_short(1, RT_TRUE);
#endif
    rt_thread_mdelay(500);
    rt_sem_release(start_ui_sem);
    switch_to_page(&page_list[PAGE_HOME]);

    lv_timer_t *timer = lv_timer_create(delete_obj_timer_cb, 2000, (void *)page_list[PAGE_START].root);
    lv_timer_set_repeat_count(timer, 1);
}

/**********home page **********/
static void status_bar_init(lv_obj_t *scr)
{
    lv_obj_t *status_bar = lv_obj_create(scr);
    lv_obj_set_size(status_bar, LV_HOR_RES, 40);
    lv_obj_align(status_bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_scrollbar_mode(status_bar, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(status_bar, lv_color_hex(LV_COLOR_THEME_BLACK), 0);
    lv_obj_set_style_border_width(status_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(status_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(status_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(status_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(status_bar, LV_OPA_0, LV_PART_MAIN | LV_STATE_DEFAULT);

    home_ui.status_bar_bluetooth_icon = lv_label_create(status_bar);
    lv_obj_align(home_ui.status_bar_bluetooth_icon, LV_ALIGN_LEFT_MID, 40, 0);
    lv_label_set_text(home_ui.status_bar_bluetooth_icon, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_font(home_ui.status_bar_bluetooth_icon, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(home_ui.status_bar_bluetooth_icon, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(home_ui.status_bar_bluetooth_icon, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    home_ui.status_bar_wifi_icon = lv_label_create(status_bar);
    lv_obj_align(home_ui.status_bar_wifi_icon, LV_ALIGN_LEFT_MID, 80, 0);
    lv_label_set_text(home_ui.status_bar_wifi_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_font(home_ui.status_bar_wifi_icon, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(home_ui.status_bar_wifi_icon, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(home_ui.status_bar_wifi_icon, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    home_ui.status_bar_gps_icon = lv_label_create(status_bar);
    lv_obj_align(home_ui.status_bar_gps_icon, LV_ALIGN_LEFT_MID, 125, 0);
    lv_label_set_text(home_ui.status_bar_gps_icon, LV_SYMBOL_GPS);
    lv_obj_set_style_text_font(home_ui.status_bar_gps_icon, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(home_ui.status_bar_gps_icon, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(home_ui.status_bar_gps_icon, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    date_time_t current_dt;
    get_date_time(&current_dt);
    home_ui.status_bar_time_label = lv_label_create(status_bar);
    lv_obj_align(home_ui.status_bar_time_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text_fmt(home_ui.status_bar_time_label, "%02d:%02d", current_dt.hour, current_dt.minute);
    lv_obj_set_style_text_font(home_ui.status_bar_time_label, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(home_ui.status_bar_time_label, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(home_ui.status_bar_time_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    uint8_t battery_percent = get_charge_percent();
    home_ui.status_bar_battery_label = lv_label_create(status_bar);
    lv_obj_align(home_ui.status_bar_battery_label, LV_ALIGN_RIGHT_MID, -30, 0);
    lv_label_set_text_fmt(home_ui.status_bar_battery_label, "%d%%", battery_percent);
    lv_obj_set_style_text_font(home_ui.status_bar_battery_label, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(home_ui.status_bar_battery_label, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(home_ui.status_bar_battery_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);

    home_ui.status_bar_battery_icon = lv_label_create(status_bar);
    lv_obj_align(home_ui.status_bar_battery_icon, LV_ALIGN_RIGHT_MID, -100, 0);
    lv_label_set_text(home_ui.status_bar_battery_icon, LV_SYMBOL_BATTERY_FULL);
    lv_obj_set_style_text_font(home_ui.status_bar_battery_icon, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(home_ui.status_bar_battery_icon, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(home_ui.status_bar_battery_icon, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_anim_t move_down;
    lv_anim_init(&move_down);
    lv_anim_set_var(&move_down, status_bar);
    lv_anim_set_values(&move_down, 0, 40);
    lv_anim_set_duration(&move_down, 400);
    lv_anim_set_delay(&move_down, 200);
    lv_anim_set_exec_cb(&move_down, anim_size_height_cb);
    lv_anim_set_path_cb(&move_down, lv_anim_path_ease_in_out);
    lv_anim_start(&move_down);
}

static void app_icon_ui_init(struct Page *page)
{
    lv_obj_t *app_scroll_cont = lv_obj_create(page->root);
    lv_obj_set_scrollbar_mode(app_scroll_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(app_scroll_cont, 480, 440);
    lv_obj_align(app_scroll_cont, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_bg_opa(app_scroll_cont, LV_OPA_0, 0);
    lv_obj_set_style_bg_color(app_scroll_cont, lv_color_hex(LV_COLOR_THEME_BLACK), 0);
    lv_obj_set_style_border_width(app_scroll_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(app_scroll_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(app_scroll_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(app_scroll_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_scroll_dir(app_scroll_cont, LV_DIR_VER);
    lv_obj_add_flag(app_scroll_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_snap_y(app_scroll_cont, LV_SCROLL_SNAP_CENTER);
    lv_obj_add_event_cb(app_scroll_cont, scroll_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_set_flex_flow(app_scroll_cont, LV_FLEX_FLOW_COLUMN);

    for (int i = 0; i < APP_SIZE; i++)
    {
        lv_obj_t *icon_cont = lv_obj_create(app_scroll_cont);
        lv_obj_set_scrollbar_mode(icon_cont, LV_SCROLLBAR_MODE_OFF);
        lv_obj_set_size(icon_cont, 480, 240);
        lv_obj_align(icon_cont, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_bg_opa(icon_cont, LV_OPA_0, 0);
        lv_obj_set_style_bg_color(icon_cont, lv_color_hex(LV_COLOR_THEME_BLACK), 0);
        lv_obj_set_style_border_width(icon_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(icon_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(icon_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_width(icon_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_add_flag(icon_cont, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        lv_obj_set_user_data(icon_cont, (void *)(intptr_t)app_list[i].id);
        lv_obj_add_event_cb(icon_cont, app_icon_event_cb, LV_EVENT_CLICKED, (void *)NULL);
        lv_group_focus_obj(icon_cont);
        lv_group_add_obj(page->group, icon_cont);

        lv_obj_t *icon = lv_img_create(icon_cont);
        lv_img_set_src(icon, app_list[i].icon_dsc);
        lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 40, 60);

        lv_obj_t *icon_label = lv_label_create(icon_cont);
        lv_obj_set_size(icon_label, 130, 50);
        lv_label_set_text(icon_label, app_list[i].name);
        lv_obj_align_to(icon_label, icon, LV_ALIGN_BOTTOM_MID, 0, 80);
        lv_obj_set_style_text_color(icon_label, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(icon_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(icon_label, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_obj_t *text_title_cont = lv_obj_create(icon_cont);
        lv_obj_set_scrollbar_mode(text_title_cont, LV_SCROLLBAR_MODE_OFF);
        lv_obj_clear_flag(text_title_cont, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(text_title_cont, 125, 240);
        lv_obj_align(text_title_cont, LV_ALIGN_TOP_LEFT, 180, 0);
        lv_obj_set_style_bg_opa(text_title_cont, LV_OPA_0, 0);
        lv_obj_set_style_border_width(text_title_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(text_title_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_width(text_title_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_flex_flow(text_title_cont, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(text_title_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        lv_obj_t *text_data_cont = lv_obj_create(icon_cont);
        lv_obj_set_scrollbar_mode(text_data_cont, LV_SCROLLBAR_MODE_OFF);
        lv_obj_clear_flag(text_data_cont, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(text_data_cont, 140, 240);
        lv_obj_align(text_data_cont, LV_ALIGN_TOP_LEFT, 320, 0);
        lv_obj_set_style_bg_opa(text_data_cont, LV_OPA_0, 0);
        lv_obj_set_style_border_width(text_data_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(text_data_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_width(text_data_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_flex_flow(text_data_cont, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(text_data_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        for (int j = 0; j < app_list[i].text_count; j++)
        {
            lv_obj_t *app_title_label = lv_label_create(text_title_cont);
            lv_obj_set_size(app_title_label, 120, 30);
            // lv_obj_align(app_title_label, LV_ALIGN_TOP_LEFT, 0, 10);
            lv_label_set_text(app_title_label, app_list[i].text_title[j]);
            lv_obj_set_style_text_color(app_title_label, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_align(app_title_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(app_title_label, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_long_mode(app_title_label, LV_LABEL_LONG_SCROLL_CIRCULAR);

            lv_obj_t *app_data_label = lv_label_create(text_data_cont);
            lv_obj_set_size(app_data_label, 125, 30);
            // lv_obj_align(app_data_label, LV_ALIGN_TOP_LEFT, 0, 10);
            lv_label_set_text(app_data_label, app_list[i].text_data[j]);
            lv_obj_set_style_text_color(app_data_label, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_align(app_data_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(app_data_label, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_label_set_long_mode(app_data_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
        }
    }

    lv_obj_t *cross_line = lv_obj_create(page->root);
    lv_obj_set_size(cross_line, 8, 220);
    lv_obj_align(cross_line, LV_ALIGN_TOP_LEFT, 160, 140);
    lv_obj_set_scrollbar_mode(cross_line, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_border_width(cross_line, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(cross_line, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(cross_line, lv_color_hex(LV_COLOR_THEME_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(cross_line, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(cross_line, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    home_ui.progress_bar = lv_obj_create(page->root);
    lv_obj_set_size(home_ui.progress_bar, 12, 0);
    lv_obj_align(home_ui.progress_bar, LV_ALIGN_TOP_LEFT, 158, 140);
    lv_obj_set_scrollbar_mode(home_ui.progress_bar, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_border_width(home_ui.progress_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(home_ui.progress_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(home_ui.progress_bar, lv_color_hex(LV_COLOR_THEME_YELLOW), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(home_ui.progress_bar, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(home_ui.progress_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    if (last_page->id == PAGE_START)
    {
        lv_obj_scroll_to_view(lv_obj_get_child(app_scroll_cont, 0), LV_ANIM_OFF);
    }
    else
    {
        lv_obj_set_height(home_ui.progress_bar, progress_bar_y);
        lv_obj_scroll_to_view(lv_obj_get_child(app_scroll_cont, last_page->app_id), LV_ANIM_OFF);
        lv_group_focus_obj(lv_obj_get_child(app_scroll_cont, last_page->app_id));
    }

    lv_obj_t *logo_label = lv_label_create(page->root);
    lv_obj_set_size(logo_label, 200, 32);
    lv_obj_align(logo_label, LV_ALIGN_TOP_MID, 0, 400);
    lv_label_set_text(logo_label, "LILYGO");
    lv_obj_set_style_text_color(logo_label, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(logo_label, LV_OPA_80, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(logo_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(logo_label, &lv_font_montserrat_30, LV_PART_MAIN | LV_STATE_DEFAULT);

}

static void home_page_on_create(struct Page *page)
{
    log_d("home_page_on_create\n");

    lv_obj_set_scrollbar_mode(page->root, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(page->root, LV_DIR_NONE);

    lv_obj_t* screen_app_img_3 = lv_image_create(page->root);
    lv_obj_align(screen_app_img_3, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_size(screen_app_img_3, 480, 480);
    lv_obj_add_flag(screen_app_img_3, LV_OBJ_FLAG_CLICKABLE);
    lv_image_set_src(screen_app_img_3, &dark_bg2);
    lv_image_set_pivot(screen_app_img_3, 50,50);
    lv_image_set_rotation(screen_app_img_3, 0);

    //Write style for screen_app_img_3, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_image_recolor_opa(screen_app_img_3, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_image_opa(screen_app_img_3, 255, LV_PART_MAIN|LV_STATE_DEFAULT);

    status_bar_init(page->root);
    app_icon_ui_init(page);
}

static void error_panel_close_cb(lv_event_t *e)
{
    lv_obj_t *panel = (lv_obj_t *)lv_event_get_user_data(e);
    lv_obj_del(panel);
}

static void home_timer_callback(lv_timer_t *timer)
{
    date_time_t current_dt;
    uint32_t value;
    static uint8_t updata_count = 0;
    updata_count++;
    if (updata_count % 25 == 0) // 5s
    {
        updata_count = 0;
        get_date_time(&current_dt);
        lv_label_set_text_fmt(home_ui.status_bar_time_label, "%02d:%02d", current_dt.hour, current_dt.minute);

        if (home_ui.status_bar_battery_icon != NULL && home_ui.status_bar_battery_label != NULL)
        {
            int32_t battery_percentage = get_charge_percent();
            lv_label_set_text_fmt(home_ui.status_bar_battery_label, "%d%%", battery_percentage);
            if (battery_percentage >= 80)
            {
                lv_obj_set_style_text_color(home_ui.status_bar_battery_icon, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_label_set_text(home_ui.status_bar_battery_icon, "" LV_SYMBOL_BATTERY_FULL "");
            }
            else if (battery_percentage >= 60)
            {
                lv_obj_set_style_text_color(home_ui.status_bar_battery_icon, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_label_set_text(home_ui.status_bar_battery_icon, "" LV_SYMBOL_BATTERY_3 "");
            }
            else if (battery_percentage >= 40)
            {
                lv_obj_set_style_text_color(home_ui.status_bar_battery_icon, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_label_set_text(home_ui.status_bar_battery_icon, "" LV_SYMBOL_BATTERY_2 "");
            }
            else if (battery_percentage >= 20)
            {
                lv_obj_set_style_text_color(home_ui.status_bar_battery_icon, lv_color_hex(LV_COLOR_WARM_RED), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_label_set_text(home_ui.status_bar_battery_icon, "" LV_SYMBOL_BATTERY_1 "");
            }
            else
            {
                lv_obj_set_style_text_color(home_ui.status_bar_battery_icon, lv_color_hex(LV_COLOR_WARM_RED), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_label_set_text(home_ui.status_bar_battery_icon, "" LV_SYMBOL_BATTERY_EMPTY "");
            }
            sgm41562b_handle_t charege_handle = sgm41562b_get_handle();
            if ((CHG_STAT_PRE_CHARGE == sgm41562b_get_charge_status(charege_handle)) || (CHG_STAT_FAST_CHARGE == sgm41562b_get_charge_status(charege_handle)))
            {
                lv_obj_set_style_text_color(home_ui.status_bar_battery_label, lv_color_hex(LV_COLOR_THEME_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_text_color(home_ui.status_bar_battery_icon, lv_color_hex(LV_COLOR_THEME_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
            }
            else
            {
                lv_obj_set_style_text_color(home_ui.status_bar_battery_icon, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_obj_set_style_text_color(home_ui.status_bar_battery_label, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
            }
        }
    }

    bt_app_t *bt_env = bt_app_get_env();
    if (bt_env->bt_connected)
    {
        device_switch.bluetooth_connected = true;
        if (home_ui.status_bar_bluetooth_icon != NULL)
        {
            lv_obj_set_style_text_color(home_ui.status_bar_bluetooth_icon, lv_color_hex(LV_COLOR_THEME_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
    else
    {
        device_switch.bluetooth_connected = false;
        if (home_ui.status_bar_bluetooth_icon != NULL)
        {
            if (device_switch.bluetooth_enable)
                lv_obj_set_style_text_color(home_ui.status_bar_bluetooth_icon, lv_color_hex(LV_COLOR_BTN_BLUE), LV_PART_MAIN | LV_STATE_DEFAULT);
            else
                lv_obj_set_style_text_color(home_ui.status_bar_bluetooth_icon, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }

    if (bt_env->bluetooth_pan)
    {
        device_switch.bluetooth_pan_enable = true;
        if (!time_thread_created)
        {
            time_thread = rt_thread_create("time_sync",
                                           (void (*)(void *))get_time_async,
                                           &current_dt,
                                           1024,
                                           RT_THREAD_PRIORITY_MIDDLE,
                                           10);
            if (time_thread != RT_NULL)
            {
                time_thread_created = true;
                rt_thread_startup(time_thread);
            }
        }
    }
    else
    {
        if (time_thread != RT_NULL)
        {
            log_d("time_thread destroy");
            rt_thread_delete(time_thread);
            time_thread = RT_NULL;
            time_thread_created = false;
        }
    }

    if (device_switch.wifi_enable)
    {
        if (home_ui.status_bar_wifi_icon != NULL)
        {
            lv_obj_set_style_text_color(home_ui.status_bar_wifi_icon, lv_color_hex(LV_COLOR_THEME_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
    else
    {
        if (home_ui.status_bar_wifi_icon != NULL)
        {
            lv_obj_set_style_text_color(home_ui.status_bar_wifi_icon, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }

    if (lvgl_mb != RT_NULL)
    {
        if (rt_mb_recv(lvgl_mb, (rt_uint32_t *)&value, 10) == RT_EOK)
        {
            switch (value)
            {
            case UI_UPDATE_BLUETOOTH_CONNECT:
                break;
            case UI_UPDATE_BLUETOOTH_DISCONNECT:
                break;
            case UI_UPDATE_TIME:
                break;
            case UI_UPDATE_TIME_STOP:
                break;
            case UI_UPDATE_INIT_ERROR:
                if (init_error != RT_EOK)
                {
                    log_e("init_error: 0x%04x\n", init_error);

                    static const struct
                    {
                        uint16_t flag;
                        const char *name;
                    } err_map[] = {
                        {INIT_CHARGE_ERROR, "Charge Chip"},
                        {INIT_SD_ERROR, "SD Card"},
                        {INIT_AUDPRC_ERROR, "Audio Processor"},
                        {INIT_BLUETOOTH_ERROR, "Bluetooth"},
                        {INIT_KEY_BOARD_ERROR, "Keyboard"},
                        {INIT_LORA_SX1262_ERROR, "LoRa"},
                        {INIT_IMU_BHI260AP_ERROR, "IMU"},
                        {INIT_IO_XL9555_ERROR, "IO Expander"},
                        {INIT_SENSOR_BME280_ERROR, "Sensor (BME280)"},
                        {INIT_MOTOR_AW86224_ERROR, "Motor"},
                        {INIT_LED_AW21009_ERROR, "Key LED"},
                        {INIT_ESP32C6_ERROR, "ESP32-C6"},
                        {INIT_GPS_L76K_ERROR, "GPS"},
                    };
                    const int err_count = sizeof(err_map) / sizeof(err_map[0]);

                    /* Panel */
                    lv_obj_t *panel = lv_obj_create(page_list[PAGE_HOME].root);
                    lv_obj_set_size(panel, 480, 480);
                    lv_obj_center(panel);
                    lv_obj_set_style_bg_color(panel, lv_color_hex(LV_COLOR_THEME_WIHTE), 0);
                    lv_obj_set_style_radius(panel, 16, 0);
                    lv_obj_set_style_border_width(panel, 0, 0);
                    lv_obj_set_style_shadow_width(panel, 20, 0);
                    lv_obj_set_style_pad_all(panel, 0, 0);
                    lv_obj_set_style_clip_corner(panel, true, 0);
                    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

                    /* Header bar */
                    lv_obj_t *header = lv_obj_create(panel);
                    lv_obj_set_size(header, 480, 56);
                    lv_obj_set_style_bg_color(header, lv_color_hex(LV_COLOR_WARM_RED), 0);
                    lv_obj_set_style_radius(header, 0, 0);
                    lv_obj_set_style_border_width(header, 0, 0);
                    lv_obj_set_style_pad_all(header, 0, 0);
                    lv_obj_align(header, LV_ALIGN_TOP_LEFT, 0, 0);
                    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

                    lv_obj_t *title = lv_label_create(header);
                    lv_label_set_text(title, "Hardware Init Error");
                    lv_obj_set_style_text_color(title, lv_color_hex(LV_COLOR_THEME_WIHTE), 0);
                    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
                    lv_obj_center(title);

                    lv_obj_t *close_btn = lv_btn_create(header);
                    lv_obj_set_size(close_btn, 36, 36);
                    lv_obj_align(close_btn, LV_ALIGN_RIGHT_MID, -20, 0);
                    lv_obj_set_style_bg_color(close_btn, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_STATE_DEFAULT);
                    lv_obj_set_style_radius(close_btn, 18, 0);
                    lv_obj_set_style_shadow_width(close_btn, 0, 0);
                    lv_obj_set_style_border_width(close_btn, 0, 0);

                    lv_obj_t *close_x = lv_label_create(close_btn);
                    lv_label_set_text(close_x, LV_SYMBOL_CLOSE);
                    lv_obj_set_style_text_color(close_x, lv_color_hex(LV_COLOR_WARM_RED), LV_STATE_DEFAULT);
                    lv_obj_center(close_x);

                    /* Hint */
                    lv_obj_t *hint = lv_label_create(panel);
                    lv_label_set_text(hint, "Failed components:");
                    lv_obj_set_style_text_color(hint, lv_color_hex(LV_COLOR_THEME_DIM_GRAY), 0);
                    lv_obj_set_style_text_font(hint, &lv_font_montserrat_16, 0);
                    lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 24, 72);

                    /* Scrollable error list */
                    lv_obj_t *list = lv_obj_create(panel);
                    lv_obj_set_size(list, 400, 290);
                    lv_obj_align(list, LV_ALIGN_TOP_LEFT, 20, 100);
                    lv_obj_set_style_bg_color(list, lv_color_hex(LV_COLOR_THEME_WIHTE), 0);
                    lv_obj_set_style_border_width(list, 0, 0);
                    lv_obj_set_style_pad_all(list, 4, 0);
                    lv_obj_set_style_radius(list, 8, 0);

                    int failed = 0;
                    for (int i = 0; i < err_count; i++)
                    {
                        if (init_error & err_map[i].flag)
                        {
                            lv_obj_t *row = lv_obj_create(list);
                            lv_obj_set_size(row, 380, 38);
                            lv_obj_set_pos(row, 0, failed * 38);
                            lv_obj_set_style_bg_color(row, lv_color_hex(0xfafafa), 0);
                            lv_obj_set_style_border_width(row, 0, 0);
                            lv_obj_set_style_pad_all(row, 0, 0);
                            lv_obj_set_style_radius(row, 6, 0);
                            lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

                            if (failed % 2 == 0)
                            {
                                lv_obj_set_style_bg_color(row, lv_color_hex(0xf0f0f0), 0);
                            }

                            lv_obj_t *x_icon = lv_label_create(row);
                            lv_label_set_text(x_icon, LV_SYMBOL_CLOSE);
                            lv_obj_set_style_text_color(x_icon, lv_color_hex(LV_COLOR_WARM_RED), 0);
                            lv_obj_set_style_text_font(x_icon, &lv_font_montserrat_20, 0);
                            lv_obj_align(x_icon, LV_ALIGN_LEFT_MID, 14, 0);

                            lv_obj_t *name = lv_label_create(row);
                            lv_label_set_text(name, err_map[i].name);
                            lv_obj_set_style_text_color(name, lv_color_hex(LV_COLOR_THEME_BLACK), 0);
                            lv_obj_set_style_text_font(name, &lv_font_montserrat_20, 0);
                            lv_obj_align(name, LV_ALIGN_LEFT_MID, 40, 0);

                            failed++;
                        }
                    }

                    /* OK button */
                    lv_obj_t *ok_btn = lv_btn_create(panel);
                    lv_obj_set_size(ok_btn, 200, 44);
                    lv_obj_align(ok_btn, LV_ALIGN_BOTTOM_MID, 0, -16);
                    lv_obj_set_style_bg_color(ok_btn, lv_color_hex(LV_COLOR_WARM_RED), LV_STATE_DEFAULT);
                    lv_obj_set_style_radius(ok_btn, 22, 0);
                    lv_obj_set_style_shadow_width(ok_btn, 0, 0);

                    lv_obj_t *ok_label = lv_label_create(ok_btn);
                    lv_label_set_text(ok_label, "OK");
                    lv_obj_set_style_text_color(ok_label, lv_color_hex(LV_COLOR_THEME_WIHTE), 0);
                    lv_obj_set_style_text_font(ok_label, &lv_font_montserrat_24, 0);
                    lv_obj_center(ok_label);

                    lv_obj_add_event_cb(close_btn, error_panel_close_cb, LV_EVENT_CLICKED, panel);
                    lv_obj_add_event_cb(ok_btn, error_panel_close_cb, LV_EVENT_CLICKED, panel);
                }
                break;
            }
        }
    }
}

static void gesture_event_cb(lv_event_t *e)
{
    static lv_point_t start_point;
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_get_act();
    lv_obj_t *obj = lv_event_get_target(e);

    GestureDirection dir = (GestureDirection)lv_indev_get_gesture_dir(indev);
    log_d("LV_EVENT_GESTURE dir: %d\n", dir);

    switch (dir)
    {
    case GESTURE_DOWN:
        break;
    case GESTURE_UP:
        break;
    case GESTURE_RIGHT:
        if (current_page->id != PAGE_HOME)
        {
            switch_to_page(&page_list[PAGE_HOME]);
        }
        break;
    }
}

static void app_icon_event_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);
    int id = (int)lv_obj_get_user_data(obj);

    log_d("app_icon_event_cb %d", code);
    log_d("id %d", id);

    if (code == LV_EVENT_CLICKED)
    {
        switch (id)
        {
        case APP_MUSIC:
            switch_to_page(&page_list[PAGE_MUSIC]);
            break;
        case APP_LORA:
            switch_to_page(&page_list[PAGE_LORA]);
            break;
        case APP_SENSOR:
            switch_to_page(&page_list[PAGE_SENSOR]);
            break;
        case APP_GPS:
#ifdef PKG_USING_UART_MANGER
            uart_mux_switch_to(UART_MUX_DEVICE_GPS, 115200);
#endif
            switch_to_page(&page_list[PAGE_GPS]);
            break;
        case APP_WIFI:
#ifdef PKG_USING_UART_MANGER
            uart_mux_switch_to(UART_MUX_DEVICE_ESP32C6, 115200);
#endif
            switch_to_page(&page_list[PAGE_WIFI]);
            break;
        case APP_CHARGE:
            switch_to_page(&page_list[PAGE_CHARGE]);
            break;
        case APP_WEATHER:
            switch_to_page(&page_list[PAGE_WEATHER]);
            break;
        case APP_AUDIO:
            switch_to_page(&page_list[PAGE_AUDIO]);
            break;
        case APP_FILE:
            switch_to_page(&page_list[PAGE_FILE]);
            break;
        case APP_KEYBOARD:
            switch_to_page(&page_list[PAGE_KEYBOARD]);
            break;
        case APP_SYSTEM:
            switch_to_page(&page_list[PAGE_SYSTEM]);
            break;
        }
    }
}

static void scroll_event_cb(lv_event_t *e)
{
    static lv_point_t start_point;
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = lv_event_get_target(e);

    if (LV_EVENT_SCROLL == code)
    {
        int32_t scroll_y = lv_obj_get_scroll_y(obj);
        int32_t max_top = lv_obj_get_scroll_top(obj);
        int32_t max_bottom = lv_obj_get_scroll_bottom(obj); // 底部最大可滚动距离

        // log_d("scroll_y: %d, max_top: %d, max_bottom: %d", scroll_y, max_top, max_bottom);

        float currernt_y_percentage = (float)scroll_y * 1.0 / (max_top + max_bottom) * 1.0;
        currernt_y_percentage > 1.0 ? currernt_y_percentage = 1.0 : currernt_y_percentage;
        currernt_y_percentage < 0.0 ? currernt_y_percentage = 0.0 : currernt_y_percentage;

        if (home_ui.progress_bar != NULL)
        {
            int32_t height = lv_obj_get_height(home_ui.progress_bar);
            progress_bar_y = (int32_t)(220 * currernt_y_percentage);
            lv_obj_set_height(home_ui.progress_bar, progress_bar_y);
        }

        lv_area_t cont_a;
        lv_obj_get_coords(obj, &cont_a);
        int32_t cont_y_center = cont_a.y1 + lv_area_get_height(&cont_a) / 2;

        int32_t r = lv_obj_get_height(obj) * 5 / 10;
        int32_t i;
        int32_t child_cnt = (int32_t)lv_obj_get_child_count(obj);

        lv_obj_t *last_visible_child = NULL;
        int32_t max_y2 = -1;

        for (int i = 0; i < child_cnt; i++)
        {
            lv_obj_t *child = lv_obj_get_child(obj, i);
            lv_area_t child_a;
            lv_obj_get_coords(child, &child_a);

            int32_t child_y_center = child_a.y1 + lv_area_get_height(&child_a) / 2;
            int32_t diff_y = child_y_center - cont_y_center;
            diff_y = LV_ABS(diff_y);

            int32_t x;
            if (diff_y >= r)
            {
                x = r;
            }
            else
            {
                /*Use Pythagoras theorem to get x from radius and y*/
                uint32_t x_sqr = r * r - diff_y * diff_y;
                lv_sqrt_res_t res;
                lv_sqrt(x_sqr, &res, 0x8000); /*Use lvgl's built in sqrt root function*/
                x = r - res.i;
            }

            lv_obj_set_style_translate_x(child, x, 0);
            lv_opa_t opa = (lv_opa_t)lv_map(x, 0, r, LV_OPA_TRANSP, LV_OPA_COVER);
            lv_obj_set_style_opa(child, LV_OPA_COVER - opa, 0);
        }
    }

    // if (LV_EVENT_SCROLL_END == code)
    // {
    //     lv_area_t cont_a;
    //     lv_obj_get_coords(obj, &cont_a);
    //     int32_t cont_y_center = cont_a.y1 + lv_area_get_height(&cont_a) / 2;

    //     int32_t min_diff = INT32_MAX;
    //     lv_obj_t *closest = NULL;
    //     uint32_t child_cnt = lv_obj_get_child_count(obj);

    //     for (uint32_t i = 0; i < child_cnt; i++)
    //     {
    //         lv_obj_t *child = lv_obj_get_child(obj, i);
    //         lv_area_t child_a;
    //         lv_obj_get_coords(child, &child_a);
    //         int32_t child_y_center = child_a.y1 + lv_area_get_height(&child_a) / 2;
    //         int32_t diff = LV_ABS(child_y_center - cont_y_center);
    //         if (diff < min_diff)
    //         {
    //             min_diff = diff;
    //             closest = child;
    //         }
    //     }

    //     if (closest)
    //     {
    //         lv_group_t *g = lv_obj_get_group(closest);
    //         if (g && lv_group_get_focused(g) != closest)
    //         {
    //             lv_group_focus_obj(closest);
    //         }
    //     }
    // }
}

static void home_focus_cb(lv_group_t *group)
{
    lv_obj_t *focused_obj = lv_group_get_focused(group);
    if (focused_obj == NULL)
        return;
    lv_obj_scroll_to_view(focused_obj, LV_ANIM_ON);
}

rt_mailbox_t get_lvgl_mb(void)
{
    if (lvgl_mb != RT_NULL)
        return lvgl_mb;
    return RT_NULL;
}

/**********music page **********/
static void music_page_on_create(struct Page *page)
{
    log_d("music_page_on_create\n");
    music_info_t *bt_music_info = get_bt_music_info();
    music_info_t *music_info = get_bt_music_info();

    lv_obj_set_scrollbar_mode(page->root, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(page->root, LV_DIR_NONE);
    ui_event_t = MUSIC_EVENT_MUSIC_PAGE;
    lv_obj_add_event_cb(page->root, music_event_cb, LV_EVENT_ALL, (void *)(uintptr_t)ui_event_t);

    lv_obj_t *page_tile = lv_label_create(page->root);
    lv_obj_set_height(page_tile, 30);
    lv_obj_align(page_tile, LV_ALIGN_TOP_MID, 0, 10);
    lv_label_set_text(page_tile, "Music");
    lv_obj_set_style_text_font(page_tile, &lv_font_montserrat_36, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(page_tile, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    music_ui.music_name_label = lv_label_create(page->root);
    lv_obj_set_height(music_ui.music_name_label, 36);
    lv_obj_set_width(music_ui.music_name_label, 400);
    lv_obj_align(music_ui.music_name_label, LV_ALIGN_TOP_MID, 0, 70);
    lv_label_set_text(music_ui.music_name_label, bt_music_info->title);
    lv_obj_set_style_text_font(music_ui.music_name_label, &lv_font_simyou_30_ch, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(music_ui.music_name_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(music_ui.music_name_label, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_long_mode(music_ui.music_name_label, LV_LABEL_LONG_SCROLL);

    music_ui.music_artist_label = lv_label_create(page->root);
    lv_obj_set_height(music_ui.music_artist_label, 36);
    lv_obj_set_width(music_ui.music_artist_label, 400);
    lv_obj_align(music_ui.music_artist_label, LV_ALIGN_TOP_MID, 0, 130);
    lv_label_set_text(music_ui.music_artist_label, bt_music_info->artist);
    lv_obj_set_style_text_font(music_ui.music_artist_label, &lv_font_simyou_30_ch, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(music_ui.music_artist_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(music_ui.music_artist_label, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(music_ui.music_artist_label, LV_OPA_70, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_long_mode(music_ui.music_artist_label, LV_LABEL_LONG_SCROLL);

    music_ui.music_pause_btn = lv_button_create(page->root);
    lv_obj_align(music_ui.music_pause_btn, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_size(music_ui.music_pause_btn, 120, 120);
    music_ui.music_pause_btn_label = lv_label_create(music_ui.music_pause_btn);

    if (!bt_music_info->is_playing)
    {
        lv_label_set_text(music_ui.music_pause_btn_label, LV_SYMBOL_PAUSE);
    }
    else
    {
        lv_label_set_text(music_ui.music_pause_btn_label, LV_SYMBOL_PLAY);
    }
    lv_label_set_long_mode(music_ui.music_pause_btn_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(music_ui.music_pause_btn_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_pad_all(music_ui.music_pause_btn, 0, LV_STATE_DEFAULT);
    lv_obj_set_width(music_ui.music_pause_btn_label, LV_PCT(100));

    // Write style for screen_music_btn_1, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_bg_opa(music_ui.music_pause_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(music_ui.music_pause_btn, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(music_ui.music_pause_btn, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(music_ui.music_pause_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(music_ui.music_pause_btn, 60, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(music_ui.music_pause_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(music_ui.music_pause_btn, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(music_ui.music_pause_btn, &lv_font_montserrat_48, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(music_ui.music_pause_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(music_ui.music_pause_btn, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_event_t = MUSIC_EVENT_START;
    lv_obj_add_event_cb(music_ui.music_pause_btn, music_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)ui_event_t);

    lv_obj_t *music_last_btn = lv_button_create(page->root);
    lv_obj_align(music_last_btn, LV_ALIGN_LEFT_MID, 20, 20);
    lv_obj_set_size(music_last_btn, 120, 120);
    lv_obj_t *music_last_btn_label = lv_label_create(music_last_btn);
    lv_label_set_text(music_last_btn_label, LV_SYMBOL_PREV);
    lv_label_set_long_mode(music_last_btn_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(music_last_btn_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_pad_all(music_last_btn, 0, LV_STATE_DEFAULT);
    lv_obj_set_width(music_last_btn_label, LV_PCT(100));

    // Write style for screen_music_btn_1, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_bg_opa(music_last_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(music_last_btn, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(music_last_btn, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(music_last_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(music_last_btn, 60, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(music_last_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(music_last_btn, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(music_last_btn, &lv_font_montserrat_48, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(music_last_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(music_last_btn, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_event_t = MUSIC_EVENT_LAST;
    lv_obj_add_event_cb(music_last_btn, music_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)ui_event_t);

    lv_obj_t *music_next_btn = lv_button_create(page->root);
    lv_obj_align(music_next_btn, LV_ALIGN_RIGHT_MID, -20, 20);
    lv_obj_set_size(music_next_btn, 120, 120);
    lv_obj_t *music_next_btn_label = lv_label_create(music_next_btn);
    lv_label_set_text(music_next_btn_label, LV_SYMBOL_NEXT);
    lv_label_set_long_mode(music_next_btn_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(music_next_btn_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_pad_all(music_next_btn, 0, LV_STATE_DEFAULT);
    lv_obj_set_width(music_next_btn_label, LV_PCT(100));

    // Write style for screen_music_btn_1, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_bg_opa(music_next_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(music_next_btn, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(music_next_btn, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(music_next_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(music_next_btn, 60, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(music_next_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(music_next_btn, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(music_next_btn, &lv_font_montserrat_48, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(music_next_btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(music_next_btn, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    ui_event_t = MUSIC_EVENT_NEXT;
    lv_obj_add_event_cb(music_next_btn, music_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)ui_event_t);

    music_ui.music_progress_bar = lv_bar_create(page->root);
    lv_obj_align(music_ui.music_progress_bar, LV_ALIGN_BOTTOM_MID, 0, -100);
    lv_obj_set_size(music_ui.music_progress_bar, 420, 6);
    lv_obj_set_style_anim_duration(music_ui.music_progress_bar, 1000, 0);
    lv_bar_set_mode(music_ui.music_progress_bar, LV_BAR_MODE_NORMAL);
    lv_bar_set_range(music_ui.music_progress_bar, 0, 100);
    int pct = LV_CLAMP(0, (int)((float)music_info->current_ms / (float)music_info->duration_ms * 100.0f), 100);
    lv_bar_set_value(music_ui.music_progress_bar, pct, LV_ANIM_OFF);

    lv_obj_set_style_bg_opa(music_ui.music_progress_bar, 125, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(music_ui.music_progress_bar, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(music_ui.music_progress_bar, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(music_ui.music_progress_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(music_ui.music_progress_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_style_bg_opa(music_ui.music_progress_bar, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(music_ui.music_progress_bar, lv_color_hex(LV_COLOR_THEME_GREEN), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(music_ui.music_progress_bar, LV_GRAD_DIR_NONE, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(music_ui.music_progress_bar, 0, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    char current_time_buf[8];
    format_time_str(current_time_buf, music_info->current_ms);
    music_ui.voide_current_time_label = lv_label_create(page->root);
    lv_obj_set_height(music_ui.voide_current_time_label, 22);
    lv_obj_align_to(music_ui.voide_current_time_label, music_ui.music_progress_bar, LV_ALIGN_OUT_TOP_LEFT, 0, 0);
    lv_label_set_text(music_ui.voide_current_time_label, current_time_buf);
    lv_label_set_long_mode(music_ui.voide_current_time_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(music_ui.voide_current_time_label, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(music_ui.voide_current_time_label, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(music_ui.voide_current_time_label, LV_OPA_80, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(music_ui.voide_current_time_label, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_line_space(music_ui.voide_current_time_label, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(music_ui.voide_current_time_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);

    char end_time_buf[8];
    format_time_str(end_time_buf, music_info->duration_ms);
    music_ui.voide_end_time_label = lv_label_create(page->root);
    lv_obj_set_height(music_ui.voide_end_time_label, 22);
    lv_obj_align_to(music_ui.voide_end_time_label, music_ui.music_progress_bar, LV_ALIGN_OUT_TOP_RIGHT, -25, 0);
    lv_label_set_text(music_ui.voide_end_time_label, end_time_buf);
    lv_label_set_long_mode(music_ui.voide_end_time_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(music_ui.voide_end_time_label, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(music_ui.voide_end_time_label, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(music_ui.voide_end_time_label, LV_OPA_80, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(music_ui.voide_end_time_label, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_line_space(music_ui.voide_end_time_label, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(music_ui.voide_end_time_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_group_add_obj(page->group, music_last_btn);
    lv_group_add_obj(page->group, music_ui.music_pause_btn);
    lv_group_add_obj(page->group, music_next_btn);

}

static void music_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *target_obj = lv_event_get_target(e);
    int event_type = (int)lv_event_get_user_data(e);
    bt_app_t *bt_app = bt_app_get_env();
    music_info_t *music_info = get_bt_music_info();
    if (code == LV_EVENT_CLICKED)
    {
        switch (event_type)
        {
        case MUSIC_EVENT_START:
            log_d("is_playing: %d\n", music_info->is_playing);
            if (music_info->is_playing)
            {
                if (bt_app->g_bt_app_mb != NULL)
                {
                    rt_mb_send(bt_app->g_bt_app_mb, BT_APP_AUDIO_PLAY);
                }
            }
            else
            {
                if (bt_app->g_bt_app_mb != NULL)
                {
                    rt_mb_send(bt_app->g_bt_app_mb, BT_APP_AUDIO_PAUSE);
                }
            }
            break;
        case MUSIC_EVENT_LAST:
            log_d("music_event_cb:MUSIC_EVENT_LAST\n");
            if (bt_app->g_bt_app_mb != NULL)
            {
                rt_mb_send(bt_app->g_bt_app_mb, BT_APP_AUDIO_LAST);
            }
            break;
        case MUSIC_EVENT_NEXT:
            log_d("music_event_cb:MUSIC_EVENT_NEXT\n");
            if (bt_app->g_bt_app_mb != NULL)
            {
                rt_mb_send(bt_app->g_bt_app_mb, BT_APP_AUDIO_NEXT);
            }
            break;
        default:
            break;
        }
    }
}

static void format_time_str(char *buf, int ms)
{
    int total_sec = ms / 1000;
    int min = (total_sec % 3600) / 60;
    int sec = total_sec % 60;
    snprintf(buf, 8, "%02d:%02d", min, sec);
}

void music_event_name_info_update(void *data)
{
    char *title = (char *)data;
    // log_d("music_event_name_info_update : %s\n", (char *)title);
    if (current_page->id == PAGE_MUSIC)
    {
        lv_label_set_text(music_ui.music_name_label, title);
    }
}

void music_event_artist_info_update(void *data)
{
    char *artist = (char *)data;
    // log_d("music_event_artist_info_update : %s\n", (char *)artist);
    if (current_page->id == PAGE_MUSIC)
    {
        lv_label_set_text(music_ui.music_artist_label, artist);
    }
}

void music_progress_update(void *data)
{
    uint32_t duration_ms = (uint32_t)(uintptr_t)data;
    if (current_page->id == PAGE_MUSIC)
    {
        // log_d("music_progress_update : %d\n", duration_ms);
        if (music_ui.music_progress_bar)
        {
            lv_bar_set_value(music_ui.music_progress_bar, 0, LV_ANIM_OFF);
        }
        if (music_ui.voide_end_time_label)
        {
            char buf[8];
            format_time_str(buf, duration_ms);
            lv_label_set_text(music_ui.voide_end_time_label, buf);
        }
    }
}

void music_current_time_update(uint32_t current_ms, uint32_t duration_ms)
{
    // uint32_t current_ms = (uint32_t)(uintptr_t)data;
    // music_info_t *bt_music_info = get_bt_music_info();
    if (current_page->id == PAGE_MUSIC)
    {
        if (music_ui.music_progress_bar)
        {
            int pct = LV_CLAMP(0, (int)((float)current_ms / (float)duration_ms * 100.0f), 100);
            lv_bar_set_value(music_ui.music_progress_bar, pct, LV_ANIM_OFF);
        }
        if (music_ui.voide_current_time_label)
        {
            char buf[8];
            format_time_str(buf, current_ms);
            lv_label_set_text(music_ui.voide_current_time_label, buf);
        }
    }
}

void music_pause_btn_update(void *data)
{
    uint8_t is_playing = (uint8_t)(uintptr_t)data;
    if (current_page->id == PAGE_MUSIC)
    {
        lv_label_set_text(music_ui.music_pause_btn_label,
                          is_playing == 0 ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    }
}

static void music_timer_callback(lv_timer_t *timer)
{
    bt_app_t *bt_env = bt_app_get_env();
    if (bt_env->bt_connected)
    {
        music_info_t *bt_music_info = get_bt_music_info();
        if (bt_music_info)
        {
            music_event_name_info_update((void *)bt_music_info->title);
            music_event_artist_info_update((void *)bt_music_info->artist);
            music_progress_update((void *)bt_music_info->duration_ms);
            music_current_time_update(bt_music_info->current_ms, bt_music_info->duration_ms);
            music_pause_btn_update((void *)bt_music_info->is_playing);
        }

        // uint8_t volume = get_bt_volume();
        // lv_slider_set_value(music_ui.volume_slider, volume, LV_ANIM_OFF);
    }
}

/**********lora page **********/
static void lora_page_on_create(struct Page *page)
{
    radio_set_rx_callback(radio_rx_callback);

    LOG_D("lora_page_on_create\n");
    lv_group_set_editing(page->group, true);

    lv_obj_t *page_tile = lv_label_create(page->root);
    lv_obj_set_height(page_tile, 30);
    lv_obj_align(page_tile, LV_ALIGN_TOP_MID, 0, 10);
    lv_label_set_text(page_tile, "Lora");
    lv_obj_set_style_text_font(page_tile, &lv_font_montserrat_36, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(page_tile, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    static lv_style_t config_cont_style;
    lv_style_init(&config_cont_style);
    lv_style_set_border_width(&config_cont_style, 0);
    lv_style_set_bg_opa(&config_cont_style, LV_OPA_100);
    lv_style_set_pad_all(&config_cont_style, 0);
    lv_style_set_pad_bottom(&config_cont_style, 40);
    lv_style_set_radius(&config_cont_style, 20);

    lora_ui.lora_config_cont = lv_obj_create(page->root);
    lv_obj_set_size(lora_ui.lora_config_cont, 460, 320);
    lv_obj_align(lora_ui.lora_config_cont, LV_ALIGN_TOP_LEFT, 10, 60);
    lv_obj_add_style(lora_ui.lora_config_cont, &config_cont_style, 0);
    lv_obj_set_style_bg_color(lora_ui.lora_config_cont, lv_color_hex(LV_COLOR_THEME_BLACK), 0);

    char *lora_config_label_text[7] = {"Mode", "Freq", "Power", "BW", "SF", "CR", "SyncWord"};
    char *lora_config_ddlist_text[7] = {"LORA",
                                        "868Mhz\n915Mhz",
                                        "22dBm\n15dBm\n10dBm\n5dBm\n0dBm\n-5dBm\n-9dBm",
                                        "62Khz\n125kHz\n250kHz\n500kHz",
                                        "5\n6\n7\n8\n9\n10\n11\n12",
                                        "5\n6\n7\n8",
                                        "0x1424"};
    for (int i = 0; i < sizeof(lora_config_label_text) / sizeof(lora_config_label_text[0]); i++)
    {
        lv_obj_t *label = lv_label_create(lora_ui.lora_config_cont);
        lv_obj_set_size(label, 200, 36);
        lv_obj_align(label, LV_ALIGN_TOP_LEFT, 10, 50 + i * 80);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(label, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(label, lora_config_label_text[i]);

        lv_obj_t *lora_mdoe_ddlist = lv_dropdown_create(lora_ui.lora_config_cont);
        lv_obj_set_size(lora_mdoe_ddlist, 200, 50);
        lv_obj_align(lora_mdoe_ddlist, LV_ALIGN_TOP_RIGHT, -40, 40 + i * 80);
        lv_dropdown_set_options(lora_mdoe_ddlist, lora_config_ddlist_text[i]);

        lv_obj_set_style_text_font(lora_mdoe_ddlist, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(lora_mdoe_ddlist, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_opa(lora_mdoe_ddlist, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(lora_mdoe_ddlist, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_opa(lora_mdoe_ddlist, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(lora_mdoe_ddlist, lv_color_hex(LV_COLOR_THEME_GRAY), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_side(lora_mdoe_ddlist, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_top(lora_mdoe_ddlist, 15, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_left(lora_mdoe_ddlist, 50, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_right(lora_mdoe_ddlist, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(lora_mdoe_ddlist, 15, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(lora_mdoe_ddlist, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(lora_mdoe_ddlist, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_grad_dir(lora_mdoe_ddlist, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_width(lora_mdoe_ddlist, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        ui_event_t = LORA_EVENT_MODE + i;
        lv_obj_add_event_cb(lora_mdoe_ddlist, lora_event_cb, LV_EVENT_ALL, (void *)((uintptr_t)ui_event_t));
        if (lora_config_label_text[i] == "Freq")
        {
            lv_dropdown_set_selected(lora_mdoe_ddlist, 0);
        }
        else if (lora_config_label_text[i] == "Power")
        {
            lv_dropdown_set_selected(lora_mdoe_ddlist, 0);
        }
        else if (lora_config_label_text[i] == "BW")
        {
            lv_dropdown_set_selected(lora_mdoe_ddlist, 1);
        }
        else if (lora_config_label_text[i] == "SF")
        {
            lv_dropdown_set_selected(lora_mdoe_ddlist, 0);
        }
        else if (lora_config_label_text[i] == "CR")
        {
            lv_dropdown_set_selected(lora_mdoe_ddlist, 0);
        }

        lv_obj_add_flag(lora_mdoe_ddlist, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        lv_group_add_obj(page->group, lora_mdoe_ddlist);
    }

    static lv_style_t btn_style;
    lv_style_init(&btn_style);
    lv_style_set_bg_opa(&btn_style, LV_OPA_COVER);
    lv_style_set_radius(&btn_style, 25);
    lv_style_set_pad_all(&btn_style, 0);
    lv_style_set_border_width(&btn_style, 0);
    lv_style_set_text_color(&btn_style, lv_color_hex(LV_COLOR_THEME_BLACK));
    lv_style_set_shadow_width(&btn_style, 0);

    lv_obj_t *lora_send_btn = lv_btn_create(page->root);
    lv_obj_set_size(lora_send_btn, 150, 50);
    lv_obj_add_style(lora_send_btn, &btn_style, 0);
    lv_obj_align(lora_send_btn, LV_ALIGN_BOTTOM_LEFT, 50, -30);
    lv_obj_set_style_bg_color(lora_send_btn, lv_color_hex(LV_COLOR_THEME_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
#ifdef PKG_USING_LORA_RADIO_DRIVER
    ui_event_t = LORA_EVENT_SEND_MESSAGE;
    lv_obj_add_event_cb(lora_send_btn, lora_event_cb, LV_EVENT_CLICKED, (void *)((uintptr_t)ui_event_t));
#endif
    lv_group_add_obj(page->group, lora_send_btn);

    lv_obj_t *lora_send_label = lv_label_create(lora_send_btn);
    lv_label_set_text(lora_send_label, "Send");
    lv_obj_align(lora_send_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(lora_send_label, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(lora_send_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *lora_receive_btn = lv_btn_create(page->root);
    lv_obj_set_size(lora_receive_btn, 150, 50);
    lv_obj_add_style(lora_receive_btn, &btn_style, 0);
    lv_obj_align(lora_receive_btn, LV_ALIGN_BOTTOM_RIGHT, -50, -30);
    lv_obj_set_style_bg_color(lora_receive_btn, lv_color_hex(LV_COLOR_THEME_YELLOW), LV_PART_MAIN | LV_STATE_DEFAULT);
#ifdef PKG_USING_LORA_RADIO_DRIVER
    ui_event_t = LORA_EVENT_RECEIVE_MESSAGE;
    lv_obj_add_event_cb(lora_receive_btn, lora_event_cb, LV_EVENT_CLICKED, (void *)((uintptr_t)ui_event_t));
#endif
    lv_group_add_obj(page->group, lora_receive_btn);

    lv_obj_t *lora_receive_label = lv_label_create(lora_receive_btn);
    lv_obj_align(lora_receive_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(lora_receive_label, "Receive");
    lv_obj_set_style_text_font(lora_receive_label, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(lora_receive_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    lora_ui.lora_send_recevice_msg_cont = lv_obj_create(page->root);
    lv_obj_set_size(lora_ui.lora_send_recevice_msg_cont, 460, 340);
    lv_obj_align(lora_ui.lora_send_recevice_msg_cont, LV_ALIGN_TOP_LEFT, 10, 60);
    lv_obj_add_style(lora_ui.lora_send_recevice_msg_cont, &config_cont_style, 0);
    lv_obj_add_flag(lora_ui.lora_send_recevice_msg_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(lora_ui.lora_send_recevice_msg_cont, lv_color_hex(LV_COLOR_THEME_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_scrollbar_mode(lora_ui.lora_send_recevice_msg_cont, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *tx_tile = lv_label_create(lora_ui.lora_send_recevice_msg_cont);
    lv_obj_set_height(tx_tile, 25);
    lv_obj_align(tx_tile, LV_ALIGN_TOP_MID, 0, 15);
    lv_label_set_text(tx_tile, "TX");
    lv_obj_set_style_text_font(tx_tile, &lv_font_montserrat_30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(tx_tile, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *tx_time_tile = lv_label_create(lora_ui.lora_send_recevice_msg_cont);
    lv_obj_set_height(tx_time_tile, 30);
    lv_obj_align(tx_time_tile, LV_ALIGN_TOP_RIGHT, -30, 10);
    lv_label_set_text(tx_time_tile, "1s send");
    lv_obj_set_style_text_font(tx_time_tile, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(tx_time_tile, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    lora_ui.tx_msg_label = lv_label_create(lora_ui.lora_send_recevice_msg_cont);
    lv_obj_set_height(lora_ui.tx_msg_label, 25);
    lv_obj_align(lora_ui.tx_msg_label, LV_ALIGN_TOP_MID, 0, 60);
    lv_label_set_text(lora_ui.tx_msg_label, "TX Message:");
    lv_obj_set_style_text_font(lora_ui.tx_msg_label, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(lora_ui.tx_msg_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *rx_tile = lv_label_create(lora_ui.lora_send_recevice_msg_cont);
    lv_obj_set_height(rx_tile, 25);
    lv_obj_align(rx_tile, LV_ALIGN_TOP_MID, 0, 150);
    lv_label_set_text(rx_tile, "RX");
    lv_obj_set_style_text_font(rx_tile, &lv_font_montserrat_30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(rx_tile, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    lora_ui.rx_msg_label = lv_label_create(lora_ui.lora_send_recevice_msg_cont);
    lv_obj_set_height(lora_ui.rx_msg_label, 25);
    lv_obj_align(lora_ui.rx_msg_label, LV_ALIGN_TOP_MID, 0, 200);
    lv_label_set_text(lora_ui.rx_msg_label, "RX Message:");
    lv_obj_set_style_text_font(lora_ui.rx_msg_label, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(lora_ui.rx_msg_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    lora_ui.rx_rssi_label = lv_label_create(lora_ui.lora_send_recevice_msg_cont);
    lv_obj_set_height(lora_ui.rx_rssi_label, 25);
    lv_obj_align(lora_ui.rx_rssi_label, LV_ALIGN_TOP_MID, 0, 230);
    lv_label_set_text(lora_ui.rx_rssi_label, "RX RSSI:");
    lv_obj_set_style_text_font(lora_ui.rx_rssi_label, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(lora_ui.rx_rssi_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    lora_ui.rx_snr_label = lv_label_create(lora_ui.lora_send_recevice_msg_cont);
    lv_obj_set_height(lora_ui.rx_snr_label, 25);
    lv_obj_align(lora_ui.rx_snr_label, LV_ALIGN_TOP_MID, 0, 260);
    lv_label_set_text(lora_ui.rx_snr_label, "RX SNR:");
    lv_obj_set_style_text_font(lora_ui.rx_snr_label, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(lora_ui.rx_snr_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void lora_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *target_obj = lv_event_get_target(e);
    int event_id = (int)lv_event_get_user_data(e);
    int index = lv_dropdown_get_selected(target_obj);
    switch (code)
    {
    case LV_EVENT_VALUE_CHANGED:
        switch (event_id)
        {
        case LORA_EVENT_MODE:
            /* only LoRa mode supported */
            break;
        case LORA_EVENT_FREQ:
            switch (index)
            {
            case 0:
                radio_set_freq(868000000);
                break;
            case 1:
                radio_set_freq(915000000);
                break;
            }
            break;
        case LORA_EVENT_POWER:
            switch (index)
            {
            case 0:
                radio_set_outpower(22);
                break;
            case 1:
                radio_set_outpower(15);
                break;
            case 2:
                radio_set_outpower(10);
                break;
            case 3:
                radio_set_outpower(5);
                break;
            case 4:
                radio_set_outpower(0);
                break;
            case 5:
                radio_set_outpower(-5);
                break;
            case 6:
                radio_set_outpower(-9);
                break;
            }
            break;
        case LORA_EVENT_BW:
            switch (index)
            {
            case 0:
                radio_set_lora_bandwidth(LORA_BW_62KHZ);
                break;
            case 1:
                radio_set_lora_bandwidth(LORA_BW_125KHZ);
                break;
            case 2:
                radio_set_lora_bandwidth(LORA_BW_250KHZ);
                break;
            case 3:
                radio_set_lora_bandwidth(LORA_BW_500KHZ);
                break;
            }
            break;
        case LORA_EVENT_SF:
            switch (index)
            {
            case 0:
                radio_set_lora_sf(LORA_SPREADING_FACTOR_5);
                break;
            case 1:
                radio_set_lora_sf(LORA_SPREADING_FACTOR_6);
                break;
            case 2:
                radio_set_lora_sf(LORA_SPREADING_FACTOR_7);
                break;
            case 3:
                radio_set_lora_sf(LORA_SPREADING_FACTOR_8);
                break;
            case 4:
                radio_set_lora_sf(LORA_SPREADING_FACTOR_9);
                break;
            case 5:
                radio_set_lora_sf(LORA_SPREADING_FACTOR_10);
                break;
            case 6:
                radio_set_lora_sf(LORA_SPREADING_FACTOR_11);
                break;
            case 7:
                radio_set_lora_sf(LORA_SPREADING_FACTOR_12);
                break;
            }
            break;
        case LORA_EVENT_CR:
            switch (index)
            {
            case 0:
                radio_set_lora_cr(LORA_CODINGRATE_4_5);
                break;
            case 1:
                radio_set_lora_cr(LORA_CODINGRATE_4_6);
                break;
            case 2:
                radio_set_lora_cr(LORA_CODINGRATE_4_7);
                break;
            case 3:
                radio_set_lora_cr(LORA_CODINGRATE_4_8);
                break;
            }
            break;
        case LORA_EVENT_SYNC:
            break;
        }
        break;

    case LV_EVENT_CLICKED:
        switch (event_id)
        {
        case LORA_EVENT_SEND_MESSAGE:
            if (device_switch.lora_tx_enable)
            {
                device_switch.lora_tx_enable = false;
                radio_sleep();
                lv_obj_clear_flag(lora_ui.lora_config_cont, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(lora_ui.lora_send_recevice_msg_cont, LV_OBJ_FLAG_HIDDEN);
                anim_zoom(lora_ui.lora_config_cont, 400, 0, 460, 0, 320);
            }
            else
            {
                device_switch.lora_tx_enable = false;
                radio_sleep();

                lv_obj_add_flag(lora_ui.lora_config_cont, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_style_bg_color(lora_ui.lora_send_recevice_msg_cont, lv_color_hex(LV_COLOR_THEME_GREEN), 0);
                lv_obj_clear_flag(lora_ui.lora_send_recevice_msg_cont, LV_OBJ_FLAG_HIDDEN);
                anim_zoom(lora_ui.lora_send_recevice_msg_cont, 400, 0, 460, 0, 320);

                lora_tx_count = 0;
                device_switch.lora_tx_enable = true;
                rt_thread_t tid = rt_thread_create("lora_send",
                                                   lora_send_thread_entry, NULL, 2048, 3, 10);
                if (tid)
                    rt_thread_startup(tid);
            }
            break;
        case LORA_EVENT_RECEIVE_MESSAGE:
            if (device_switch.lora_rx_enable)
            {
                device_switch.lora_rx_enable = false;
                radio_sleep();
                lv_obj_clear_flag(lora_ui.lora_config_cont, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(lora_ui.lora_send_recevice_msg_cont, LV_OBJ_FLAG_HIDDEN);
                anim_zoom(lora_ui.lora_config_cont, 400, 0, 460, 0, 320);
            }
            else
            {
                device_switch.lora_rx_enable = false;
                radio_sleep();

                lv_obj_add_flag(lora_ui.lora_config_cont, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_style_bg_color(lora_ui.lora_send_recevice_msg_cont, lv_color_hex(LV_COLOR_THEME_YELLOW), 0);
                lv_obj_clear_flag(lora_ui.lora_send_recevice_msg_cont, LV_OBJ_FLAG_HIDDEN);
                anim_zoom(lora_ui.lora_send_recevice_msg_cont, 400, 0, 460, 0, 320);

                device_switch.lora_rx_enable = true;
                radio_rx();
            }
            break;
        }
        break;
    }
}

static void lora_tx_ui_update_cb(void *data)
{
    (void)data;
    if (lora_ui.lora_send_recevice_msg_cont == NULL)
        return;
    if (lora_ui.tx_msg_label)
    {
        lv_label_set_text_fmt(lora_ui.tx_msg_label, "TX Message: %s", lora_tx_display_buf);
    }
}

static void lora_rx_ui_update_cb(void *data)
{
    (void)data;
    if (lora_ui.lora_send_recevice_msg_cont == NULL)
        return;
    if (lora_ui.rx_msg_label)
    {
        lv_label_set_text_fmt(lora_ui.rx_msg_label, "RX Message: %s",
                              (const char *)lora_rx_display_buf.data);
    }
    if (lora_ui.rx_rssi_label)
    {
        lv_label_set_text_fmt(lora_ui.rx_rssi_label, "RX RSSI: %d dBm", lora_rx_display_buf.rssi);
    }
    if (lora_ui.rx_snr_label)
    {
        lv_label_set_text_fmt(lora_ui.rx_snr_label, "RX SNR: %d", lora_rx_display_buf.snr);
    }
}

static void lora_send_thread_entry(void *parameter)
{
    while (device_switch.lora_tx_enable)
    {
        snprintf(lora_tx_display_buf, sizeof(lora_tx_display_buf), "T-Display-SF32 #%d", lora_tx_count++);
#ifdef PKG_USING_LORA_RADIO_DRIVER
        radio_tx((uint8_t *)lora_tx_display_buf, strlen(lora_tx_display_buf));
#endif
        lv_async_call(lora_tx_ui_update_cb, NULL);
        rt_thread_mdelay(1000);
    }
}

static void radio_rx_callback(lora_rx_info_t *info)
{
    if (info->data > 0)
    {
        rx_info = *info;
        log_d("rx message(%d bytes): %s, rssi: %d, snr: %d", info->data_len, info->data, info->rssi, info->snr);
        memcpy(&lora_rx_display_buf, &rx_info, sizeof(lora_rx_info_t));
        lv_async_call(lora_rx_ui_update_cb, NULL);
    }
    else
    {
        log_d("info message data len error");
    }
    radio_rx();
}
/**********sensor page **********/
static lv_obj_t *create_bar(lv_obj_t *parent, int32_t range, const char *title, lv_color_t color, const char *value, int x, int y);
static lv_obj_t *create_card(lv_obj_t *parent, const char *title, const char *value, int x, int y);

static void sensor_page_on_create(struct Page *page)
{
    bhi260ap_thread_resume();

    LOG_D("sensor_page_on_create\n");
    static lv_chart_series_t *ser_x, *ser_y, *ser_z;

    lv_obj_t *page_tile = lv_label_create(page->root);
    lv_obj_set_height(page_tile, 30);
    lv_obj_align(page_tile, LV_ALIGN_TOP_MID, 0, 10);
    lv_label_set_text(page_tile, "Sensor");
    lv_obj_set_style_text_font(page_tile, &lv_font_montserrat_36, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(page_tile, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    static lv_style_t config_cont_style;
    lv_style_init(&config_cont_style);
    lv_style_set_border_width(&config_cont_style, 0);
    lv_style_set_bg_color(&config_cont_style, lv_color_hex(LV_COLOR_THEME_WIHTE));
    lv_style_set_bg_opa(&config_cont_style, LV_OPA_0);
    lv_style_set_pad_all(&config_cont_style, 0);
    lv_style_set_pad_bottom(&config_cont_style, 40);
    lv_style_set_radius(&config_cont_style, 20);

    lv_obj_t *cont = lv_obj_create(page->root);
    lv_obj_add_style(cont, &config_cont_style, 0); // 使用已有样式
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(cont, 460, 400);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *temp_label = lv_label_create(cont);
    lv_obj_align(temp_label, LV_ALIGN_TOP_LEFT, 20, 15);
    lv_label_set_text(temp_label, "Temp");
    lv_obj_set_style_text_font(temp_label, &lv_font_montserrat_30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(temp_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(temp_label, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(temp_label, LV_OPA_80, LV_PART_MAIN | LV_STATE_DEFAULT);

    sensor_ui.sensor_temp_label = lv_label_create(cont);
    lv_obj_align(sensor_ui.sensor_temp_label, LV_ALIGN_TOP_LEFT, 10, 80);
    lv_label_set_text(sensor_ui.sensor_temp_label, "20");
    lv_obj_set_style_text_font(sensor_ui.sensor_temp_label, &lv_font_montserrat_48, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(sensor_ui.sensor_temp_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(sensor_ui.sensor_temp_label, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(sensor_ui.sensor_temp_label, LV_OPA_100, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *temp_symbol_label = lv_label_create(cont);
    lv_obj_align_to(temp_symbol_label, sensor_ui.sensor_temp_label, LV_ALIGN_OUT_RIGHT_BOTTOM, 5, -20);
    lv_label_set_text(temp_symbol_label, "°C");
    lv_obj_set_style_text_font(temp_symbol_label, &lv_font_montserrat_30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(temp_symbol_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(temp_symbol_label, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(temp_symbol_label, LV_OPA_100, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *bar_cont = create_bar(cont, 5000, "Press", lv_color_hex(LV_COLOR_WARM_RED), "0hPa", 120, 0);
    sensor_ui.sensor_press_bar = lv_obj_get_child(bar_cont, 1);
    sensor_ui.sensor_press_label = lv_obj_get_child(bar_cont, 2);
    bar_cont = create_bar(cont, 100, "Hum", lv_color_hex(LV_COLOR_BTN_BLUE), "0%", 120, 50);
    sensor_ui.sensor_hum_bar = lv_obj_get_child(bar_cont, 1);
    sensor_ui.sensor_hum_label = lv_obj_get_child(bar_cont, 2);
    bar_cont = create_bar(cont, 1000, "Step", lv_color_hex(LV_COLOR_THEME_YELLOW), "0", 120, 100);
    sensor_ui.sensor_step_bar = lv_obj_get_child(bar_cont, 1);
    sensor_ui.sensor_step_label = lv_obj_get_child(bar_cont, 2);

    lv_obj_t *screen_sensor_line_1 = lv_line_create(cont);
    lv_obj_set_pos(screen_sensor_line_1, 10, 150);
    lv_obj_set_size(screen_sensor_line_1, 440, 10);
    static lv_point_precise_t screen_imu_line_1[] = {{0, 0}, {440, 0}};
    lv_line_set_points(screen_sensor_line_1, screen_imu_line_1, 2);

    lv_obj_set_style_line_width(screen_sensor_line_1, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_color(screen_sensor_line_1, lv_color_hex(LV_COLOR_THEME_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_opa(screen_sensor_line_1, LV_OPA_80, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_rounded(screen_sensor_line_1, true, LV_PART_MAIN | LV_STATE_DEFAULT);

    sensor_ui.sensor_motor_btn = lv_btn_create(cont);
    lv_obj_set_size(sensor_ui.sensor_motor_btn, 80, 80);
    lv_obj_align(sensor_ui.sensor_motor_btn, LV_ALIGN_TOP_LEFT, 80, 230);
    lv_obj_set_style_radius(sensor_ui.sensor_motor_btn, 40, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(sensor_ui.sensor_motor_btn, lv_color_hex(LV_COLOR_WARM_RED), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(sensor_ui.sensor_motor_btn, LV_OPA_100, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(sensor_ui.sensor_motor_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(sensor_ui.sensor_motor_btn, sensor_event_cb, LV_EVENT_CLICKED, (void *)SENSOR_EVENT_MOTOR);
    lv_group_add_obj(page->group, sensor_ui.sensor_motor_btn);

    lv_obj_t *sensor_motor_label = lv_label_create(sensor_ui.sensor_motor_btn);
    lv_label_set_text(sensor_motor_label, "Motor");
    lv_obj_set_style_text_font(sensor_motor_label, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(sensor_motor_label);

    sensor_ui.sensor_pitch_arc = lv_arc_create(cont);
    lv_obj_align(sensor_ui.sensor_pitch_arc, LV_ALIGN_TOP_LEFT, 20, 170);
    lv_obj_clear_flag(sensor_ui.sensor_pitch_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(sensor_ui.sensor_pitch_arc, 200, 200);
    lv_arc_set_mode(sensor_ui.sensor_pitch_arc, LV_ARC_MODE_NORMAL);
    lv_arc_set_range(sensor_ui.sensor_pitch_arc, 0, 360);
    lv_arc_set_bg_angles(sensor_ui.sensor_pitch_arc, 0, 360);
    lv_arc_set_value(sensor_ui.sensor_pitch_arc, 0);
    lv_arc_set_rotation(sensor_ui.sensor_pitch_arc, 270);

    // Write style for screen_sensor_arc_1, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_bg_opa(sensor_ui.sensor_pitch_arc, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(sensor_ui.sensor_pitch_arc, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(sensor_ui.sensor_pitch_arc, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(sensor_ui.sensor_pitch_arc, LV_OPA_40, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(sensor_ui.sensor_pitch_arc, lv_color_hex(LV_COLOR_THEME_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(sensor_ui.sensor_pitch_arc, true, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(sensor_ui.sensor_pitch_arc, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(sensor_ui.sensor_pitch_arc, 40, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(sensor_ui.sensor_pitch_arc, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Write style for screen_sensor_arc_1, Part: LV_PART_INDICATOR, State: LV_STATE_DEFAULT.
    lv_obj_set_style_arc_width(sensor_ui.sensor_pitch_arc, 16, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(sensor_ui.sensor_pitch_arc, LV_OPA_100, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(sensor_ui.sensor_pitch_arc, lv_color_hex(LV_COLOR_THEME_GREEN), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(sensor_ui.sensor_pitch_arc, true, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    // Write style for screen_sensor_arc_1, Part: LV_PART_KNOB, State: LV_STATE_DEFAULT.
    lv_obj_set_style_bg_opa(sensor_ui.sensor_pitch_arc, 0, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(sensor_ui.sensor_pitch_arc, 0, LV_PART_KNOB | LV_STATE_DEFAULT);

    sensor_ui.sensor_yaw_arc = lv_arc_create(cont);
    lv_obj_align(sensor_ui.sensor_yaw_arc, LV_ALIGN_TOP_LEFT, 20, 170);
    lv_obj_set_size(sensor_ui.sensor_yaw_arc, 200, 200);
    lv_obj_clear_flag(sensor_ui.sensor_yaw_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_arc_set_mode(sensor_ui.sensor_yaw_arc, LV_ARC_MODE_NORMAL);
    lv_arc_set_range(sensor_ui.sensor_yaw_arc, 0, 360);
    lv_arc_set_bg_angles(sensor_ui.sensor_yaw_arc, 0, 360);
    lv_arc_set_value(sensor_ui.sensor_yaw_arc, 0);
    lv_arc_set_rotation(sensor_ui.sensor_yaw_arc, 270);

    // Write style for screen_sensor_arc_1, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_bg_opa(sensor_ui.sensor_yaw_arc, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(sensor_ui.sensor_yaw_arc, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(sensor_ui.sensor_yaw_arc, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(sensor_ui.sensor_yaw_arc, LV_OPA_40, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(sensor_ui.sensor_yaw_arc, lv_color_hex(LV_COLOR_THEME_YELLOW), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(sensor_ui.sensor_yaw_arc, true, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(sensor_ui.sensor_yaw_arc, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(sensor_ui.sensor_yaw_arc, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(sensor_ui.sensor_yaw_arc, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Write style for screen_sensor_arc_1, Part: LV_PART_INDICATOR, State: LV_STATE_DEFAULT.
    lv_obj_set_style_arc_width(sensor_ui.sensor_yaw_arc, 16, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(sensor_ui.sensor_yaw_arc, LV_OPA_100, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(sensor_ui.sensor_yaw_arc, lv_color_hex(LV_COLOR_THEME_YELLOW), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(sensor_ui.sensor_yaw_arc, true, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    // Write style for screen_sensor_arc_1, Part: LV_PART_KNOB, State: LV_STATE_DEFAULT.
    lv_obj_set_style_bg_opa(sensor_ui.sensor_yaw_arc, 0, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(sensor_ui.sensor_yaw_arc, 0, LV_PART_KNOB | LV_STATE_DEFAULT);

    sensor_ui.sensor_roll_arc = lv_arc_create(cont);
    lv_obj_align(sensor_ui.sensor_roll_arc, LV_ALIGN_TOP_LEFT, 20, 170);
    lv_obj_clear_flag(sensor_ui.sensor_roll_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(sensor_ui.sensor_roll_arc, 200, 200);
    lv_arc_set_mode(sensor_ui.sensor_roll_arc, LV_ARC_MODE_NORMAL);
    lv_arc_set_range(sensor_ui.sensor_roll_arc, 0, 360);
    lv_arc_set_bg_angles(sensor_ui.sensor_roll_arc, 0, 360);
    lv_arc_set_value(sensor_ui.sensor_roll_arc, 0);
    lv_arc_set_rotation(sensor_ui.sensor_roll_arc, 270);

    // Write style for screen_sensor_arc_1, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_bg_opa(sensor_ui.sensor_roll_arc, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(sensor_ui.sensor_roll_arc, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(sensor_ui.sensor_roll_arc, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(sensor_ui.sensor_roll_arc, LV_OPA_40, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(sensor_ui.sensor_roll_arc, lv_color_hex(LV_COLOR_BTN_BLUE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(sensor_ui.sensor_roll_arc, true, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(sensor_ui.sensor_roll_arc, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(sensor_ui.sensor_roll_arc, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(sensor_ui.sensor_roll_arc, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Write style for screen_sensor_arc_1, Part: LV_PART_INDICATOR, State: LV_STATE_DEFAULT.
    lv_obj_set_style_arc_width(sensor_ui.sensor_roll_arc, 16, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(sensor_ui.sensor_roll_arc, LV_OPA_100, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(sensor_ui.sensor_roll_arc, lv_color_hex(LV_COLOR_BTN_BLUE), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(sensor_ui.sensor_roll_arc, true, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    // Write style for screen_sensor_arc_1, Part: LV_PART_KNOB, State: LV_STATE_DEFAULT.
    lv_obj_set_style_bg_opa(sensor_ui.sensor_roll_arc, 0, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(sensor_ui.sensor_roll_arc, 0, LV_PART_KNOB | LV_STATE_DEFAULT);

    lv_obj_t *imu_cont = lv_obj_create(cont);
    lv_obj_set_size(imu_cont, 200, 120);
    lv_obj_set_pos(imu_cont, 240, 170);
    lv_obj_set_style_bg_color(imu_cont, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(imu_cont, LV_OPA_10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(imu_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(imu_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *card = create_card(imu_cont, "Pitch", "0.00", 10, 0);
    sensor_ui.sensor_pitch_label = lv_obj_get_child(card, 1);
    card = create_card(imu_cont, "Yaw", "0.00", 10, 40);
    sensor_ui.sensor_yaw_label = lv_obj_get_child(card, 1);
    card = create_card(imu_cont, "Roll", "0.00", 10, 80);
    sensor_ui.sensor_roll_label = lv_obj_get_child(card, 1);

    sensor_ui.sensor_ir_send_btn = lv_btn_create(cont);
    lv_obj_set_size(sensor_ui.sensor_ir_send_btn, 60, 60);
    lv_obj_align(sensor_ui.sensor_ir_send_btn, LV_ALIGN_TOP_LEFT, 240, 300);
    lv_obj_set_style_radius(sensor_ui.sensor_ir_send_btn, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(sensor_ui.sensor_ir_send_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(sensor_ui.sensor_ir_send_btn, lv_color_hex(LV_COLOR_BTN_BLUE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(sensor_ui.sensor_ir_send_btn, sensor_event_cb, LV_EVENT_CLICKED, (void *)SENSOR_EVENT_IR_SEND);
    lv_group_add_obj(page->group, sensor_ui.sensor_ir_send_btn);

    lv_obj_t *ir_send_label = lv_label_create(sensor_ui.sensor_ir_send_btn);
    lv_label_set_text(ir_send_label, "IR TX");
    lv_obj_set_style_text_font(ir_send_label, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(ir_send_label);

    sensor_ui.sensor_ir_send_msg_label = lv_label_create(cont);
    lv_obj_set_size(sensor_ui.sensor_ir_send_msg_label, 120, 60);
    lv_obj_align(sensor_ui.sensor_ir_send_msg_label, LV_ALIGN_TOP_LEFT, 310, 300);
    lv_label_set_text(sensor_ui.sensor_ir_send_msg_label, "msg\nnull");
    lv_obj_set_style_text_align(sensor_ui.sensor_ir_send_msg_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(sensor_ui.sensor_ir_send_msg_label, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(sensor_ui.sensor_ir_send_msg_label, LV_OPA_100, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(sensor_ui.sensor_ir_send_msg_label, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static lv_obj_t *create_card(lv_obj_t *parent, const char *title, const char *value, int x, int y)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, 180, 30);
    lv_obj_set_pos(cont, x, y);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_color(cont, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(cont, LV_OPA_0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *title_label = lv_label_create(cont);
    lv_obj_align(title_label, LV_ALIGN_LEFT_MID, 5, 0);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(title_label, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(title_label, LV_OPA_60, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *value_label = lv_label_create(cont);
    lv_obj_set_size(value_label, 120, 30);
    lv_obj_align(value_label, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_label_set_text(value_label, value);
    lv_obj_set_style_text_font(value_label, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(value_label, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(value_label, LV_OPA_100, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *line = lv_line_create(cont);
    lv_obj_set_size(line, 180, 4);
    lv_obj_align(line, LV_ALIGN_BOTTOM_MID, 0, -2);
    static lv_point_precise_t screen_imu_line_1[] = {{0, 0}, {180, 0}};
    lv_line_set_points(line, screen_imu_line_1, 2);

    lv_obj_set_style_line_width(line, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_color(line, lv_color_hex(LV_COLOR_THEME_GRAY), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_opa(line, LV_OPA_80, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_rounded(line, true, LV_PART_MAIN | LV_STATE_DEFAULT);
    return cont;
}

static lv_obj_t *create_bar(lv_obj_t *parent, int32_t range, const char *title, lv_color_t color, const char *value, int x, int y)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, 340, 50);
    lv_obj_set_pos(cont, x, y);
    lv_obj_set_style_bg_color(cont, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(cont, LV_OPA_0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *title_label = lv_label_create(cont);
    lv_obj_align(title_label, LV_ALIGN_LEFT_MID, 5, 0);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(title_label, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(title_label, LV_OPA_60, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *bar = lv_bar_create(cont);
    lv_obj_align(bar, LV_ALIGN_LEFT_MID, 60, 0);
    lv_obj_set_size(bar, 200, 10);
    lv_bar_set_mode(bar, LV_BAR_MODE_NORMAL);
    lv_bar_set_range(bar, 0, range);
    lv_bar_set_value(bar, range / 2, LV_ANIM_OFF);

    lv_obj_set_style_bg_color(bar, color, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_20, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_style_bg_color(bar, color, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bar, 0, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(bar, LV_OPA_100, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    lv_obj_t *value_label = lv_label_create(cont);
    lv_obj_set_size(value_label, 70, 20);
    lv_obj_align(value_label, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_label_set_text(value_label, value);
    lv_obj_set_style_text_font(value_label, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(value_label, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(value_label, LV_OPA_60, LV_PART_MAIN | LV_STATE_DEFAULT);
    return cont;
}

static void sensor_event_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);
    int event_id = (int)lv_event_get_user_data(e);

    if (code == LV_EVENT_CLICKED)
    {
        switch (event_id)
        {
        case SENSOR_EVENT_MOTOR:
#ifdef PKG_USING_PKG_KEY_BOARD
            aw86224_play_ram_short(1, RT_TRUE);
#endif
            break;
        case SENSOR_EVENT_IR_SEND:
            device_switch.ir_enable = !device_switch.ir_enable;
            if (device_switch.ir_enable)
            {
                lv_obj_set_style_bg_color(sensor_ui.sensor_ir_send_btn, lv_color_hex(LV_COLOR_THEME_YELLOW), LV_PART_MAIN | LV_STATE_DEFAULT);
            }
            else
            {
                lv_obj_set_style_bg_color(sensor_ui.sensor_ir_send_btn, lv_color_hex(LV_COLOR_BTN_BLUE), LV_PART_MAIN | LV_STATE_DEFAULT);
            }
            break;
        }
    }
}

static void sensor_timer_callback(lv_timer_t *timer)
{
    static int bme_update_time = 0;
    static int ir_tx_time = 0;
    bme_update_time++;
    ir_tx_time++;

    static int ir_tx_count = 0;
    float temp, hum, press;
    struct bhy2_data_orientation_float orient = bhi260ap_get_orient_sensor_data();
    uint32_t step = bhi260ap_get_step_counter_sensor_data();

    orient.heading = orient.heading + 180;
    orient.pitch = orient.pitch + 90;
    orient.roll = orient.roll + 180;

    lv_arc_set_value(sensor_ui.sensor_yaw_arc, (int32_t)orient.heading);
    lv_arc_set_value(sensor_ui.sensor_pitch_arc, (int32_t)orient.pitch);
    lv_arc_set_value(sensor_ui.sensor_roll_arc, (int32_t)orient.roll);

    lv_bar_set_value(sensor_ui.sensor_step_bar, (int32_t)step, LV_ANIM_ON);
    lv_label_set_text_fmt(sensor_ui.sensor_step_label, "%d", step);

    lv_label_set_text_fmt(sensor_ui.sensor_yaw_label, "%.2f", orient.heading);
    lv_label_set_text_fmt(sensor_ui.sensor_pitch_label, "%.2f", orient.pitch);
    lv_label_set_text_fmt(sensor_ui.sensor_roll_label, "%.2f", orient.roll);

#ifdef PKG_USING_BME280
    if (bme_update_time >= 20)
    {
        bme_update_time = 0;
        if (bme280_read_all(bme280_get_device(), &temp, &press, &hum) == RT_EOK)
        {
            log_d("Temperature: %.2f °C\n", temp);
            log_d("Pressure   : %.2f Pa (%.2f hPa)\n", press, press / 100.0f);
            log_d("Humidity   : %.2f %%\n", hum);

            lv_label_set_text_fmt(sensor_ui.sensor_temp_label, "%d", (int32_t)temp);
            lv_label_set_text_fmt(sensor_ui.sensor_press_label, "%dhPa", (int32_t)press / 100);
            lv_label_set_text_fmt(sensor_ui.sensor_hum_label, "%d%", (int32_t)hum);

            lv_bar_set_value(sensor_ui.sensor_press_bar, (int32_t)press / 100, LV_ANIM_ON);
            lv_bar_set_value(sensor_ui.sensor_hum_bar, (int32_t)hum, LV_ANIM_ON);
        }
    }
#endif

#ifdef PKG_USING_INFRARED
    if (ir_tx_time >= 10)
    {
        ir_tx_time = 0;
        if (device_switch.ir_enable)
        {
            ir_tx_count++;
            ir_nec_send(0x67, ir_tx_count);
            lv_label_set_text_fmt(sensor_ui.sensor_ir_send_msg_label, "IR Count\n%d", ir_tx_count);
        }
    }
#endif
}

/**********gps page **********/
static lv_obj_t *gps_create_box(lv_obj_t *parent, const char *title, int x, int y);

static void gps_page_on_create(struct Page *page)
{
    LOG_D("gps_page_on_create\n");
    lv_obj_t *page_tile = lv_label_create(page->root);
    lv_obj_set_height(page_tile, 30);
    lv_obj_align(page_tile, LV_ALIGN_TOP_MID, 0, 10);
    lv_label_set_text(page_tile, "GPS");
    lv_obj_set_style_text_font(page_tile, &lv_font_montserrat_36, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(page_tile, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    static lv_style_t config_cont_style;
    lv_style_init(&config_cont_style);
    lv_style_set_border_width(&config_cont_style, 0);
    lv_style_set_bg_color(&config_cont_style, lv_color_hex(LV_COLOR_THEME_WIHTE));
    lv_style_set_bg_opa(&config_cont_style, LV_OPA_0);
    lv_style_set_pad_all(&config_cont_style, 0);
    lv_style_set_radius(&config_cont_style, 20);

    lv_obj_t *cont = lv_obj_create(page->root);
    lv_obj_add_style(cont, &config_cont_style, 0); // 使用已有样式
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(cont, 460, 400);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);

    gps_ui.gps_data = lv_label_create(cont);
    lv_obj_set_size(gps_ui.gps_data, 300, 30);
    lv_obj_align(gps_ui.gps_data, LV_ALIGN_TOP_MID, 0, 0);
    lv_label_set_text(gps_ui.gps_data, "1970-00-00");
    lv_obj_set_style_text_font(gps_ui.gps_data, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(gps_ui.gps_data, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(gps_ui.gps_data, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(gps_ui.gps_data, LV_OPA_70, LV_PART_MAIN | LV_STATE_DEFAULT);

    gps_ui.gps_time = lv_label_create(cont);
    lv_obj_set_size(gps_ui.gps_time, 200, 30);
    lv_obj_align(gps_ui.gps_time, LV_ALIGN_TOP_MID, 0, 35);
    lv_label_set_text(gps_ui.gps_time, "00:00:00");
    lv_obj_set_style_text_font(gps_ui.gps_time, &lv_font_montserrat_36, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(gps_ui.gps_time, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(gps_ui.gps_time, lv_color_hex(LV_COLOR_THEME_YELLOW), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(gps_ui.gps_time, LV_OPA_100, LV_PART_MAIN | LV_STATE_DEFAULT);

    static lv_point_precise_t line_point[] = {{0, 0}, {440, 0}};

    lv_obj_t *screen_gps_line_1 = lv_line_create(cont);
    lv_obj_set_pos(screen_gps_line_1, 10, 80);
    lv_obj_set_size(screen_gps_line_1, 440, 10);
    lv_line_set_points(screen_gps_line_1, line_point, 2);

    lv_obj_set_style_line_width(screen_gps_line_1, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_color(screen_gps_line_1, lv_color_hex(LV_COLOR_THEME_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_opa(screen_gps_line_1, LV_OPA_80, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_rounded(screen_gps_line_1, true, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *box = gps_create_box(cont, "LAT", 60, 100);
    gps_ui.gps_lat = lv_obj_get_child(box, 1);
    box = gps_create_box(cont, "LON", 280, 100);
    gps_ui.gps_lon = lv_obj_get_child(box, 1);
    box = gps_create_box(cont, "SPD", 60, 200);
    gps_ui.gps_spd = lv_obj_get_child(box, 1);
    box = gps_create_box(cont, "CRS", 280, 200);
    gps_ui.gps_crs = lv_obj_get_child(box, 1);

    lv_obj_t *screen_gps_line_2 = lv_line_create(cont);
    lv_obj_set_pos(screen_gps_line_2, 10, 290);
    lv_obj_set_size(screen_gps_line_2, 440, 10);
    lv_line_set_points(screen_gps_line_2, line_point, 2);

    lv_obj_set_style_line_width(screen_gps_line_2, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_color(screen_gps_line_2, lv_color_hex(LV_COLOR_THEME_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_opa(screen_gps_line_2, LV_OPA_80, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_line_rounded(screen_gps_line_2, true, LV_PART_MAIN | LV_STATE_DEFAULT);

    box = gps_create_box(cont, "ALT", 20, 300);
    gps_ui.gps_alt = lv_obj_get_child(box, 1);
    box = gps_create_box(cont, "SATS", 170, 300);
    gps_ui.gps_sats = lv_obj_get_child(box, 1);
    box = gps_create_box(cont, "DOP", 320, 300);
    gps_ui.gps_dop = lv_obj_get_child(box, 1);
}

static lv_obj_t *gps_create_box(lv_obj_t *parent, const char *title, int x, int y)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_set_size(box, 120, 80);
    lv_obj_align(box, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_set_style_bg_color(box, lv_color_hex(LV_COLOR_THEME_WIHTE), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_20, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_radius(box, 20, 0);
    lv_obj_set_style_pad_all(box, 0, 0);

    lv_obj_t *label = lv_label_create(box);
    lv_label_set_text(label, title);
    lv_obj_set_size(label, 110, 20);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(LV_COLOR_THEME_WIHTE), 0);
    lv_obj_set_style_text_opa(label, LV_OPA_70, 0);

    lv_obj_t *value = lv_label_create(box);
    lv_label_set_text(value, "NULL");
    lv_obj_set_size(value, 110, 20);
    lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(value, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_text_font(value, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(value, lv_color_hex(LV_COLOR_THEME_WIHTE), 0);
    lv_obj_set_style_text_opa(value, LV_OPA_100, 0);

    return box;
}

static void gps_timer_callback(lv_timer_t *timer)
{
#if defined(PKG_USING_L76K)
    GPSInfo gps_info;
    if (l76k_is_fixed())
    {
        device_switch.gps_enable = true;
        l76k_get_gps_info(&gps_info);
        log_d("Time: %04d-%02d-%02d %02d:%02d:%02d UTC\n", gps_info.time.year, gps_info.time.month, gps_info.time.day,
              gps_info.time.hour, gps_info.time.minute, gps_info.time.second);
        log_d("Latitude: %f, Longitude: %f\n", gps_info.latitude, gps_info.longitude);
        log_d("Altitude: %.2f m\n", gps_info.altitude);
        log_d("Speed: %.2f km/h\n", gps_info.speed);
        log_d("Satellite count: %d\n", gps_info.satellites);
        log_d("HDOP: %.2f\n", gps_info.hdop);
        log_d("Course: %.2f\n", gps_info.course);

        lv_label_set_text_fmt(gps_ui.gps_data, "%04d-%02d-%02d", gps_info.time.year, gps_info.time.month, gps_info.time.day);
        lv_label_set_text_fmt(gps_ui.gps_time, "%02d:%02d:%02d", gps_info.time.hour, gps_info.time.minute, gps_info.time.second);
        lv_label_set_text_fmt(gps_ui.gps_lat, "%.6f", gps_info.latitude);
        lv_label_set_text_fmt(gps_ui.gps_lon, "%.6f", gps_info.longitude);
        lv_label_set_text_fmt(gps_ui.gps_spd, "%.2f km/h", gps_info.speed);
        lv_label_set_text_fmt(gps_ui.gps_crs, "%.2f", gps_info.course);
        lv_label_set_text_fmt(gps_ui.gps_alt, "%.2f m", gps_info.altitude);
        lv_label_set_text_fmt(gps_ui.gps_sats, "%d", gps_info.satellites);
        lv_label_set_text_fmt(gps_ui.gps_dop, "%.2f", gps_info.hdop);
    }
    else
    {
        device_switch.gps_enable = false;
        log_d("GPS is not fixed\n");
    }
#endif
}

/**********wifi page **********/
static void wifi_page_on_create(struct Page *page)
{
    LOG_D("wifi_page_on_create\n");
    lv_obj_t *page_tile = lv_label_create(page->root);
    lv_obj_set_height(page_tile, 40);
    lv_obj_align(page_tile, LV_ALIGN_TOP_MID, 0, 10);
    lv_label_set_text(page_tile, "WIFI");
    lv_obj_set_style_text_font(page_tile, &lv_font_montserrat_36, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(page_tile, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_scrollbar_mode(page->root, LV_SCROLLBAR_MODE_OFF);

    wifi_ui.status_label = lv_label_create(page->root);
    lv_obj_set_size(wifi_ui.status_label, 440, 24);
    lv_obj_align(wifi_ui.status_label, LV_ALIGN_TOP_MID, 0, 60);
    lv_label_set_text(wifi_ui.status_label, "Status: Not Connected");
    lv_obj_set_style_text_color(wifi_ui.status_label, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(wifi_ui.status_label, LV_OPA_70, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(wifi_ui.status_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(wifi_ui.status_label, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);

    wifi_ui.resp_ta = lv_textarea_create(page->root);
    lv_obj_set_size(wifi_ui.resp_ta, 440, 260);
    lv_obj_align(wifi_ui.resp_ta, LV_ALIGN_TOP_MID, 0, 100);
    lv_textarea_set_text(wifi_ui.resp_ta, "Response:\n");
    lv_obj_set_style_text_font(wifi_ui.resp_ta, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(wifi_ui.resp_ta, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(wifi_ui.resp_ta, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(wifi_ui.resp_ta, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_clear_flag(wifi_ui.resp_ta, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *cont = lv_obj_create(page->root);
    lv_obj_set_size(cont, 480, 100);
    lv_obj_align(cont, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_style_bg_opa(cont, LV_OPA_0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(cont, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(cont, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    const char *btn_texts[] = {"AT+CWSTATE?", "AT+CWJAP",
                               "AT+CWQAP", "AT+CIPSNTPTIME?",
                               "AT+GMR", "AT+CIFSR",
                               "AT+RST", "AT+GSLP=10S"};
    const wifi_event_t btn_events[] = {ESP32C6_EVENT_SEND_CWSTATE, ESP32C6_EVENT_SEND_CWJAP,
                                       ESP32C6_EVENT_SEND_CWQAP, ESP32C6_EVENT_SEND_SNTPTIME,
                                       ESP32C6_EVENT_SEND_GMR, ESP32C6_EVENT_SEND_CIFSR,
                                       ESP32C6_EVENT_SEND_RST, ESP32C6_EVENT_SEND_GSLP};
    for (int i = 0; i < sizeof(btn_texts) / sizeof(btn_texts[0]); i++)
    {
        lv_obj_t *btn = lv_btn_create(cont);
        lv_obj_set_size(btn, 200, 40);
        lv_obj_align(btn, LV_ALIGN_TOP_LEFT, (i % 2) * 240, (i / 2) * 50);
        lv_obj_set_style_bg_color(btn, lv_color_hex(LV_COLOR_THEME_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(btn, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_t *label = lv_label_create(btn);
        lv_label_set_text(label, btn_texts[i]);
        lv_obj_center(label);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(label, lv_color_hex(LV_COLOR_THEME_BLACK), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_add_event_cb(btn, wifi_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)btn_events[i]);
        lv_group_add_obj(page->group, btn);
    }
}

static void wifi_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    wifi_event_t event_id = (wifi_event_t)lv_event_get_user_data(e);
    if (code == LV_EVENT_CLICKED)
    {
#if defined(PKG_USING_ESP32C6_AT_COMMAND)
        switch (event_id)
        {
        case ESP32C6_EVENT_SEND_RST:
            esp32_at_rst(AT_RESP_ID_RST);
            lv_textarea_add_text(wifi_ui.resp_ta, "> AT+RST\n");
            break;
        case ESP32C6_EVENT_SEND_SNTPTIME:
            esp32_at_sntp_time_q(AT_RESP_ID_SNTPTIME_Q);
            lv_textarea_add_text(wifi_ui.resp_ta, "> AT+CIPSNTPTIME?\n");
            break;
        case ESP32C6_EVENT_SEND_GMR:
            esp32_at_gmr(AT_RESP_ID_GMR);
            lv_textarea_add_text(wifi_ui.resp_ta, "> AT+GMR\n");
            break;
        case ESP32C6_EVENT_SEND_CIFSR:
            esp32_at_cifsr(AT_RESP_ID_CIFSR);
            lv_textarea_add_text(wifi_ui.resp_ta, "> AT+CIFSR\n");
            break;
        case ESP32C6_EVENT_SEND_CWJAP:
            esp32_at_cwjap(AT_RESP_ID_CWJAP, "xinyuandianzi", "AA15994823428");
            lv_textarea_add_text(wifi_ui.resp_ta, "> AT+CWJAP\n");
            break;
        case ESP32C6_EVENT_SEND_CWQAP:
            esp32_at_cwqap(AT_RESP_ID_CWQAP);
            lv_textarea_add_text(wifi_ui.resp_ta, "> AT+CWQAP?\n");
            break;
        case ESP32C6_EVENT_SEND_CWSTATE:
            esp32_at_cwstate(AT_RESP_ID_CWSTATE);
            lv_textarea_add_text(wifi_ui.resp_ta, "> AT+CWSTATE?\n");
            break;
        case ESP32C6_EVENT_SEND_GSLP:
            esp32_at_deep_sleep(AT_RESP_ID_DEEP_SLEEP, 10000);
            lv_textarea_add_text(wifi_ui.resp_ta, "> AT+GSLP=10000\n");
            break;
        }
#endif
    }
}

void wifi_update_wifi_status(void *data)
{
    if (data == RT_NULL)
        return;
    bool connected = *(bool *)data;
    if (wifi_ui.status_label != RT_NULL && current_page->id == PAGE_WIFI)
    {
        if (connected)
        {
            lv_label_set_text(wifi_ui.status_label, "Connected");
            lv_obj_set_style_text_color(wifi_ui.status_label, lv_color_hex(LV_COLOR_THEME_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        else
        {
            lv_label_set_text(wifi_ui.status_label, "Disconnected");
            lv_obj_set_style_text_color(wifi_ui.status_label, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
}

void wifi_update_resp_cb(void *data)
{
    if (data != RT_NULL && wifi_ui.resp_ta != RT_NULL && current_page->id == PAGE_WIFI)
    {
        const char *text = lv_textarea_get_text(wifi_ui.resp_ta);
        uint32_t text_len = strlen(text);

        if (text_len > 1000)
        {
            lv_textarea_set_text(wifi_ui.resp_ta, "[Response:]\n");
        }

        lv_obj_set_style_text_color(wifi_ui.resp_ta, lv_color_hex(LV_COLOR_THEME_YELLOW), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_textarea_add_text(wifi_ui.resp_ta, "[Response:]");
        lv_obj_set_style_text_color(wifi_ui.resp_ta, lv_color_hex(LV_COLOR_THEME_BLACK), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_textarea_add_text(wifi_ui.resp_ta, (const char *)data);
        lv_textarea_add_char(wifi_ui.resp_ta, '\n');
        lv_obj_scroll_to_view(wifi_ui.resp_ta, LV_ANIM_ON);
    }
}

/**********charge page **********/
static void charge_page_on_create(struct Page *page)
{
    LOG_D("charge_page_on_create\n");

    lv_obj_t *page_tile = lv_label_create(page->root);
    lv_obj_set_height(page_tile, 30);
    lv_obj_align(page_tile, LV_ALIGN_TOP_MID, 0, 10);
    lv_label_set_text(page_tile, "Charge Manger");
    lv_obj_set_style_text_font(page_tile, &lv_font_montserrat_36, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(page_tile, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    char *status_text = NULL;

    voltage = get_voltage();
    log_d("voltage: %d\n", voltage);

    voltage_percent = get_charge_percent();
    log_d("voltage percent: %d\n", voltage_percent);

    charge_ui.charge_arc = lv_arc_create(page->root);
    lv_obj_align(charge_ui.charge_arc, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_clear_flag(charge_ui.charge_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(charge_ui.charge_arc, 250, 250);
    lv_arc_set_mode(charge_ui.charge_arc, LV_ARC_MODE_NORMAL);
    lv_arc_set_range(charge_ui.charge_arc, 0, 100);
    lv_arc_set_bg_angles(charge_ui.charge_arc, 135, 45);
    lv_arc_set_value(charge_ui.charge_arc, voltage_percent);
    lv_arc_set_rotation(charge_ui.charge_arc, 0);

    // Write style for screen_charge_arc_1, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_bg_opa(charge_ui.charge_arc, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(charge_ui.charge_arc, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_width(charge_ui.charge_arc, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(charge_ui.charge_arc, LV_OPA_30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(charge_ui.charge_arc, lv_color_hex(LV_COLOR_THEME_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(charge_ui.charge_arc, true, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(charge_ui.charge_arc, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(charge_ui.charge_arc, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(charge_ui.charge_arc, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(charge_ui.charge_arc, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(charge_ui.charge_arc, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(charge_ui.charge_arc, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Write style for screen_charge_arc_1, Part: LV_PART_INDICATOR, State: LV_STATE_DEFAULT.
    lv_obj_set_style_arc_width(charge_ui.charge_arc, 12, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_opa(charge_ui.charge_arc, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_color(charge_ui.charge_arc, lv_color_hex(LV_COLOR_THEME_GREEN), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_arc_rounded(charge_ui.charge_arc, true, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    // Write style for screen_charge_arc_1, Part: LV_PART_KNOB, State: LV_STATE_DEFAULT.
    lv_obj_set_style_bg_opa(charge_ui.charge_arc, 0, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(charge_ui.charge_arc, 0, LV_PART_KNOB | LV_STATE_DEFAULT);

    charge_ui.batter_voltage_percentage_label = lv_label_create(page->root);
    lv_obj_set_height(charge_ui.batter_voltage_percentage_label, 32);
    lv_obj_align(charge_ui.batter_voltage_percentage_label, LV_ALIGN_TOP_MID, 0, 120);
    lv_label_set_text_fmt(charge_ui.batter_voltage_percentage_label, "%d%%", voltage_percent);
    lv_obj_set_style_text_color(charge_ui.batter_voltage_percentage_label, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(charge_ui.batter_voltage_percentage_label, &lv_font_montserrat_30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(charge_ui.batter_voltage_percentage_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(charge_ui.batter_voltage_percentage_label, LV_OPA_100, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_long_mode(charge_ui.batter_voltage_percentage_label, LV_LABEL_LONG_WRAP);

    charge_ui.charge_status_label = lv_label_create(page->root);
    lv_obj_set_height(charge_ui.charge_status_label, 25);
    lv_obj_align(charge_ui.charge_status_label, LV_ALIGN_TOP_MID, 0, 160);
    lv_label_set_text(charge_ui.charge_status_label, status_text);
    lv_obj_set_style_text_color(charge_ui.charge_status_label, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(charge_ui.charge_status_label, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(charge_ui.charge_status_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(charge_ui.charge_status_label, LV_OPA_70, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_long_mode(charge_ui.charge_status_label, LV_LABEL_LONG_WRAP);

    charge_ui.batter_voltage_mv_label = lv_label_create(page->root);
    lv_obj_set_height(charge_ui.batter_voltage_mv_label, 25);
    lv_obj_align(charge_ui.batter_voltage_mv_label, LV_ALIGN_TOP_MID, 0, 190);
    lv_label_set_text_fmt(charge_ui.batter_voltage_mv_label, "%d mV", voltage);
    lv_obj_set_style_text_color(charge_ui.batter_voltage_mv_label, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(charge_ui.batter_voltage_mv_label, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(charge_ui.batter_voltage_mv_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(charge_ui.batter_voltage_mv_label, LV_OPA_70, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_long_mode(charge_ui.batter_voltage_mv_label, LV_LABEL_LONG_WRAP);

    sgm41562b_handle_t charger;
    charger = sgm41562b_get_handle();

    charge_ui.batter_current_ma_btn = lv_button_create(page->root);
    lv_obj_set_size(charge_ui.batter_current_ma_btn, 100, 30);
    lv_obj_align(charge_ui.batter_current_ma_btn, LV_ALIGN_TOP_MID, 0, 220);
    lv_obj_set_style_bg_color(charge_ui.batter_current_ma_btn, lv_color_hex(LV_COLOR_THEME_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(charge_ui.batter_current_ma_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(charge_ui.batter_current_ma_btn, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(charge_ui.batter_current_ma_btn, charge_event_cb, LV_EVENT_CLICKED, (void *)CHARGE_EVENT_CURRENT);
    lv_group_add_obj(page->group, charge_ui.batter_current_ma_btn);
    lv_obj_t *current_label = lv_label_create(charge_ui.batter_current_ma_btn);
    lv_label_set_text_fmt(current_label, "%d mA", sgm41562b_get_charge_current(charger));
    lv_obj_set_style_text_color(current_label, lv_color_hex(LV_COLOR_THEME_BLACK), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(current_label, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(current_label);

    lv_obj_t *charge_cont = lv_obj_create(page->root);
    lv_obj_set_size(charge_cont, 440, 200);
    lv_obj_align(charge_cont, LV_ALIGN_TOP_MID, 0, 260);
    lv_obj_set_style_bg_color(charge_cont, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(charge_cont, 30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(charge_cont, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(charge_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(charge_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(charge_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_group_add_obj(page->group, charge_cont);

    char *charge_status_text[] = {
        "Charge Voltage:",
        "Charge Current:",
        "Input Power:",
        "Thermal Regulation:",
        "VIN Fault:",
        "Battery Fault:",
        "NTC Status:"};

    for (int i = 0; i < sizeof(charge_status_text) / sizeof(charge_status_text[0]); i++)
    {
        lv_obj_t *charge_status_text_label = lv_label_create(charge_cont);
        lv_obj_align(charge_status_text_label, LV_ALIGN_TOP_LEFT, 10, 20 + i * 40);
        lv_obj_set_height(charge_status_text_label, 25);
        lv_label_set_text(charge_status_text_label, charge_status_text[i]);
        lv_obj_set_style_text_color(charge_status_text_label, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(charge_status_text_label, &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_letter_space(charge_status_text_label, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_opa(charge_status_text_label, LV_OPA_70, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(charge_status_text_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_long_mode(charge_status_text_label, LV_LABEL_LONG_WRAP);

        charge_ui.batter_status_label[i] = lv_label_create(charge_cont);
        lv_obj_align(charge_ui.batter_status_label[i], LV_ALIGN_TOP_RIGHT, -30, 20 + i * 40);
        lv_obj_set_height(charge_ui.batter_status_label[i], 25);
        lv_label_set_text(charge_ui.batter_status_label[i], "Not Ready");
        lv_obj_set_style_text_color(charge_ui.batter_status_label[i], lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(charge_ui.batter_status_label[i], &lv_font_montserrat_16, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_letter_space(charge_ui.batter_status_label[i], 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_opa(charge_ui.batter_status_label[i], LV_OPA_100, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(charge_ui.batter_status_label[i], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_long_mode(charge_ui.batter_status_label[i], LV_LABEL_LONG_WRAP);
        lv_obj_add_flag(charge_ui.batter_status_label[i], LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    }
}

static void charge_timer_callback(lv_timer_t *timer)
{
    sgm41562b_handle_t charger;
    charger = sgm41562b_get_handle();
    voltage = get_voltage();
    voltage_percent = get_charge_percent();

    lv_arc_set_value(charge_ui.charge_arc, voltage_percent);
    lv_label_set_text_fmt(charge_ui.batter_voltage_percentage_label, "%d%%", voltage_percent);
    lv_label_set_text_fmt(charge_ui.batter_voltage_mv_label, "%d mV", voltage);

    lv_label_set_text(charge_ui.charge_status_label, sgm41562b_get_charge_status_str(charger));
    lv_label_set_text_fmt(charge_ui.batter_status_label[0], "%d mV", sgm41562b_get_charge_vlotage(charger));
    lv_label_set_text_fmt(charge_ui.batter_status_label[1], "%d mA", sgm41562b_get_charge_current(charger));
    lv_label_set_text(charge_ui.batter_status_label[2], sgm41562b_get_power_good_str(charger));
    lv_label_set_text(charge_ui.batter_status_label[3], sgm41562b_get_thermal_regulation_str(charger));
    lv_label_set_text(charge_ui.batter_status_label[4], sgm41562b_get_vin_fault_str(charger));
    lv_label_set_text(charge_ui.batter_status_label[5], sgm41562b_get_battery_fault_str(charger));
    lv_label_set_text(charge_ui.batter_status_label[6], sgm41562b_get_ntc_fault_str(charger));
}

static void charge_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *target_obj = lv_event_get_target(e);
    int event_id = (int)lv_event_get_user_data(e);
    sgm41562b_handle_t charge_handle = sgm41562b_get_handle();
    log_d("charge_event_cb, code: %d, event_id: %d\n", code, event_id);
    static bool current_ma = false;
    switch (code)
    {
    case LV_EVENT_CLICKED:
        switch (event_id)
        {
        case CHARGE_EVENT_CURRENT:
            current_ma = !current_ma;
            lv_obj_t *current_label = lv_obj_get_child(target_obj, 0);
            if (current_ma)
            {
                lv_label_set_text(current_label, "400 mA");
                sgm41562b_set_charge_current(charge_handle, 400); /* 400mA */
            }
            else
            {
                lv_label_set_text(current_label, "200 mA");
                sgm41562b_set_charge_current(charge_handle, 200); /* 200mA */
            }
            break;
        }

        break;
    }
}

/**********weather page **********/
static void weather_page_on_create(struct Page *page)
{
    LOG_D("set_page_on_create\n");
    user_seniverse_config_t weather_info;
    int weather_code = 0;
    if (device_switch.bluetooth_pan_enable)
    {
        char *weather = get_weather();
        if (weather != NULL)
        {
            LOG_D("updata weather");
            http_weather_data_parse(weather, &weather_info);
            weather_code = atoi(weather_info.now_config.code);

            rt_free(weather);
        }
    }

    lv_obj_t *page_tile = lv_label_create(page->root);
    lv_obj_set_height(page_tile, 30);
    lv_obj_align(page_tile, LV_ALIGN_TOP_MID, 0, 10);
    lv_label_set_text(page_tile, "Weather");
    lv_obj_set_style_text_font(page_tile, &lv_font_montserrat_36, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(page_tile, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    weather_ui.weather_icon = lv_img_create(page->root);
    lv_obj_set_size(weather_ui.weather_icon, 120, 120);
    lv_obj_align(weather_ui.weather_icon, LV_ALIGN_TOP_MID, 0, 60);
    if (device_switch.bluetooth_pan_enable)
    {
        if (weather_code >= 0 && weather_code <= 3)
        {
            lv_img_set_src(weather_ui.weather_icon, &sun96_icon);
        }
        else if (weather_code >= 4 && weather_code <= 9)
        {
            lv_img_set_src(weather_ui.weather_icon, &cloudyday96_icon);
        }
        else if (weather_code >= 10 && weather_code <= 11)
        {
            lv_img_set_src(weather_ui.weather_icon, &cloud96_icon);
        }
        else if (weather_code >= 12 && weather_code <= 18)
        {
            lv_img_set_src(weather_ui.weather_icon, &rain96_icon);
        }
    }
    else
        lv_img_set_src(weather_ui.weather_icon, &sun96_icon);

    weather_ui.weather_temp_label = lv_label_create(page->root);
    lv_obj_set_height(weather_ui.weather_temp_label, 60);
    lv_obj_set_width(weather_ui.weather_temp_label, 240);
    lv_obj_align(weather_ui.weather_temp_label, LV_ALIGN_TOP_MID, 0, 200);
    lv_obj_set_style_text_font(weather_ui.weather_temp_label, &lv_font_montserrat_48, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(weather_ui.weather_temp_label, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(weather_ui.weather_temp_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_long_mode(weather_ui.weather_temp_label, LV_LABEL_LONG_SCROLL);
    if (device_switch.bluetooth_pan_enable)
        lv_label_set_text_fmt(weather_ui.weather_temp_label, "%s°C", weather_info.now_config.temperature);
    else
        lv_label_set_text(weather_ui.weather_temp_label, "Plase open to Pan of Bluetooth");

    lv_obj_t *weather_city_name_label = lv_label_create(page->root);
    lv_obj_set_height(weather_city_name_label, 30);
    lv_obj_align(weather_city_name_label, LV_ALIGN_TOP_LEFT, 20, 280);
    lv_obj_set_style_text_font(weather_city_name_label, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(weather_city_name_label, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(weather_city_name_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(weather_city_name_label, "City:");
    lv_label_set_long_mode(weather_city_name_label, LV_LABEL_LONG_WRAP);

    weather_ui.weather_city_label = lv_label_create(page->root);
    lv_obj_set_height(weather_ui.weather_city_label, 30);
    lv_obj_align(weather_ui.weather_city_label, LV_ALIGN_TOP_LEFT, 220, 280);
    lv_obj_set_style_text_font(weather_ui.weather_city_label, &lv_font_simyou_30_ch, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(weather_ui.weather_city_label, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(weather_ui.weather_city_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    if (device_switch.bluetooth_pan_enable)
        lv_label_set_text_fmt(weather_ui.weather_city_label, "%s,%s", weather_info.name, weather_info.country);
    else
        lv_label_set_text(weather_ui.weather_city_label, "");

    lv_label_set_long_mode(weather_ui.weather_city_label, LV_LABEL_LONG_WRAP);

    lv_obj_t *weather_update_label = lv_label_create(page->root);
    lv_obj_set_height(weather_update_label, 30);
    lv_obj_align(weather_update_label, LV_ALIGN_TOP_LEFT, 20, 340);
    lv_obj_set_style_text_font(weather_update_label, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(weather_update_label, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(weather_update_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(weather_update_label, "Update Time:");
    lv_label_set_long_mode(weather_update_label, LV_LABEL_LONG_WRAP);

    weather_ui.last_update_label = lv_label_create(page->root);
    lv_obj_set_height(weather_ui.last_update_label, 30);
    lv_obj_set_width(weather_ui.last_update_label, 260);
    lv_obj_align(weather_ui.last_update_label, LV_ALIGN_TOP_LEFT, 220, 340);
    lv_obj_set_style_text_font(weather_ui.last_update_label, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(weather_ui.last_update_label, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(weather_ui.last_update_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    if (device_switch.bluetooth_pan_enable)
        lv_label_set_text_fmt(weather_ui.last_update_label, "%s", weather_info.last_update);
    else
        lv_label_set_text(weather_ui.last_update_label, "");
    lv_label_set_long_mode(weather_ui.last_update_label, LV_LABEL_LONG_SCROLL);

    lv_obj_t *refresh_btn = lv_btn_create(page->root);
    lv_obj_set_size(refresh_btn, 200, 50);
    lv_obj_align(refresh_btn, LV_ALIGN_TOP_MID, 0, 400);
    lv_obj_set_style_bg_color(refresh_btn, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(refresh_btn, LV_OPA_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(refresh_btn, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(refresh_btn, 25, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(refresh_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(refresh_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(refresh_btn, weather_event_cb, LV_EVENT_CLICKED, NULL);
    lv_group_add_obj(page->group, refresh_btn);

    lv_obj_t *refresh_label = lv_label_create(refresh_btn);
    lv_obj_align(refresh_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_size(refresh_label, 40, 40);
    lv_label_set_text(refresh_label, "UPDATE");
    lv_label_set_long_mode(refresh_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(refresh_label, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(refresh_label, LV_OPA_80, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(refresh_label, lv_color_hex(LV_COLOR_THEME_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(refresh_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_width(refresh_label, LV_PCT(100));
}

static void weather_timer_callback(lv_timer_t *timer)
{
    user_seniverse_config_t weather_info;

    char *weather = get_weather();
    if (weather)
    {
        http_weather_data_parse(weather, &weather_info);

        lv_label_set_text_fmt(weather_ui.last_update_label, "%s", weather_info.last_update);
        lv_label_set_text_fmt(weather_ui.weather_city_label, "%s,%s", weather_info.name, weather_info.country);
        lv_label_set_text_fmt(weather_ui.weather_temp_label, "%s°C", weather_info.now_config.temperature);
        int weather_code = atoi(weather_info.now_config.code);
        if (weather_code >= 0 && weather_code <= 3)
        {
            lv_img_set_src(weather_ui.weather_icon, &sun96_icon);
        }
        else if (weather_code >= 4 && weather_code <= 9)
        {
            lv_img_set_src(weather_ui.weather_icon, &cloudyday96_icon);
        }
        else if (weather_code >= 10 && weather_code <= 11)
        {
            lv_img_set_src(weather_ui.weather_icon, &cloud96_icon);
        }
        else if (weather_code >= 12 && weather_code <= 18)
        {
            lv_img_set_src(weather_ui.weather_icon, &rain96_icon);
        }

        rt_free(weather);
    }
}

static void weather_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *target_obj = lv_event_get_target(e);
    int event_id = (int)lv_event_get_user_data(e);
    user_seniverse_config_t weather_info;

    switch (code)
    {
    case LV_EVENT_CLICKED:
    {
        char *weather = get_weather();
        if (weather)
        {
            http_weather_data_parse(weather, &weather_info);
            lv_label_set_text_fmt(weather_ui.last_update_label, "%s", weather_info.last_update);
            lv_label_set_text_fmt(weather_ui.weather_city_label, "%s,%s", weather_info.name, weather_info.country);
            lv_label_set_text_fmt(weather_ui.weather_temp_label, "%s°C", weather_info.now_config.temperature);
            int weather_code = atoi(weather_info.now_config.code);
            if (weather_code >= 0 && weather_code <= 3)
            {
                lv_img_set_src(weather_ui.weather_icon, &sun96_icon);
            }
            else if (weather_code >= 4 && weather_code <= 9)
            {
                lv_img_set_src(weather_ui.weather_icon, &cloudyday96_icon);
            }
            else if (weather_code >= 10 && weather_code <= 11)
            {
                lv_img_set_src(weather_ui.weather_icon, &cloud96_icon);
            }
            else if (weather_code >= 12 && weather_code <= 18)
            {
                lv_img_set_src(weather_ui.weather_icon, &rain96_icon);
            }

            rt_free(weather);
        }
        break;
    }
    }
}

/**********aduio page **********/
static void aduio_page_on_create(struct Page *page)
{
    LOG_D("aduio_page_on_create\n");

    lv_obj_t *page_tile = lv_label_create(page->root);
    lv_obj_set_height(page_tile, 30);
    lv_obj_align(page_tile, LV_ALIGN_TOP_MID, 0, 10);
    lv_label_set_text(page_tile, "Audio");
    lv_obj_set_style_text_font(page_tile, &lv_font_montserrat_36, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(page_tile, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    audio_ui.audio_db_bar = lv_bar_create(page->root);
    lv_obj_set_pos(audio_ui.audio_db_bar, 60, 80);
    lv_obj_set_size(audio_ui.audio_db_bar, 360, 20);
    lv_obj_set_style_anim_duration(audio_ui.audio_db_bar, 1000, 0);
    lv_bar_set_mode(audio_ui.audio_db_bar, LV_BAR_MODE_NORMAL);
    lv_bar_set_range(audio_ui.audio_db_bar, -80, 80);
    lv_bar_set_value(audio_ui.audio_db_bar, -50, LV_ANIM_OFF);

    // Write style for screen_audio_bar_1, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_bg_opa(audio_ui.audio_db_bar, 60, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(audio_ui.audio_db_bar, lv_color_hex(LV_COLOR_THEME_BLACK), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(audio_ui.audio_db_bar, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(audio_ui.audio_db_bar, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(audio_ui.audio_db_bar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Write style for screen_audio_bar_1, Part: LV_PART_INDICATOR, State: LV_STATE_DEFAULT.
    lv_obj_set_style_bg_opa(audio_ui.audio_db_bar, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(audio_ui.audio_db_bar, lv_color_hex(LV_COLOR_THEME_GREEN), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(audio_ui.audio_db_bar, LV_GRAD_DIR_HOR, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(audio_ui.audio_db_bar, lv_color_hex(LV_COLOR_WARM_RED), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_main_stop(audio_ui.audio_db_bar, 0, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_stop(audio_ui.audio_db_bar, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(audio_ui.audio_db_bar, 0, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    lv_obj_t *cont = create_info_card(page->root, "Mic db", "0 db", 40, 140);
    audio_ui.audio_db_label = lv_obj_get_child(cont, 1);
    cont = create_info_card(page->root, "Volume", "15", 260, 140);
    audio_ui.audio_vol_label = lv_obj_get_child(cont, 1);

    audio_ui.audio_recorded_btn = lv_btn_create(page->root);
    lv_obj_set_size(audio_ui.audio_recorded_btn, 150, 150);
    lv_obj_align(audio_ui.audio_recorded_btn, LV_ALIGN_TOP_LEFT, 40, 230);
    lv_obj_set_style_bg_color(audio_ui.audio_recorded_btn, lv_color_hex(LV_COLOR_BTN_BLUE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(audio_ui.audio_recorded_btn, LV_OPA_100, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(audio_ui.audio_recorded_btn, 100, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(audio_ui.audio_recorded_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(audio_ui.audio_recorded_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_group_add_obj(page->group, audio_ui.audio_recorded_btn);
    ui_event_t = AUDIO_EVENT_BTN_RECORD;
    lv_obj_add_event_cb(audio_ui.audio_recorded_btn, aduio_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)ui_event_t);

    lv_obj_t *audio_recorded_btn_label = lv_label_create(audio_ui.audio_recorded_btn);
    lv_obj_set_style_text_font(audio_recorded_btn_label, &lv_font_montserrat_30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(audio_recorded_btn_label, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(audio_recorded_btn_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(audio_recorded_btn_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(audio_recorded_btn_label, "Record");

    audio_ui.audio_play_btn = lv_btn_create(page->root);
    lv_obj_set_size(audio_ui.audio_play_btn, 150, 150);
    lv_obj_align(audio_ui.audio_play_btn, LV_ALIGN_TOP_RIGHT, -40, 230);
    lv_obj_set_style_bg_color(audio_ui.audio_play_btn, lv_color_hex(LV_COLOR_BTN_BLUE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(audio_ui.audio_play_btn, LV_OPA_100, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(audio_ui.audio_play_btn, 100, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(audio_ui.audio_play_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(audio_ui.audio_play_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_group_add_obj(page->group, audio_ui.audio_play_btn);

    lv_obj_t *audio_play_btn_label = lv_label_create(audio_ui.audio_play_btn);
    lv_obj_set_style_text_font(audio_play_btn_label, &lv_font_montserrat_30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(audio_play_btn_label, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(audio_play_btn_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(audio_play_btn_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(audio_play_btn_label, "Play");
    ui_event_t = AUDIO_EVENT_BTN_PLAY;
    lv_obj_add_event_cb(audio_ui.audio_play_btn, aduio_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)ui_event_t);
}

static void audio_timer_callback(lv_timer_t *timer) // 10ms调用一次
{
    audio_manager_t *audio_info = get_audio_info();
    if (audio_info->is_recording)
    {
        lv_bar_set_value(audio_ui.audio_db_bar, audio_info->current_db, LV_ANIM_OFF);
        lv_label_set_text_fmt(audio_ui.audio_db_label, "mic db: %d", audio_info->current_db);
    }

    if (!audio_info->is_playing)
    {
        lv_obj_set_style_bg_color(audio_ui.audio_play_btn, lv_color_hex(LV_COLOR_BTN_BLUE), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_t *audio_play_btn_label = lv_obj_get_child(audio_ui.audio_play_btn, 0);
        lv_label_set_text(audio_play_btn_label, "Play");
    }

    lv_label_set_text_fmt(audio_ui.audio_vol_label, "%d", audio_get_volume());
}

static void aduio_event_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_event_code_t event_code = lv_event_get_code(e);
    int event_id = (int)lv_event_get_user_data(e);
    audio_manager_t *audio_info = get_audio_info();
    switch (event_code)
    {
    case LV_EVENT_CLICKED:
        switch (event_id)
        {
        case AUDIO_EVENT_BTN_RECORD:
            lv_obj_t *audio_record_btn_label = lv_obj_get_child(obj, 0);
            if (!audio_info->is_recording)
            {
                lv_obj_set_style_bg_color(obj, lv_color_hex(LV_COLOR_WARM_RED), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_label_set_text(audio_record_btn_label, "Recording");
                audio_record_start(RECORD_WAV_FILE);
            }
            else
            {
                lv_obj_set_style_bg_color(obj, lv_color_hex(LV_COLOR_BTN_BLUE), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_label_set_text(audio_record_btn_label, "Record");
                audio_record_stop();
            }
            break;
        case AUDIO_EVENT_BTN_PLAY:
            lv_obj_t *audio_play_btn_label = lv_obj_get_child(obj, 0);
            if (!audio_info->is_playing)
            {
                lv_obj_set_style_bg_color(obj, lv_color_hex(LV_COLOR_THEME_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
                lv_label_set_text(audio_play_btn_label, "Playing");
                audio_play_start(RECORD_WAV_FILE);
            }
            else
            {
                // lv_obj_set_style_bg_color(obj, lv_color_hex(LV_COLOR_BTN_BLUE), LV_PART_MAIN | LV_STATE_DEFAULT);
                // lv_label_set_text(audio_play_btn_label, "Play");
                audio_play_stop();
            }
            break;
        }
        break;
    }
}

/**********file page **********/
static void file_mune_create(lv_obj_t *parent)
{
    lv_group_remove_all_objs(page_list[PAGE_FILE].group);

    FileNode *current;

    // --- 页面标题 ---
    lv_obj_t *page_title = lv_label_create(parent);
    lv_obj_set_size(page_title, 440, 25);
    lv_obj_align(page_title, LV_ALIGN_TOP_MID, 0, 5);
    lv_label_set_text(page_title, "File Explorer");
    lv_obj_set_style_text_font(page_title, &lv_font_montserrat_36, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(page_title, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(page_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    // --- 路径显示栏（支持中文路径） ---
    lv_obj_t *path_label = lv_label_create(parent);
    lv_obj_set_size(path_label, 440, 22);
    lv_obj_align(path_label, LV_ALIGN_TOP_MID, 0, 33);
    lv_label_set_text(path_label, current_dir);
    lv_obj_set_style_text_font(path_label, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(path_label, lv_color_hex(LV_COLOR_THEME_YELLOW), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(path_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_long_mode(path_label, LV_LABEL_LONG_SCROLL_CIRCULAR);

    // --- 文件列表 ---
    lv_obj_t *file_list = lv_list_create(parent);
    lv_obj_set_size(file_list, 440, 420);
    lv_obj_set_style_bg_color(file_list, lv_color_hex(LV_COLOR_THEME_BLACK), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(file_list, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(file_list, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(file_list, LV_ALIGN_TOP_MID, 0, 55);

    // --- 返回上级 ---
    if (strcmp(current_dir, "/") != 0)
    {
        lv_obj_t *up_btn = lv_list_add_btn(file_list, LV_SYMBOL_UP, "  ..");
        lv_obj_set_style_bg_color(up_btn, lv_color_hex(LV_COLOR_THEME_DIM_GRAY), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(up_btn, lv_color_hex(LV_COLOR_THEME_GREEN), LV_PART_MAIN | LV_STATE_FOCUSED);
        lv_obj_set_style_text_color(up_btn, lv_color_hex(LV_COLOR_THEME_YELLOW), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(up_btn, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(up_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_user_data(up_btn, NULL);
        lv_obj_add_event_cb(up_btn, file_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)FILE_EVENT_BTN_UP_ONE_LEVEL);
        lv_group_add_obj(page_list[PAGE_FILE].group, up_btn);
    }

    // --- 先显示文件夹（黄色），再显示文件（白色） ---
    current = file_list_head;
    while (current != NULL)
    {
        if (current->is_dir)
        {
            lv_obj_t *dir_item = lv_list_add_btn(file_list, LV_SYMBOL_DIRECTORY, current->name);
            lv_obj_set_style_bg_color(dir_item, lv_color_hex(LV_COLOR_THEME_BLACK), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(dir_item, lv_color_hex(LV_COLOR_THEME_DIM_GRAY), LV_PART_MAIN | LV_STATE_FOCUSED);
            lv_obj_set_style_text_color(dir_item, lv_color_hex(LV_COLOR_THEME_YELLOW), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(dir_item, lv_color_hex(LV_COLOR_THEME_GREEN), LV_PART_MAIN | LV_STATE_FOCUSED);
            lv_obj_set_style_text_font(dir_item, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(dir_item, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_user_data(dir_item, (void *)current);
            lv_obj_add_event_cb(dir_item, file_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)FILE_EVENT_BTN_DIR);
            lv_group_add_obj(page_list[PAGE_FILE].group, dir_item);
        }
        current = current->next;
    }

    current = file_list_head;
    while (current != NULL)
    {
        if (!current->is_dir)
        {
            lv_obj_t *file_item = lv_list_add_btn(file_list, LV_SYMBOL_FILE, current->name);
            lv_obj_set_style_bg_color(file_item, lv_color_hex(LV_COLOR_THEME_BLACK), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(file_item, lv_color_hex(LV_COLOR_THEME_DIM_GRAY), LV_PART_MAIN | LV_STATE_FOCUSED);
            lv_obj_set_style_text_color(file_item, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(file_item, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(file_item, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_user_data(file_item, (void *)current);
            lv_obj_add_event_cb(file_item, file_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)FILE_EVENT_BTN_FILE);
            lv_group_add_obj(page_list[PAGE_FILE].group, file_item);
        }
        current = current->next;
    }
}

static void get_parent_path(const char *path, char *parent_path, size_t size)
{
    strncpy(parent_path, path, size);
    parent_path[size - 1] = '\0';

    // 找到最后一个'/'的位置
    char *last_slash = strrchr(parent_path, '/');
    if (last_slash == NULL || last_slash == parent_path)
    {
        // 根目录，返回根
        strncpy(parent_path, "/", size);
        return;
    }

    *last_slash = '\0'; // 去掉最后一个/
    if (strlen(parent_path) == 0)
    {
        strncpy(parent_path, "/", size);
    }
}

static void file_page_on_create(struct Page *page)
{
    LOG_D("file_page_on_create\n");
    strcpy(current_dir, "/");

    lv_obj_t *page_title = lv_label_create(page->root);
    lv_obj_set_size(page_title, 440, 25);
    lv_obj_align(page_title, LV_ALIGN_TOP_MID, 0, 5);
    lv_label_set_text(page_title, "File Explorer");
    lv_obj_set_style_text_font(page_title, &lv_font_montserrat_36, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(page_title, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(page_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *path_label = lv_label_create(page->root);
    lv_obj_set_size(path_label, 440, 22);
    lv_obj_align(path_label, LV_ALIGN_TOP_MID, 0, 33);
    lv_label_set_text(path_label, current_dir);
    lv_obj_set_style_text_font(path_label, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(path_label, lv_color_hex(LV_COLOR_THEME_YELLOW), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(path_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Loading 提示 */
    lv_obj_t *loading_label = lv_label_create(page->root);
    lv_obj_align(loading_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(loading_label, "Scanning SD card...");
    lv_obj_set_style_text_font(loading_label, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(loading_label, lv_color_hex(LV_COLOR_THEME_GRAY), LV_PART_MAIN | LV_STATE_DEFAULT);

    /* 后台线程扫描，不阻塞 LVGL */
    if (file_scan_thread == RT_NULL)
    {
        free_file_list();
        file_scan_done = false;
        file_scan_thread = rt_thread_create("fscan",
                                            file_scan_thread_entry,
                                            NULL, 2048, 20, 20);
        if (file_scan_thread != RT_NULL)
        {
            rt_thread_startup(file_scan_thread);
        }
    }
}

/* 后台文件扫描线程：在非 LVGL 上下文中执行阻塞 I/O */
static void file_scan_thread_entry(void *parameter)
{
    LOG_D("file scan thread started\n");
    list_files(current_dir, 0);
    file_scan_done = true;
    file_scan_thread = RT_NULL;
}

/* FILE 页面定时器回调：检查扫描完成状态，异步更新 UI */
static void file_timer_callback(lv_timer_t *timer)
{
    if (file_scan_done)
    {
        file_scan_done = false;
        LOG_D("file list ready, updating UI\n");
        lv_obj_clean(current_page->root);
        file_mune_create(current_page->root);
    }
}

static void close_file_event_cb(lv_event_t *e)
{
    log_d("close_file_event_cb");
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *target_obj = lv_event_get_target(e);
    int event_type = (int)lv_event_get_user_data(e);
    lv_obj_t *cont = (lv_obj_t *)lv_obj_get_user_data(target_obj);

    switch (code)
    {
    case LV_EVENT_CLICKED:
        switch (event_type)
        {
        case FILE_EVENT_BTN_CLOSE:
            lv_group_remove_obj(target_obj);
            lv_group_focus_obj(last_group_obj);
            lv_obj_clean(cont);
            lv_obj_del(cont);
            break;
        }
        break;
    }
}

static void file_event_cb(lv_event_t *e)
{
    log_d("file_event_cb");
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *target_obj = lv_event_get_target(e);
    FileNode *node = (FileNode *)lv_obj_get_user_data(target_obj);
    int event_type = (int)lv_event_get_user_data(e);

    if (code != LV_EVENT_CLICKED)
        return;

    last_group_obj = target_obj;

    switch (event_type)
    {
    case FILE_EVENT_BTN_UP_ONE_LEVEL:
    {
        if (file_scan_thread != RT_NULL)
            break;
        char parent_path[256];
        get_parent_path(current_dir, parent_path, sizeof(parent_path));
        strcpy(current_dir, parent_path);
        free_file_list();
        file_scan_done = false;
        lv_obj_clean(current_page->root);
        lv_obj_t *loading = lv_label_create(current_page->root);
        lv_obj_align(loading, LV_ALIGN_CENTER, 0, 0);
        lv_label_set_text(loading, "Scanning...");
        lv_obj_set_style_text_font(loading, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(loading, lv_color_hex(LV_COLOR_THEME_GRAY), LV_PART_MAIN | LV_STATE_DEFAULT);
        file_scan_thread = rt_thread_create("fscan", file_scan_thread_entry, NULL, 2048, 20, 20);
        if (file_scan_thread != RT_NULL)
            rt_thread_startup(file_scan_thread);
        break;
    }
    case FILE_EVENT_BTN_DIR:
    {
        if (node == NULL || file_scan_thread != RT_NULL)
            return;
        log_d("open dir: %s", node->full_path);
        strcpy(current_dir, node->full_path);
        free_file_list();
        file_scan_done = false;
        lv_obj_clean(current_page->root);
        lv_obj_t *loading = lv_label_create(current_page->root);
        lv_obj_align(loading, LV_ALIGN_CENTER, 0, 0);
        lv_label_set_text(loading, "Scanning...");
        lv_obj_set_style_text_font(loading, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(loading, lv_color_hex(LV_COLOR_THEME_GRAY), LV_PART_MAIN | LV_STATE_DEFAULT);
        file_scan_thread = rt_thread_create("fscan", file_scan_thread_entry, NULL, 2048, 20, 20);
        if (file_scan_thread != RT_NULL)
            rt_thread_startup(file_scan_thread);
        break;
    }
    case FILE_EVENT_BTN_FILE:
    {
        if (node == NULL)
            return;
        rt_mutex_take(lvgl_mutex, RT_WAITING_FOREVER);
        FIL fil;
        FRESULT res = f_open(&fil, node->full_path, FA_READ | FA_OPEN_EXISTING);
        if (res == FR_OK)
        {
            char *buf = (char *)malloc(512);
            if (buf != NULL)
            {
                UINT bytes_read;
                res = f_read(&fil, buf, 511, &bytes_read);
                log_d("Read from %s file: %s", node->name, buf);

                if (res == FR_OK && bytes_read > 0)
                {
                    buf[bytes_read] = '\0';

                    // 文件内容覆盖层
                    lv_obj_t *cont = lv_obj_create(current_page->root);
                    lv_obj_set_style_bg_opa(cont, LV_OPA_100, 0);
                    lv_obj_set_style_pad_all(cont, 0, 0);
                    lv_obj_set_style_border_width(cont, 0, 0);
                    lv_obj_set_style_bg_color(cont, lv_color_hex(LV_COLOR_THEME_BLACK), 0);
                    lv_obj_set_size(cont, 480, 480);
                    lv_obj_align(cont, LV_ALIGN_CENTER, 0, 0);

                    // 顶部标题栏
                    lv_obj_t *header = lv_obj_create(cont);
                    lv_obj_set_size(header, 480, 46);
                    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
                    lv_obj_set_style_bg_color(header, lv_color_hex(0x1a1a1a), 0);
                    lv_obj_set_style_border_width(header, 0, 0);
                    lv_obj_set_style_pad_all(header, 0, 0);
                    lv_obj_set_style_radius(header, 0, 0);

                    // 文件名（左对齐）
                    lv_obj_t *title_label = lv_label_create(header);
                    lv_label_set_text(title_label, node->name);
                    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_24, 0);
                    lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);
                    lv_obj_set_style_text_color(title_label, lv_color_hex(LV_COLOR_THEME_WIHTE), 0);
                    lv_label_set_long_mode(title_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
                    lv_obj_set_size(title_label, 380, 30);
                    lv_obj_align(title_label, LV_ALIGN_CENTER, 0, 0);

                    // 关闭按钮（右对齐，固定在标题栏内）
                    lv_obj_t *file_btn = lv_button_create(header);
                    lv_obj_set_size(file_btn, 40, 40);
                    lv_obj_align(file_btn, LV_ALIGN_RIGHT_MID, -30, 0);
                    lv_obj_t *file_btn_label = lv_label_create(file_btn);
                    lv_label_set_text(file_btn_label, LV_SYMBOL_CLOSE);
                    lv_obj_align(file_btn_label, LV_ALIGN_CENTER, 0, 0);
                    lv_obj_set_style_radius(file_btn, 20, 0);
                    lv_obj_set_style_border_width(file_btn, 0, 0);
                    lv_obj_set_style_bg_color(file_btn, lv_color_hex(LV_COLOR_WARM_RED), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_color(file_btn, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_style_text_font(file_btn, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
                    lv_obj_set_user_data(file_btn, (void *)cont);
                    lv_obj_add_event_cb(file_btn, close_file_event_cb, LV_EVENT_CLICKED, (void *)FILE_EVENT_BTN_CLOSE);
                    lv_group_add_obj(page_list[PAGE_FILE].group, file_btn);
                    lv_group_focus_obj(file_btn);

                    // 文件内容
                    lv_obj_t *buf_label = lv_label_create(cont);
                    lv_label_set_text(buf_label, buf);
                    lv_obj_set_size(buf_label, 460, 420);
                    lv_obj_align(buf_label, LV_ALIGN_TOP_MID, 0, 55);
                    lv_obj_set_style_text_font(buf_label, &lv_font_montserrat_24, 0);
                    lv_obj_set_style_text_color(buf_label, lv_color_hex(LV_COLOR_THEME_WIHTE), 0);
                    lv_obj_set_style_text_align(buf_label, LV_TEXT_ALIGN_LEFT, 0);
                }
                else
                {
                    log_d("File is empty or read error: %s", node->full_path);
                }
                free(buf);
            }
            f_close(&fil);
        }
        rt_mutex_release(lvgl_mutex);
        break;
    }
    }
}

/**********keyboard page **********/
static void keyboard_page_on_create(struct Page *page)
{
    LOG_D("keyboard_page_on_create\n");
    const char *key_text[20] = {
        "KEY41", "KEY42", "HOME", "KEY44",
        "KEY31", "KEY32", "KEY33", "KEY34",
        "KEY21", "KEY22", "KEY23", "KEY24",
        "KEY11", "KEY12", "KEY13", "KEY14",
        "KEY1", "KEY2", "KEY3", "KEY4"};
    static lv_style_t key_style;
    lv_style_init(&key_style);
    lv_style_set_bg_color(&key_style, lv_color_hex(LV_COLOR_BTN_BLUE));
    lv_style_set_text_color(&key_style, lv_color_hex(LV_COLOR_THEME_WIHTE));
    lv_style_set_text_font(&key_style, &lv_font_montserrat_16);
    lv_style_set_border_width(&key_style, 0);
    lv_style_set_radius(&key_style, LV_RADIUS_CIRCLE);

    lv_obj_t *page_tile = lv_label_create(page->root);
    lv_obj_set_height(page_tile, 30);
    lv_obj_align(page_tile, LV_ALIGN_TOP_MID, 0, 10);
    lv_label_set_text(page_tile, "Keyboard");
    lv_obj_set_style_text_font(page_tile, &lv_font_montserrat_36, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(page_tile, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    for (int i = 0; i < 5; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            int index = i * 4 + j;
            keyboard_ui.key[index] = lv_btn_create(page->root);
            lv_obj_set_size(keyboard_ui.key[index], 80, 40);
            if (j == 0)
            {
                lv_obj_align(keyboard_ui.key[index], LV_ALIGN_TOP_LEFT, 40, 60 + i * 80);
            }
            else
            {
                lv_obj_align(keyboard_ui.key[index], LV_ALIGN_TOP_LEFT, 40 + j * 110, 80 + i * 80);
            }
            lv_obj_add_style(keyboard_ui.key[index], &key_style, 0);
            lv_obj_set_style_shadow_width(keyboard_ui.key[index], 0, 0);
            lv_obj_set_style_bg_color(keyboard_ui.key[index], lv_color_hex(LV_COLOR_BTN_BLUE), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_color(keyboard_ui.key[index], lv_color_hex(LV_COLOR_WARM_RED), LV_PART_MAIN | LV_STATE_CHECKED);
            lv_obj_set_user_data(keyboard_ui.key[index], (void *)(uintptr_t)((4 - i) * 10 + (j + 1)));
            lv_obj_t *label = lv_label_create(keyboard_ui.key[index]);
            lv_label_set_text(label, key_text[index]);
            lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
            lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_add_flag(keyboard_ui.key[index], LV_OBJ_FLAG_CHECKABLE); // 让按钮可切换
            lv_obj_add_event_cb(keyboard_ui.key[index], keyboard_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)index);
        }
    }
}

static void keyboard_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *target_obj = lv_event_get_target(e);
    int index = (int)lv_event_get_user_data(e);
    log_d("keyboard_event_cb:%d\n", index);
    switch (code)
    {
    case LV_EVENT_CLICKED:
        if (index == 0)
        {
            switch_to_page(&page_list[PAGE_HOME]);
        }
        break;
    }
}

/**********system page **********/
static void system_page_on_create(struct Page *page)
{
    LOG_D("system_page_on_create\n");

    lv_obj_set_scrollbar_mode(page->root, LV_SCROLLBAR_MODE_OFF);

    /* Root container (black background matching dropdown style) */
    lv_obj_t *cont = lv_obj_create(page->root);
    lv_obj_set_size(cont, 480, 480);
    lv_obj_align(cont, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(cont, lv_color_hex(LV_COLOR_THEME_BLACK), 0);
    lv_obj_set_style_radius(cont, 0, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_style_shadow_width(cont, 0, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    /* Title */
    lv_obj_t *title = lv_label_create(cont);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_color(title, lv_color_hex(LV_COLOR_THEME_WIHTE), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_36, 0);

    /* ─── Card 1: Connectivity & Display ─── */
    lv_obj_t *card1 = lv_obj_create(cont);
    lv_obj_set_size(card1, 440, 220);
    lv_obj_align(card1, LV_ALIGN_TOP_LEFT, 20, 56);
    lv_obj_set_style_bg_color(card1, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_radius(card1, 16, 0);
    lv_obj_set_style_border_width(card1, 0, 0);
    lv_obj_set_style_pad_all(card1, 0, 0);
    lv_obj_set_style_shadow_width(card1, 0, 0);
    // lv_obj_clear_flag(card1, LV_OBJ_FLAG_SCROLLABLE);

    /* Row: Bluetooth */
    system_ui.bt_btn = lv_button_create(card1);
    lv_obj_set_size(system_ui.bt_btn, 424, 48);
    lv_obj_set_pos(system_ui.bt_btn, 8, 8);
    lv_obj_set_style_bg_opa(system_ui.bt_btn, LV_OPA_10, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(system_ui.bt_btn, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(system_ui.bt_btn, 12, 0);
    lv_obj_set_style_border_width(system_ui.bt_btn, 0, 0);
    lv_obj_set_style_shadow_width(system_ui.bt_btn, 0, 0);

    lv_obj_t *bt_icon = lv_label_create(system_ui.bt_btn);
    lv_label_set_text(bt_icon, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_color(bt_icon, lv_color_hex(LV_COLOR_THEME_WIHTE), 0);
    lv_obj_set_style_text_font(bt_icon, &lv_font_montserrat_24, 0);
    lv_obj_align(bt_icon, LV_ALIGN_LEFT_MID, 14, 0);

    lv_obj_t *bt_name = lv_label_create(system_ui.bt_btn);
    lv_label_set_text(bt_name, "Bluetooth");
    lv_obj_set_style_text_color(bt_name, lv_color_hex(LV_COLOR_THEME_WIHTE), 0);
    lv_obj_set_style_text_font(bt_name, &lv_font_montserrat_24, 0);
    lv_obj_align(bt_name, LV_ALIGN_LEFT_MID, 52, 0);

    lv_obj_t *bt_status = lv_label_create(system_ui.bt_btn);
    lv_label_set_text(bt_status, device_switch.bluetooth_enable ? "ON" : "OFF");
    lv_obj_set_style_text_color(bt_status, device_switch.bluetooth_enable ? lv_color_hex(LV_COLOR_THEME_GREEN) : lv_color_hex(LV_COLOR_THEME_GRAY), 0);
    lv_obj_set_style_text_font(bt_status, &lv_font_montserrat_20, 0);
    lv_obj_align(bt_status, LV_ALIGN_RIGHT_MID, -16, 0);

    ui_event_t = SYSTEM_EVENT_BLUETOOTH;
    lv_obj_add_event_cb(system_ui.bt_btn, system_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)ui_event_t);
    lv_group_add_obj(page->group, system_ui.bt_btn);

    /* Row: WiFi */
    system_ui.wifi_btn = lv_button_create(card1);
    lv_obj_set_size(system_ui.wifi_btn, 424, 48);
    lv_obj_set_pos(system_ui.wifi_btn, 8, 60);
    lv_obj_set_style_bg_opa(system_ui.wifi_btn, LV_OPA_10, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(system_ui.wifi_btn, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(system_ui.wifi_btn, 12, 0);
    lv_obj_set_style_border_width(system_ui.wifi_btn, 0, 0);
    lv_obj_set_style_shadow_width(system_ui.wifi_btn, 0, 0);

    lv_obj_t *wifi_icon = lv_label_create(system_ui.wifi_btn);
    lv_label_set_text(wifi_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(wifi_icon, lv_color_hex(LV_COLOR_THEME_WIHTE), 0);
    lv_obj_set_style_text_font(wifi_icon, &lv_font_montserrat_24, 0);
    lv_obj_align(wifi_icon, LV_ALIGN_LEFT_MID, 14, 0);

    lv_obj_t *wifi_name = lv_label_create(system_ui.wifi_btn);
    lv_label_set_text(wifi_name, "WiFi");
    lv_obj_set_style_text_color(wifi_name, lv_color_hex(LV_COLOR_THEME_WIHTE), 0);
    lv_obj_set_style_text_font(wifi_name, &lv_font_montserrat_24, 0);
    lv_obj_align(wifi_name, LV_ALIGN_LEFT_MID, 52, 0);

    lv_obj_t *wifi_status = lv_label_create(system_ui.wifi_btn);
    lv_label_set_text(wifi_status, device_switch.wifi_enable ? "ON" : "OFF");
    lv_obj_set_style_text_color(wifi_status, device_switch.wifi_enable ? lv_color_hex(LV_COLOR_THEME_GREEN) : lv_color_hex(LV_COLOR_THEME_GRAY), 0);
    lv_obj_set_style_text_font(wifi_status, &lv_font_montserrat_20, 0);
    lv_obj_align(wifi_status, LV_ALIGN_RIGHT_MID, -16, 0);

    ui_event_t = SYSTEM_EVENT_WIFI;
    lv_obj_add_event_cb(system_ui.wifi_btn, system_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)ui_event_t);
    lv_group_add_obj(page->group, system_ui.wifi_btn);

    /* Row: key_led */
    system_ui.key_led_btn = lv_button_create(card1);
    lv_obj_set_size(system_ui.key_led_btn, 424, 48);
    lv_obj_set_pos(system_ui.key_led_btn, 8, 112);
    lv_obj_set_style_bg_opa(system_ui.key_led_btn, LV_OPA_10, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(system_ui.key_led_btn, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(system_ui.key_led_btn, 12, 0);
    lv_obj_set_style_border_width(system_ui.key_led_btn, 0, 0);
    lv_obj_set_style_shadow_width(system_ui.key_led_btn, 0, 0);

    lv_obj_t *key_led_icon = lv_label_create(system_ui.key_led_btn);
    lv_label_set_text(key_led_icon, LV_SYMBOL_WIFI);
    lv_obj_set_style_text_color(key_led_icon, lv_color_hex(LV_COLOR_THEME_WIHTE), 0);
    lv_obj_set_style_text_font(key_led_icon, &lv_font_montserrat_24, 0);
    lv_obj_align(key_led_icon, LV_ALIGN_LEFT_MID, 14, 0);

    lv_obj_t *key_led_name = lv_label_create(system_ui.key_led_btn);
    lv_label_set_text(key_led_name, "Key LED");
    lv_obj_set_style_text_color(key_led_name, lv_color_hex(LV_COLOR_THEME_WIHTE), 0);
    lv_obj_set_style_text_font(key_led_name, &lv_font_montserrat_24, 0);
    lv_obj_align(key_led_name, LV_ALIGN_LEFT_MID, 52, 0);

    lv_obj_t *key_led_status = lv_label_create(system_ui.key_led_btn);
    lv_label_set_text(key_led_status, device_switch.keyboard_led_enable ? "ON" : "OFF");
    lv_obj_set_style_text_color(key_led_status, device_switch.keyboard_led_enable ? lv_color_hex(LV_COLOR_THEME_GREEN) : lv_color_hex(LV_COLOR_THEME_GRAY), 0);
    lv_obj_set_style_text_font(key_led_status, &lv_font_montserrat_20, 0);
    lv_obj_align(key_led_status, LV_ALIGN_RIGHT_MID, -16, 0);

    ui_event_t = SYSTEM_EVENT_KEY_LED;
    lv_obj_add_event_cb(system_ui.key_led_btn, system_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)ui_event_t);
    lv_group_add_obj(page->group, system_ui.key_led_btn);

    /* Row: Brightness */
    lv_obj_t *bri_row = lv_obj_create(card1);
    lv_obj_set_size(bri_row, 424, 48);
    lv_obj_set_pos(bri_row, 8, 164);
    lv_obj_set_style_bg_opa(bri_row, LV_OPA_10, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(bri_row, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(bri_row, 12, 0);
    lv_obj_set_style_border_width(bri_row, 0, 0);
    lv_obj_set_style_shadow_width(bri_row, 0, 0);
    lv_obj_set_style_pad_all(bri_row, 0, 0);
    lv_obj_clear_flag(bri_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *bri_icon = lv_label_create(bri_row);
    lv_label_set_text(bri_icon, LV_SYMBOL_EYE_OPEN);
    lv_obj_set_style_text_color(bri_icon, lv_color_hex(LV_COLOR_THEME_WIHTE), 0);
    lv_obj_set_style_text_font(bri_icon, &lv_font_montserrat_24, 0);
    lv_obj_align(bri_icon, LV_ALIGN_LEFT_MID, 14, 0);

    lv_obj_t *bri_name = lv_label_create(bri_row);
    lv_label_set_text(bri_name, "Brightness");
    lv_obj_set_style_text_color(bri_name, lv_color_hex(LV_COLOR_THEME_WIHTE), 0);
    lv_obj_set_style_text_font(bri_name, &lv_font_montserrat_24, 0);
    lv_obj_align(bri_name, LV_ALIGN_LEFT_MID, 52, 0);

    rt_device_t lcd_device = rt_device_find("lcd");
    uint8_t bri;
    if (lcd_device)
    {
        rt_device_control(lcd_device, RTGRAPHIC_CTRL_GET_BRIGHTNESS, &bri);
    }

    system_ui.brightness_slider = lv_slider_create(bri_row);
    lv_obj_set_size(system_ui.brightness_slider, 160, 32);
    lv_obj_align(system_ui.brightness_slider, LV_ALIGN_RIGHT_MID, -60, 0);
    lv_slider_set_range(system_ui.brightness_slider, 10, 100);
    lv_slider_set_value(system_ui.brightness_slider, bri, LV_ANIM_OFF);
    lv_obj_set_style_radius(system_ui.brightness_slider, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(system_ui.brightness_slider, LV_OPA_30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(system_ui.brightness_slider, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(system_ui.brightness_slider, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(system_ui.brightness_slider, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(system_ui.brightness_slider, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(system_ui.brightness_slider, lv_color_hex(LV_COLOR_THEME_YELLOW), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(system_ui.brightness_slider, 16, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(system_ui.brightness_slider, 0, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(system_ui.brightness_slider, 0, LV_PART_KNOB | LV_STATE_DEFAULT);

    lv_obj_t *bri_val = lv_label_create(bri_row);
    lv_label_set_text_fmt(bri_val, "%d%%", bri);
    lv_obj_set_style_text_color(bri_val, lv_color_hex(LV_COLOR_THEME_WIHTE), 0);
    lv_obj_set_style_text_font(bri_val, &lv_font_montserrat_20, 0);
    lv_obj_align(bri_val, LV_ALIGN_RIGHT_MID, -12, 0);

    ui_event_t = SYSTEM_EVENT_BRIGHTNESS;
    lv_obj_add_event_cb(system_ui.brightness_slider, system_event_cb, LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)ui_event_t);
    lv_group_add_obj(page->group, system_ui.brightness_slider);

    /* Row: Volume */
    lv_obj_t *vol_row = lv_obj_create(card1);
    lv_obj_set_size(vol_row, 424, 48);
    lv_obj_set_pos(vol_row, 8, 216);
    lv_obj_set_style_bg_opa(vol_row, LV_OPA_10, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(vol_row, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(vol_row, 12, 0);
    lv_obj_set_style_border_width(vol_row, 0, 0);
    lv_obj_set_style_shadow_width(vol_row, 0, 0);
    lv_obj_set_style_pad_all(vol_row, 0, 0);
    lv_obj_clear_flag(vol_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *vol_icon = lv_label_create(vol_row);
    lv_label_set_text(vol_icon, LV_SYMBOL_BELL);
    lv_obj_set_style_text_color(vol_icon, lv_color_hex(LV_COLOR_THEME_WIHTE), 0);
    lv_obj_set_style_text_font(vol_icon, &lv_font_montserrat_24, 0);
    lv_obj_align(vol_icon, LV_ALIGN_LEFT_MID, 14, 0);

    lv_obj_t *vol_name = lv_label_create(vol_row);
    lv_label_set_text(vol_name, "Volume");
    lv_obj_set_style_text_color(vol_name, lv_color_hex(LV_COLOR_THEME_WIHTE), 0);
    lv_obj_set_style_text_font(vol_name, &lv_font_montserrat_24, 0);
    lv_obj_align(vol_name, LV_ALIGN_LEFT_MID, 52, 0);

    int vol = audio_get_volume();
    system_ui.volume_slider = lv_slider_create(vol_row);
    lv_obj_set_size(system_ui.volume_slider, 160, 32);
    lv_obj_align(system_ui.volume_slider, LV_ALIGN_RIGHT_MID, -60, 0);
    lv_slider_set_range(system_ui.volume_slider, 0, 15);
    lv_slider_set_value(system_ui.volume_slider, vol, LV_ANIM_OFF);
    lv_obj_set_style_radius(system_ui.volume_slider, 16, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(system_ui.volume_slider, LV_OPA_30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(system_ui.volume_slider, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(system_ui.volume_slider, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(system_ui.volume_slider, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(system_ui.volume_slider, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(system_ui.volume_slider, lv_color_hex(LV_COLOR_THEME_GREEN), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(system_ui.volume_slider, 16, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(system_ui.volume_slider, 0, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(system_ui.volume_slider, 0, LV_PART_KNOB | LV_STATE_DEFAULT);

    lv_obj_t *vol_val = lv_label_create(vol_row);
    lv_label_set_text_fmt(vol_val, "%d", vol);
    lv_obj_set_style_text_color(vol_val, lv_color_hex(LV_COLOR_THEME_WIHTE), 0);
    lv_obj_set_style_text_font(vol_val, &lv_font_montserrat_20, 0);
    lv_obj_align(vol_val, LV_ALIGN_RIGHT_MID, -12, 0);

    ui_event_t = SYSTEM_EVENT_VOLUME;
    lv_obj_add_event_cb(system_ui.volume_slider, system_event_cb, LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)ui_event_t);
    lv_group_add_obj(page->group, system_ui.volume_slider);

    /* ─── Card 2: System Actions ─── */
    lv_obj_t *card2 = lv_obj_create(cont);
    lv_obj_set_size(card2, 440, 168);
    lv_obj_align(card2, LV_ALIGN_TOP_LEFT, 20, 286);
    lv_obj_set_style_bg_color(card2, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_radius(card2, 16, 0);
    lv_obj_set_style_border_width(card2, 0, 0);
    lv_obj_set_style_pad_all(card2, 0, 0);
    lv_obj_set_style_shadow_width(card2, 0, 0);
    lv_obj_clear_flag(card2, LV_OBJ_FLAG_SCROLLABLE);

    /* Row: Shutdown */
    system_ui.shotdown_btn = lv_button_create(card2);
    lv_obj_set_size(system_ui.shotdown_btn, 424, 48);
    lv_obj_set_pos(system_ui.shotdown_btn, 8, 8);
    lv_obj_set_style_bg_opa(system_ui.shotdown_btn, LV_OPA_10, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(system_ui.shotdown_btn, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(system_ui.shotdown_btn, 12, 0);
    lv_obj_set_style_border_width(system_ui.shotdown_btn, 0, 0);
    lv_obj_set_style_shadow_width(system_ui.shotdown_btn, 0, 0);

    lv_obj_t *sd_icon = lv_label_create(system_ui.shotdown_btn);
    lv_label_set_text(sd_icon, LV_SYMBOL_POWER);
    lv_obj_set_style_text_color(sd_icon, lv_color_hex(LV_COLOR_WARM_RED), 0);
    lv_obj_set_style_text_font(sd_icon, &lv_font_montserrat_24, 0);
    lv_obj_align(sd_icon, LV_ALIGN_LEFT_MID, 14, 0);

    lv_obj_t *sd_name = lv_label_create(system_ui.shotdown_btn);
    lv_label_set_text(sd_name, "Shutdown");
    lv_obj_set_style_text_color(sd_name, lv_color_hex(LV_COLOR_THEME_WIHTE), 0);
    lv_obj_set_style_text_font(sd_name, &lv_font_montserrat_24, 0);
    lv_obj_align(sd_name, LV_ALIGN_LEFT_MID, 52, 0);

    lv_obj_t *sd_arrow = lv_label_create(system_ui.shotdown_btn);
    lv_label_set_text(sd_arrow, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(sd_arrow, lv_color_hex(LV_COLOR_THEME_DIM_GRAY), 0);
    lv_obj_set_style_text_font(sd_arrow, &lv_font_montserrat_20, 0);
    lv_obj_align(sd_arrow, LV_ALIGN_RIGHT_MID, -16, 0);

    ui_event_t = SYSTEM_EVENT_SHOTDOWN;
    lv_obj_add_event_cb(system_ui.shotdown_btn, system_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)ui_event_t);
    lv_group_add_obj(page->group, system_ui.shotdown_btn);

    /* Row: Restart */
    system_ui.restart_btn = lv_button_create(card2);
    lv_obj_set_size(system_ui.restart_btn, 424, 48);
    lv_obj_set_pos(system_ui.restart_btn, 8, 60);
    lv_obj_set_style_bg_opa(system_ui.restart_btn, LV_OPA_10, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(system_ui.restart_btn, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(system_ui.restart_btn, 12, 0);
    lv_obj_set_style_border_width(system_ui.restart_btn, 0, 0);
    lv_obj_set_style_shadow_width(system_ui.restart_btn, 0, 0);

    lv_obj_t *re_icon = lv_label_create(system_ui.restart_btn);
    lv_label_set_text(re_icon, "O");
    lv_obj_set_style_text_color(re_icon, lv_color_hex(LV_COLOR_THEME_YELLOW), 0);
    lv_obj_set_style_text_font(re_icon, &lv_font_montserrat_24, 0);
    lv_obj_align(re_icon, LV_ALIGN_LEFT_MID, 14, 0);

    lv_obj_t *re_name = lv_label_create(system_ui.restart_btn);
    lv_label_set_text(re_name, "Restart");
    lv_obj_set_style_text_color(re_name, lv_color_hex(LV_COLOR_THEME_WIHTE), 0);
    lv_obj_set_style_text_font(re_name, &lv_font_montserrat_24, 0);
    lv_obj_align(re_name, LV_ALIGN_LEFT_MID, 52, 0);

    lv_obj_t *re_arrow = lv_label_create(system_ui.restart_btn);
    lv_label_set_text(re_arrow, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(re_arrow, lv_color_hex(LV_COLOR_THEME_DIM_GRAY), 0);
    lv_obj_set_style_text_font(re_arrow, &lv_font_montserrat_20, 0);
    lv_obj_align(re_arrow, LV_ALIGN_RIGHT_MID, -16, 0);

    ui_event_t = SYSTEM_EVENT_RESTART;
    lv_obj_add_event_cb(system_ui.restart_btn, system_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)ui_event_t);
    lv_group_add_obj(page->group, system_ui.restart_btn);

    /* Row: Ship Mode */
    system_ui.ship_mode_btn = lv_button_create(card2);
    lv_obj_set_size(system_ui.ship_mode_btn, 424, 48);
    lv_obj_set_pos(system_ui.ship_mode_btn, 8, 112);
    lv_obj_set_style_bg_opa(system_ui.ship_mode_btn, LV_OPA_10, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(system_ui.ship_mode_btn, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(system_ui.ship_mode_btn, 12, 0);
    lv_obj_set_style_border_width(system_ui.ship_mode_btn, 0, 0);
    lv_obj_set_style_shadow_width(system_ui.ship_mode_btn, 0, 0);

    lv_obj_t *sm_icon = lv_label_create(system_ui.ship_mode_btn);
    lv_label_set_text(sm_icon, "X");
    lv_obj_set_style_text_color(sm_icon, lv_color_hex(LV_COLOR_BTN_BLUE), 0);
    lv_obj_set_style_text_font(sm_icon, &lv_font_montserrat_24, 0);
    lv_obj_align(sm_icon, LV_ALIGN_LEFT_MID, 14, 0);

    lv_obj_t *sm_name = lv_label_create(system_ui.ship_mode_btn);
    lv_label_set_text(sm_name, "Ship Mode");
    lv_obj_set_style_text_color(sm_name, lv_color_hex(LV_COLOR_THEME_WIHTE), 0);
    lv_obj_set_style_text_font(sm_name, &lv_font_montserrat_24, 0);
    lv_obj_align(sm_name, LV_ALIGN_LEFT_MID, 52, 0);

    lv_obj_t *sm_arrow = lv_label_create(system_ui.ship_mode_btn);
    lv_label_set_text(sm_arrow, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(sm_arrow, lv_color_hex(LV_COLOR_THEME_DIM_GRAY), 0);
    lv_obj_set_style_text_font(sm_arrow, &lv_font_montserrat_20, 0);
    lv_obj_align(sm_arrow, LV_ALIGN_RIGHT_MID, -16, 0);

    ui_event_t = SYSTEM_EVENT_SHIP_MODE;
    lv_obj_add_event_cb(system_ui.ship_mode_btn, system_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)ui_event_t);
    lv_group_add_obj(page->group, system_ui.ship_mode_btn);

    lv_group_focus_obj(system_ui.bt_btn);
}

static void ship_mode_confirm_dialog(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    int event_type = (int)lv_event_get_user_data(e);
    lv_obj_t *target_obj = lv_event_get_target(e);
    sgm41562b_handle_t charger;
    charger = sgm41562b_get_handle();
    switch (code)
    {
    case LV_EVENT_CLICKED:
        switch (event_type)
        {
        case SYSTEM_EVENT_SHOTDOWN:
            log_d("Shotdowning...\n");
            into_shotdown();
            break;
        case SYSTEM_EVENT_SHIP_MODE:
            log_d("into Ship Mode...\n");
            sgm41562b_enter_shipping_mode(charger);
            break;
        case SYSTEM_EVENT_RESTART:
            log_d("Restarting...\n");
            wdt_dog_reset();
            break;
        default:
            lv_obj_clean(system_ui.mbox);
            lv_obj_del(system_ui.mbox);
            lv_group_focus_obj(page_list[PAGE_SYSTEM].last_group_obj);
            break;
        }
        break;
    }
}

static void show_system_confirm_dialog(const char *text, int event_type)
{
    /* Main panel */
    system_ui.mbox = lv_obj_create(page_list[PAGE_SYSTEM].root);
    lv_obj_set_size(system_ui.mbox, 380, 270);
    lv_obj_center(system_ui.mbox);
    lv_obj_set_style_bg_color(system_ui.mbox, lv_color_hex(LV_COLOR_THEME_BLACK), 0);
    lv_obj_set_style_radius(system_ui.mbox, 20, 0);
    lv_obj_set_style_border_width(system_ui.mbox, 0, 0);
    lv_obj_set_style_shadow_width(system_ui.mbox, 0, 0);
    lv_obj_set_style_pad_all(system_ui.mbox, 0, 0);
    lv_obj_clear_flag(system_ui.mbox, LV_OBJ_FLAG_SCROLLABLE);

    /* Header row: title + close button */
    lv_obj_t *hd = lv_obj_create(system_ui.mbox);
    lv_obj_set_size(hd, 380, 48);
    lv_obj_align(hd, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_opa(hd, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hd, 0, 0);
    lv_obj_set_style_pad_all(hd, 0, 0);
    lv_obj_clear_flag(hd, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(hd);
    lv_label_set_text(title, "Confirm");
    lv_obj_set_style_text_color(title, lv_color_hex(LV_COLOR_THEME_WIHTE), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 16, 0);

    /* Separator line */
    lv_obj_t *sep = lv_obj_create(system_ui.mbox);
    lv_obj_set_size(sep, 340, 1);
    lv_obj_align(sep, LV_ALIGN_TOP_LEFT, 20, 54);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_radius(sep, 0, 0);
    lv_obj_set_style_pad_all(sep, 0, 0);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE);

    /* Message text */
    lv_obj_t *msg = lv_label_create(system_ui.mbox);
    lv_label_set_text(msg, text);
    lv_obj_set_style_text_color(msg, lv_color_hex(LV_COLOR_THEME_WIHTE), 0);
    lv_obj_set_style_text_font(msg, &lv_font_montserrat_20, 0);
    lv_obj_set_width(msg, 340);
    lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
    lv_obj_align(msg, LV_ALIGN_TOP_LEFT, 20, 72);

    /* Yes button */
    lv_obj_t *btn_yes = lv_button_create(system_ui.mbox);
    lv_obj_set_size(btn_yes, 110, 40);
    lv_obj_align(btn_yes, LV_ALIGN_BOTTOM_RIGHT, -40, -16);
    lv_obj_set_style_bg_color(btn_yes, lv_color_hex(LV_COLOR_WARM_RED), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn_yes, 20, 0);
    lv_obj_set_style_border_width(btn_yes, 0, 0);
    lv_obj_set_style_shadow_width(btn_yes, 0, 0);
    lv_obj_set_style_pad_all(btn_yes, 0, LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_yes, ship_mode_confirm_dialog, LV_EVENT_CLICKED, (void *)(uintptr_t)event_type);

    lv_obj_t *yes_label = lv_label_create(btn_yes);
    lv_label_set_text(yes_label, "Yes");
    lv_obj_set_style_text_color(yes_label, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(yes_label, &lv_font_montserrat_24, 0);
    lv_obj_center(yes_label);

    /* Cancel button */
    lv_obj_t *btn_cancel = lv_button_create(system_ui.mbox);
    lv_obj_set_size(btn_cancel, 110, 40);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_LEFT, 40, -16);
    lv_obj_set_style_bg_opa(btn_cancel, LV_OPA_20, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn_cancel, 20, 0);
    lv_obj_set_style_border_width(btn_cancel, 0, 0);
    lv_obj_set_style_shadow_width(btn_cancel, 0, 0);
    lv_obj_set_style_pad_all(btn_cancel, 0, LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_cancel, ship_mode_confirm_dialog, LV_EVENT_CLICKED, NULL);

    lv_obj_t *cancel_label = lv_label_create(btn_cancel);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_set_style_text_color(cancel_label, lv_color_hex(LV_COLOR_THEME_WIHTE), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(cancel_label, &lv_font_montserrat_24, 0);
    lv_obj_center(cancel_label);

    lv_group_add_obj(page_list[PAGE_SYSTEM].group, btn_cancel);
    lv_group_add_obj(page_list[PAGE_SYSTEM].group, btn_yes);
    lv_group_focus_obj(btn_yes);
}

static void system_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    int event_type = (int)lv_event_get_user_data(e);
    lv_obj_t *target_obj = lv_event_get_target(e);

    switch (code)
    {
    case LV_EVENT_CLICKED:
        switch (event_type)
        {
        case SYSTEM_EVENT_BLUETOOTH:
        {
            device_switch.bluetooth_enable = !device_switch.bluetooth_enable;
            if (device_switch.bluetooth_enable)
            {
                bt_interface_open_bt();
            }
            else
            {
                bt_interface_close_bt();
            }

            lv_obj_t *status = lv_obj_get_child(target_obj, -1);
            if (status)
            {
                lv_label_set_text(status, device_switch.bluetooth_enable ? "ON" : "OFF");
                lv_obj_set_style_text_color(status,
                                            device_switch.bluetooth_enable ? lv_color_hex(LV_COLOR_THEME_GREEN) : lv_color_hex(LV_COLOR_THEME_GRAY), 0);
            }
            break;
        }
        case SYSTEM_EVENT_WIFI:
        {
#ifdef PKG_USING_ESP32C6_AT_COMMAND
            device_switch.wifi_enable = !device_switch.wifi_enable;
            if (device_switch.wifi_enable)
            {
                esp32_at_cwjap(AT_RESP_ID_CWJAP, WIFI_SSID, WIFI_PASSWORD);
            }
            else
            {
                esp32_at_cwqap(AT_RESP_ID_CWQAP);
            }
            lv_obj_t *status = lv_obj_get_child(target_obj, -1);
            if (status)
            {
                lv_label_set_text(status, device_switch.wifi_enable ? "ON" : "OFF");
                lv_obj_set_style_text_color(status,
                                            device_switch.wifi_enable ? lv_color_hex(LV_COLOR_THEME_GREEN) : lv_color_hex(LV_COLOR_THEME_GRAY), 0);
            }
#endif
            break;
        }
        case SYSTEM_EVENT_KEY_LED:
        {
#ifdef PKG_USING_PKG_KEY_BOARD
            device_switch.keyboard_led_enable = !device_switch.keyboard_led_enable;
            if (device_switch.keyboard_led_enable)
            {
                aw21009_set_all_brightness(LED_BRIGHT_100);
            }
            else
            {
                aw21009_set_all_brightness(LED_BRIGHT_0);
            }
            lv_obj_t *status = lv_obj_get_child(target_obj, -1);
            if (status)
            {
                lv_label_set_text(status, device_switch.keyboard_led_enable ? "ON" : "OFF");
                lv_obj_set_style_text_color(status,
                                            device_switch.keyboard_led_enable ? lv_color_hex(LV_COLOR_THEME_GREEN) : lv_color_hex(LV_COLOR_THEME_GRAY), 0);
            }
#endif
            break;
        }
        case SYSTEM_EVENT_SHOTDOWN:
            page_list[PAGE_SYSTEM].last_group_obj = target_obj;
            show_system_confirm_dialog("Enter ShotDown?\nThis will power off the device,\nPress the PA34 button to activate",
                                       SYSTEM_EVENT_SHOTDOWN);
            break;
        case SYSTEM_EVENT_SHIP_MODE:
            page_list[PAGE_SYSTEM].last_group_obj = target_obj;
            show_system_confirm_dialog("Enter Ship Mode?\nThis will put the device into transport mode,\nand it requires a long press of the PWR button\nto activate it.",
                                       SYSTEM_EVENT_SHIP_MODE);
            break;
        case SYSTEM_EVENT_RESTART:
            page_list[PAGE_SYSTEM].last_group_obj = target_obj;
            show_system_confirm_dialog("Enter Reset?\nThis will reset the device",
                                       SYSTEM_EVENT_RESTART);
            break;
        }
        break;
    case LV_EVENT_VALUE_CHANGED:
        switch (event_type)
        {
        case SYSTEM_EVENT_BRIGHTNESS:
        {
            int32_t val = lv_slider_get_value(target_obj);
            rt_device_t lcd_device = rt_device_find("lcd");
            if (lcd_device)
            {
                uint8_t bri = (uint8_t)val;
                if (bri > 100)
                    bri = 100;
                else if (bri < 10)
                    bri = 10;
                rt_device_control(lcd_device, RTGRAPHIC_CTRL_SET_BRIGHTNESS, &bri);
            }
            lv_obj_t *parent = lv_obj_get_parent(target_obj);
            lv_obj_t *val_label = lv_obj_get_child(parent, -1);
            if (val_label)
            {
                lv_label_set_text_fmt(val_label, "%d%%", (int)val);
            }
            break;
        }
        case SYSTEM_EVENT_VOLUME:
        {
            int32_t val = lv_slider_get_value(target_obj);
            audio_server_set_private_volume(AUDIO_TYPE_BT_MUSIC, (uint8_t)val);
            audio_server_set_private_volume(AUDIO_TYPE_LOCAL_MUSIC, (uint8_t)val);
            audio_server_set_private_volume(AUDIO_TYPE_LOCAL_RECORD, (uint8_t)val);
            lv_obj_t *parent = lv_obj_get_parent(target_obj);
            lv_obj_t *val_label = lv_obj_get_child(parent, -1);
            if (val_label)
            {
                lv_label_set_text_fmt(val_label, "%d", (int)val);
            }
            break;
        }
        }
        break;
    }
}

/**********page manger***********/
static void page_on_enter(Page *page)
{
    log_d("page_on_enter: %d\n", page->id);
    if (!page)
        return;

    if (page->is_created == false)
    {
        page->root = lv_obj_create(NULL);
        lv_obj_add_style(current_page->root, &screen_style, 0);

        lv_obj_add_event_cb(page->root, gesture_event_cb, LV_EVENT_GESTURE, (void *)(uintptr_t)page->id);
        page->group = lv_group_create();
        lv_group_set_wrap(page->group, false);
        if (page->group_cb != NULL)
        {
            lv_group_set_focus_cb(page->group, page->group_cb);
        }
    }

    if (page->root)
    {
        page->on_create(page);
        page->is_created = true;
        page->timer_counter = 0;
    }

    if (page->id == PAGE_START)
    {
        lv_scr_load(page->root);
    }
    else if (page->id == PAGE_HOME)
    {
        lv_scr_load_anim(page->root, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
    }
    else
    {
        lv_scr_load_anim(page->root, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, false);
    }
}

static void page_on_leave(Page *page)
{
    if (!page)
        return;
    log_d("page_on_leave: %d\n", page->id);
    if (page->id < PAGE_SIZE)
    {
        if (page->root)
        {
            lv_obj_clean(page->root); // 清空页面内容
        }
        if (page->group)
        {
            lv_group_remove_all_objs(page->group);
        }
    }

    page->timer_counter = 0;

    switch (page->id)
    {
    case PAGE_HOME:
        home_ui.progress_bar = NULL;
        home_ui.status_bar_bluetooth_icon = NULL;
        home_ui.status_bar_time_label = NULL;
        home_ui.status_bar_battery_label = NULL;
        home_ui.status_bar_battery_icon = NULL;
        break;
    case PAGE_MUSIC:
        music_ui.music_name_label = NULL;
        music_ui.music_artist_label = NULL;
        music_ui.music_pause_btn = NULL;
        music_ui.music_pause_btn_label = NULL;
        music_ui.voide_end_time_label = NULL;
        music_ui.voide_current_time_label = NULL;
        music_ui.music_progress_bar = NULL;
        break;
    case PAGE_LORA:
        device_switch.lora_rx_enable = false;
        device_switch.lora_tx_enable = false;
        lora_tx_count = 0;
        radio_sleep();
        lora_ui.lora_config_cont = NULL;
        lora_ui.lora_send_recevice_msg_cont = NULL;
        lora_ui.tx_msg_label = NULL;
        lora_ui.rx_msg_label = NULL;
        lora_ui.rx_rssi_label = NULL;
        lora_ui.rx_snr_label = NULL;
        break;

    case PAGE_SENSOR:
#if defined(PKG_USING_BHI260AP)
        bhi260ap_thread_pause();
#endif
        sensor_ui.sensor_timer = NULL;
        sensor_ui.sensor_yaw_arc = NULL;
        sensor_ui.sensor_pitch_arc = NULL;
        sensor_ui.sensor_roll_arc = NULL;
        sensor_ui.sensor_step_arc = NULL;
        sensor_ui.sensor_yaw_label = NULL;
        sensor_ui.sensor_pitch_label = NULL;
        sensor_ui.sensor_roll_label = NULL;
        sensor_ui.sensor_step_label = NULL;
        sensor_ui.sensor_temp_label = NULL;
        break;

    case PAGE_CHARGE:
        charge_ui.charge_arc = NULL;
        charge_ui.batter_voltage_percentage_label = NULL;
        charge_ui.charge_status_label = NULL;
        charge_ui.batter_voltage_mv_label = NULL;
        for (int i = 0; i < 7; i++)
        {
            charge_ui.batter_status_label[i] = NULL;
        }
        break;

    case PAGE_WEATHER:
        weather_ui.weather_icon = NULL;
        weather_ui.last_update_label = NULL;
        weather_ui.weather_city_label = NULL;
        weather_ui.weather_temp_label = NULL;
        break;

    case PAGE_AUDIO:
        break;

    case PAGE_FILE:
        file_ui.file_list = NULL;
        file_ui.file_item = NULL;
        break;

    case PAGE_SYSTEM:
        break;
    }
}

static void page_on_destroy(Page *page)
{
    if (page->root)
    {
        lv_obj_del(page->root); // 销毁LVGL容器
        page->root = NULL;
        page->is_created = false;
    }

    if (page->timer)
    {
        lv_timer_del(page->timer); // 停止定时器
        page->timer = NULL;
    }

    if (page->group)
    {
        lv_group_del(page->group);
        page->group = NULL;
    }
}

static void switch_to_page(Page *page)
{
    if (page->id < 0 || page->id >= PAGE_SIZE)
        return;

    rt_mutex_take(page_mutex, RT_WAITING_FOREVER);
    last_page = current_page;
    current_page = page;
    log_d("switch_to_page: %d->%d\n", last_page->id, current_page->id);

    if (last_page->on_leave)
    {
        last_page->on_leave(last_page);
    }

    if (current_page->on_enter)
    {
        current_page->on_enter(current_page);
    }

#ifdef PKG_USING_PKG_KEY_BOARD
    lv_indev_set_group(kb_indev, page_list[current_page->id].group);
#endif

    rt_mutex_release(page_mutex);
}

static lv_obj_t *create_info_card(lv_obj_t *parent, const char *title, const char *value, lv_coord_t x, lv_coord_t y)
{
    // 创建卡片容器
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 160, 60);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_style_bg_color(card, lv_color_hex(LV_COLOR_THEME_DIM_GRAY), 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);

    // 创建标题标签
    lv_obj_t *title_label = lv_label_create(card);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 5);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title_label, lv_color_hex(LV_COLOR_THEME_GRAY), 0);

    // 创建值标签
    lv_obj_t *value_label = lv_label_create(card);
    lv_obj_align(value_label, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_label_set_text(value_label, value);
    lv_obj_set_style_text_font(value_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(value_label, lv_color_hex(LV_COLOR_THEME_WIHTE), 0);

    return card;
}

/*********anim***********/
static void anim_size_width_cb(void *var, int32_t v)
{
    lv_obj_set_width(var, v);
}

static void anim_size_height_cb(void *var, int32_t v)
{
    lv_obj_set_height(var, v);
}

static void anim_size_cb(void *var, int32_t v)
{
    lv_obj_set_size(var, v, v);
}

static void anim_x_cb(void *var, int32_t v)
{
    lv_obj_set_x(var, v);
}

static void anim_y_cb(void *var, int32_t v)
{
    lv_obj_set_y(var, v);
}

static void delete_obj_timer_cb(lv_timer_t *timer)
{
    lv_obj_t *obj = (lv_obj_t *)lv_timer_get_user_data(timer);
    page_on_destroy(&page_list[PAGE_START]);
}

static void anim_zoom(lv_obj_t *obj, uint32_t duration, uint32_t start_width, uint32_t end_width, uint32_t start_height, uint32_t end_height)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, start_width, end_width);
    lv_anim_set_exec_cb(&a, anim_size_width_cb);
    lv_anim_set_duration(&a, duration);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
    lv_anim_set_values(&a, start_height, end_height);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a, anim_size_height_cb);
    lv_anim_start(&a);
}

static void anim_height(lv_obj_t *obj, uint32_t duration, uint32_t start_height, uint32_t end_height)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, start_height, end_height);
    lv_anim_set_duration(&a, duration);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a, anim_size_height_cb);
    lv_anim_start(&a);
}

static void anim_y(lv_obj_t *obj, uint32_t duration, uint32_t start_y, uint32_t end_y)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, start_y, end_y);
    lv_anim_set_exec_cb(&a, anim_y_cb);
    lv_anim_set_duration(&a, duration);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

static void anim_x(lv_obj_t *obj, uint32_t duration, uint32_t start_x, uint32_t end_x)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, start_x, end_x);
    lv_anim_set_exec_cb(&a, anim_x_cb);
    lv_anim_set_duration(&a, duration);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

device_switch_t *get_device_switch(void)
{
    return &device_switch;
}
