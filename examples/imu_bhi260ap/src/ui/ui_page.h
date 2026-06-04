#ifndef UI_PAGE_H
#define UI_PAGE_H

#include "lvgl.h"
#include "littlevgl2rtt.h"

#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum
    {
        PAGE_HOME = 0,
        PAGE_SLEEP,
        PAGE_DROP_DOWN,
        PAGE_LORA,
        PAGE_INFO,
        PAGE_MUSIC,
        PAGE_SD,
        PAGE_WEATHER,
        PAGE_SET,
        PAGE_SIZE,
    } PageType;

    typedef struct
    {
        union
        {
            enum
            {
                HOME_EVENT_LORA = 0,
                HOME_EVENT_INFO,
                HOME_EVENT_MUSIC,
                HOME_EVENT_SD,
                HOME_EVENT_WEATHER,
                HOME_EVENT_SET,
                // ...
            } home_event;
            enum
            {
                DROP_DOWN_EVENT_LAST_MUSIC = 0,
                DROP_DOWN_EVENT_NEXT_MUSIC,
                DROP_DOWN_EVENT_PALY_OR_HOLD_MUSIC,
                DROP_DOWN_EVENT_BLUETOOTH,
                DROP_DOWN_EVENT_DISPALY_BRIGHTNESS,
                DROP_DOWN_EVENT_SPEAKER_VOLUME,
                // ...
            } drop_down_event;
            enum
            {
                LORA_EVENT_MODE = 0,
                LORA_EVENT_FREQ,
                LORA_EVENT_POWER,
                LORA_EVENT_BW,
                LORA_EVENT_SF,
                LORA_EVENT_CR,
                LORA_EVENT_SYNC_WORD,
                LORA_EVENT_SEND_MESSAGE,
                LORA_EVENT_RECEIVE_MESSAGE,
            } lora_event;
            enum
            {
                MUSIC_EVENT_VOLUME = 0,
                MUSIC_EVENT_LIST,
                MUSIC_EVENT_PALY_OR_PAUSE,
                MUSIC_EVENT_NEXT,
                MUSIC_EVENT_LAST,
                MUSIC_EVENT_VOLUME_SLIDER,
            } music_event;
        };
    } PageEvent;

    typedef enum
    {
        GESTURE_NONE = 0,
        GESTURE_LEFT = (1 << 0),
        GESTURE_RIGHT = (1 << 1),
        GESTURE_UP = (1 << 2),
        GESTURE_DOWN = (1 << 3),
    } GestureDirection;

    typedef enum
    {
        LABEL_TIME,     // 时间标签
        LABEL_WEEKDAY,  // 星期标签
        LABEL_DATE,     // 日期标签
        LABEL_CALENDAR, // 日历标签
        LABEL_MOTTO     // 格言标签
    } LabelIdentifier;

    typedef struct PageElements
    {
        lv_obj_t *btn;        // 按钮对象
        lv_obj_t *label;      // 标签对象
        lv_obj_t *play_music_label;      // 标签对象
        lv_obj_t *music_volume_slider;      // 标签对象
        lv_obj_t *status_bar; // 标签对象
    } PageElements;

    typedef struct Page
    {
        // 核心对象
        lv_obj_t *root;        // 页面根容器
        lv_obj_t *title_label; // 页面标题控件
        // 标识信息
        uint8_t id;       // 页面唯一ID
        const char *name; // 页面名称（调试用）

        PageElements page_elements;
        // 状态管理
        bool is_created; // 是否已初始化
        bool is_active;  // 当前激活状态
        // 动画配置
        bool is_animating; // 动画进行中标志
        // 数据管理
        void *user_data;                    // 页面私有数据指针
        void (*data_loader)(struct Page *); // 数据加载回调

        // 生命周期回调
        void (*on_create)(struct Page *);  // 初始化回调
        void (*on_destroy)(struct Page *); // 销毁回调
        void (*on_enter)(struct Page *);   // 页面进入事件
        void (*on_leave)(struct Page *);   // 页面离开事件
    } Page;

    extern Page page_list[PAGE_SIZE];
    extern lv_obj_t *tv;
    extern lv_style_t screen_style;
    // extern rt_device_t lcd_device;

    void page_manager_init(void);
    void load_page(PageType page_type, bool is_animating, GestureDirection direction);

#ifdef __cplusplus
}
#endif

#endif /* UI_PAGE_H */
