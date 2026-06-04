#include "ui_page.h"
#include "ui.h"
#include "ui_theme.h"
#include <math.h>
#include <stdlib.h>
#include "rtthread.h"
#include "drv_touch.h"

static void home_page_on_create(struct Page *p);
static void sleep_page_on_create(struct Page *p);
static void drop_down_page_on_create(struct Page *p);
static void lora_page_on_create(struct Page *p);
static void info_page_on_create(struct Page *p);
static void music_page_on_create(struct Page *p);
static void sd_page_on_create(struct Page *p);
static void weather_page_on_create(struct Page *p);
static void set_page_on_create(struct Page *p);

void draw_status_bar(struct Page *p);
static void timer_cb(lv_timer_t *timer);
static void event_cb(lv_event_t *e);
static void gesture_event_cb(lv_event_t *e);
static void anim_out_cb_y(void *target, int32_t value);
static void anim_in_cb_y(void *target, int32_t value);
static void anim_complete_cb(lv_anim_t *anim);

lv_style_t screen_style;
PageType current_page = PAGE_HOME;

/*************Time**************/
lv_obj_t *time_label = NULL;
lv_obj_t *week_label = NULL;
lv_obj_t *data_label = NULL;
lv_obj_t *calendar_label = NULL;
lv_obj_t *motto_label = NULL;
lv_obj_t *flower_image = NULL;

/*************LVGL**************/
#define ICON_COUNT 6
// 菜单数据结构
typedef struct
{
    char *name;
    lv_obj_t *cont;
    lv_obj_t *btn;
    lv_obj_t *btn_label;
    const lv_image_dsc_t *btn_src;
    int id;
} MenuItem;

static MenuItem items[ICON_COUNT] = {
    {"lora", NULL, NULL, NULL, &_radio_RGB565A8_100x100, 0},
    {"info", NULL, NULL, NULL, &_info_RGB565A8_100x100, 1},
    {"music", NULL, NULL, NULL, &_music_RGB565A8_100x100, 2},
    {"sd", NULL, NULL, NULL, &_sdcard_RGB565A8_100x100, 3},
    {"weather", NULL, NULL, NULL, &_werther_RGB565A8_100x100, 4},
    {"ubw", NULL, NULL, NULL, &_hua_RGB565A8_100x100, 5},
};

// static lv_obj_t *menu_container;
static bool is_music_playing = false;
bool is_volume_slider_hidden = true;

Page page_list[PAGE_SIZE] = {
    [PAGE_HOME] = {
        .id = PAGE_HOME,
        .name = "Home Screen",
        .on_create = home_page_on_create,
        .is_created = false,
        .is_active = true,
        .is_animating = false,
    },
    [PAGE_SLEEP] = {
        .id = PAGE_SLEEP,
        .name = "Sleep Screen",
        .on_create = sleep_page_on_create,
        .is_created = false,
        .is_active = false,
        .is_animating = false,
    },
    [PAGE_DROP_DOWN] = {
        .id = PAGE_DROP_DOWN,
        .name = "Dropdown Screen",
        .on_create = drop_down_page_on_create,
        .is_created = false,
        .is_active = false,
        .is_animating = false,
    },
    [PAGE_LORA] = {
        .id = PAGE_LORA,
        .name = "Lora Screen",
        .on_create = lora_page_on_create,
        .is_created = false,
        .is_active = false,
        .is_animating = false,
    },
    [PAGE_INFO] = {
        .id = PAGE_INFO,
        .name = "Info Screen",
        .on_create = info_page_on_create,
        .is_created = false,
        .is_active = false,
        .is_animating = false,
    },
    [PAGE_MUSIC] = {
        .id = PAGE_MUSIC,
        .name = "Music Screen",
        .on_create = music_page_on_create,
        .is_created = false,
        .is_active = false,
        .is_animating = false,
    },
    [PAGE_SD] = {
        .id = PAGE_SD,
        .name = "SD Screen",
        .on_create = sd_page_on_create,
        .is_created = false,
        .is_active = false,
        .is_animating = false,
    },
    [PAGE_WEATHER] = {
        .id = PAGE_WEATHER,
        .name = "Weather Screen",
        .on_create = weather_page_on_create,
        .is_created = false,
        .is_active = false,
        .is_animating = false,
    },
    [PAGE_SET] = {
        .id = PAGE_SET,
        .name = "Set Screen",
        .on_create = set_page_on_create,
        .is_created = false,
        .is_active = false,
        .is_animating = false,
    },
};

// 新增全局手势处理函数
static void handle_global_gesture(lv_indev_t *indev, GestureDirection dir)
{
    printf("Detected gesture :%d\n", dir);

    switch (current_page)
    {
    case PAGE_HOME:
        if (dir == GESTURE_RIGHT) // Swipe right
        {
            load_page(PAGE_SLEEP, true, GESTURE_RIGHT); // 页面从左向右滑入
        }
        else if (dir == GESTURE_DOWN)
        {
            load_page(PAGE_DROP_DOWN, true, GESTURE_DOWN);
        }
        break;
    case PAGE_SLEEP:
        if (dir == GESTURE_LEFT)
        {
            load_page(PAGE_HOME, true, GESTURE_LEFT);
        }
        break;
    case PAGE_DROP_DOWN:
        if (dir == GESTURE_UP)
        {
            load_page(PAGE_HOME, true, GESTURE_UP);
        }
        break;
    case PAGE_LORA:
        if (dir == GESTURE_RIGHT)
        {
            load_page(PAGE_HOME, true, GESTURE_RIGHT);
        }
        break;
    case PAGE_INFO:
        if (dir == GESTURE_RIGHT)
        {
            load_page(PAGE_HOME, true, GESTURE_RIGHT);
        }
        break;
    case PAGE_MUSIC:
        if (dir == GESTURE_RIGHT)
        {
            load_page(PAGE_HOME, true, GESTURE_RIGHT);
        }
        break;
    case PAGE_SD:
        if (dir == GESTURE_RIGHT)
        {
            load_page(PAGE_HOME, true, GESTURE_RIGHT);
        }
        break;
    case PAGE_WEATHER:
        if (dir == GESTURE_RIGHT)
        {
            load_page(PAGE_HOME, true, GESTURE_RIGHT);
        }
        break;
    case PAGE_SET:
        if (dir == GESTURE_RIGHT)
        {
            load_page(PAGE_HOME, true, GESTURE_RIGHT);
        }
        break;
    }
}

void page_manager_init(void)
{
    lv_style_init(&screen_style);
    lv_style_set_size(&screen_style, lv_pct(100), lv_pct(100));
    lv_style_set_border_width(&screen_style, 0);
    lv_style_set_opa(&screen_style, LV_OPA_COVER);
    lv_style_set_bg_color(&screen_style, themes[theme_get_current_id()].bg_color);
    lv_style_set_text_font(&screen_style, themes[theme_get_current_id()].font_normal);
    lv_style_set_text_color(&screen_style, themes[theme_get_current_id()].font_color);

    for (int page_id = 0; page_id < PAGE_SIZE; page_id++)
    {
        Page *page = &page_list[page_id];
        if (page->on_create)
        {
            page->root = lv_obj_create(NULL),
            lv_obj_add_style(page->root, &screen_style, 0);
            lv_obj_add_event_cb(page->root, gesture_event_cb, LV_EVENT_ALL, (void *)page_id);
            lv_obj_add_flag(page->root, LV_OBJ_FLAG_EVENT_BUBBLE);
            page->on_create(page);
        }
        page->is_created = true;
    }
    lv_screen_load(page_list[current_page].root);

    lv_timer_create(timer_cb, 10, NULL); // 10ms
}

static void timer_cb(lv_timer_t *timer)
{
    static uint32_t sec = 0;
    static uint32_t min = 0;
    static uint32_t hour = 0;
    static uint32_t time = 0;
    time++;
    if (time % 100 == 0)
    {
        sec++;
        if (sec >= 60)
        {
            time = 0;
            sec = 0;
            min++;
            if (min >= 60)
            {
                min = 0;
                hour++;
                if (hour >= 24)
                {
                    hour = 0;
                }
            }
            lv_label_set_text_fmt(time_label, "%02d:%02d", hour, min);
            // lv_image_set_rotation(flower_image, sec * 12);
        }
    }
}

void load_page(PageType page_type, bool is_animating, GestureDirection direction)
{
    if (page_type < PAGE_SIZE && page_list[page_type].is_created && is_animating)
    {
        printf("current_page: %s\n", page_list[current_page].name);
        printf("load_page: %s\n", page_list[page_type].name);

        Page *old_page = &page_list[current_page];
        page_list[current_page].is_active = false;
        Page *new_page = &page_list[page_type];

        lv_scr_load_anim_t anim_type;
        if (direction == GESTURE_LEFT)
        {
            anim_type = LV_SCR_LOAD_ANIM_MOVE_LEFT;
            lv_scr_load_anim(new_page->root, anim_type, 300, 0, false);
        }
        else if (direction == GESTURE_RIGHT)
        {
            anim_type = LV_SCR_LOAD_ANIM_MOVE_RIGHT;
            lv_scr_load_anim(new_page->root, anim_type, 300, 0, false);
        }
        else if (direction == GESTURE_UP)
        {
            anim_type = LV_SCR_LOAD_ANIM_OUT_TOP;
            lv_scr_load_anim(new_page->root, anim_type, 300, 0, false);
        }
        else if (direction == GESTURE_DOWN)
        {
            anim_type = LV_SCR_LOAD_ANIM_OVER_BOTTOM;
            lv_scr_load_anim(new_page->root, anim_type, 300, 0, false);
        }
        else if (direction == GESTURE_NONE)
        {
            anim_type = LV_SCR_LOAD_ANIM_FADE_IN;
            lv_scr_load_anim(new_page->root, anim_type, 300, 0, false);
        }

        current_page = page_type;
        page_list[page_type].is_active = true;
    }
    else
    {
        printf("current_page: %s\n", page_list[current_page].name);
        printf("load_page: %s\n", page_list[page_type].name);

        Page *old_page = &page_list[current_page];
        Page *new_page = &page_list[page_type];

        lv_scr_load(page_list[current_page].root);
        current_page = page_type;
    }
}

/* 手势事件回调 */
static void gesture_event_cb(lv_event_t *e)
{
    static lv_point_t start_point;
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_get_act();
    // printf("gesture_event_cb: %d\n", code);
    switch (code)
    {
    case LV_EVENT_GESTURE:
    {
        printf("LV_EVENT_GESTURE\n");
        GestureDirection dir = (GestureDirection)lv_indev_get_gesture_dir(indev);
        handle_global_gesture(indev, dir);
        break;
    }
    }
}

static void home_page_on_create(struct Page *p)
{
    printf("home_page_on_create\n");

    lv_obj_set_size(p->root, LV_VER_RES, LV_HOR_RES);
    lv_obj_set_scrollbar_mode(p->root, LV_SCROLLBAR_MODE_OFF);

    draw_status_bar(p);

    lv_obj_t *tlile = lv_label_create(p->root);
    lv_label_set_long_mode(tlile, LV_LABEL_LONG_WRAP);
    lv_obj_align(tlile, LV_ALIGN_TOP_LEFT, 30, 60);
    lv_obj_set_size(tlile, 150, 30);
    lv_label_set_text(tlile, "MENU");

    lv_obj_t *btn = lv_btn_create(p->root);
    lv_obj_set_size(btn, 60, 60);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn, 30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_img_src(btn, &_set_RGB565A8_60x60, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, -50, 60);
    lv_obj_add_event_cb(btn, event_cb, LV_EVENT_CLICKED, (void *)HOME_EVENT_SET);

    static lv_style_t cont_style;
    lv_style_init(&cont_style);
    lv_style_set_border_width(&cont_style, 0);
    lv_style_set_bg_opa(&cont_style, LV_OPA_30);
    lv_style_set_pad_all(&cont_style, 0);
    lv_style_set_radius(&cont_style, 30);

    lv_obj_t *app_menu = lv_obj_create(p->root);
    lv_obj_set_size(app_menu, LV_VER_RES - 20, 350);
    lv_obj_set_style_border_width(app_menu, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(app_menu, 50, LV_PART_MAIN);
    lv_obj_set_style_pad_right(app_menu, 50, LV_PART_MAIN);
    lv_obj_set_style_pad_top(app_menu, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(app_menu, 10, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(app_menu, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(app_menu, 0, LV_PART_MAIN);
    lv_obj_align(app_menu, LV_ALIGN_BOTTOM_MID, 0, -10);

    for (int i = 0; i < ICON_COUNT; i++)
    {
        items[i].cont = lv_obj_create(app_menu);
        lv_obj_set_scrollbar_mode(items[i].cont, LV_LABEL_LONG_WRAP);
        lv_obj_add_style(items[i].cont, &cont_style, 0);
        lv_obj_set_size(items[i].cont, 150, 150);
        static uint8_t row_conut = 0;

        if (i % 2 == 0)
        {
            lv_obj_align(items[i].cont, LV_ALIGN_TOP_LEFT, 0, (row_conut * 180));
        }
        else
        {
            lv_obj_align(items[i].cont, LV_ALIGN_TOP_RIGHT, 0, (row_conut * 180));
            row_conut++; // 每次奇数时增加行计数
        }

        items[i].btn = lv_button_create(items[i].cont);
        lv_obj_set_size(items[i].btn, 100, 100);
        lv_obj_set_style_bg_opa(items[i].btn, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(items[i].btn, 50, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_width(items[i].btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_image_src(items[i].btn, items[i].btn_src, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(items[i].btn, LV_ALIGN_TOP_MID, 0, 10);
        lv_obj_add_event_cb(items[i].btn, event_cb, LV_EVENT_CLICKED, (void *)items[i].id);

        items[i].btn_label = lv_label_create(items[i].cont);
        lv_obj_set_size(items[i].btn_label, 150, 20);
        lv_label_set_text(items[i].btn_label, items[i].name);
        lv_obj_set_style_text_font(items[i].btn_label, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(items[i].btn_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_align(items[i].btn_label, LV_ALIGN_BOTTOM_MID, 0, -10);
    }
}

static void sleep_page_on_create(struct Page *p)
{
    printf("sleep_page_on_create\n");
    // p->page_elements.tileview = lv_tileview_add_tile(tv, 0, 0, LV_DIR_RIGHT);

    lv_obj_set_style_bg_color(p->root, (lv_color_t)LV_COLOR_BLACK, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(p->root, LV_OPA_COVER, LV_PART_MAIN);

    time_label = lv_label_create(p->root);
    lv_obj_set_pos(time_label, 0, 0);
    lv_obj_set_size(time_label, 480, 150);
    lv_label_set_text(time_label, "12:25");
    lv_label_set_long_mode(time_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(time_label, (lv_color_t)LV_COLOR_GREEN, LV_PART_MAIN);
    lv_obj_set_style_text_font(time_label, &lv_font_Alatsi_Regular_150, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(time_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    week_label = lv_label_create(p->root);
    lv_obj_set_pos(week_label, 0, 150);
    lv_obj_set_size(week_label, 480, 60);
    lv_label_set_text(week_label, "Monday");
    lv_obj_set_style_text_color(week_label, (lv_color_t)LV_COLOR_WHITE, LV_PART_MAIN);
    lv_obj_set_style_text_font(week_label, &lv_font_Acme_Regular_50, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(week_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    data_label = lv_label_create(p->root);
    lv_obj_set_pos(data_label, 0, 210);
    lv_obj_set_size(data_label, 480, 30);
    lv_label_set_text(data_label, "2025-08-04");
    lv_obj_set_style_text_color(data_label, (lv_color_t)LV_COLOR_WHITE, LV_PART_MAIN);
    lv_obj_set_style_text_font(data_label, &lv_font_Acme_Regular_30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(data_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    calendar_label = lv_label_create(p->root);
    lv_obj_set_pos(calendar_label, 0, 250);
    lv_obj_set_size(calendar_label, 480, 60);
    lv_label_set_text(calendar_label, "07/28");
    lv_obj_set_style_text_color(calendar_label, (lv_color_t)LV_COLOR_WHITE, LV_PART_MAIN);
    lv_obj_set_style_text_font(calendar_label, &lv_font_Acme_Regular_50, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(calendar_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    motto_label = lv_label_create(p->root);
    lv_obj_set_pos(motto_label, 0, 310);
    lv_obj_set_size(motto_label, 480, 40);
    lv_label_set_text(motto_label, "You have to love yourself");
    lv_obj_set_style_text_color(motto_label, (lv_color_t)LV_COLOR_WHITE, LV_PART_MAIN);
    lv_obj_set_style_text_font(motto_label, &lv_font_AguafinaScript_Regular_30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(motto_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    flower_image = lv_img_create(p->root); // 缓慢旋转
    lv_img_set_src(flower_image, &_hua_RGB565A8_120x120);
    lv_obj_set_pos(flower_image, (LV_HOR_RES - 120) / 2, 350);
    lv_obj_set_size(flower_image, 120, 120);
    lv_image_set_pivot(flower_image, 60, 60);
}

static void drop_down_page_on_create(struct Page *p)
{
    printf("drop_down_page_on_create\n");

    draw_status_bar(p);

    lv_obj_t *drop_cont = lv_obj_create(p->root);
    lv_obj_set_size(drop_cont, LV_VER_RES, LV_HOR_RES - 50);
    lv_obj_align(drop_cont, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_set_style_pad_all(drop_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(drop_cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(drop_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(drop_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(drop_cont, 0, LV_PART_MAIN);
    lv_obj_add_flag(drop_cont, LV_OBJ_FLAG_CLICKABLE);

    static lv_style_t drop_cont_style;
    lv_style_init(&drop_cont_style);
    lv_style_set_pad_all(&drop_cont_style, 0);
    lv_style_set_border_width(&drop_cont_style, 0);
    lv_style_set_bg_opa(&drop_cont_style, LV_OPA_30);
    lv_style_set_radius(&drop_cont_style, 30);

    lv_obj_t *music_cont = lv_obj_create(drop_cont);
    lv_obj_set_size(music_cont, 200, 200);
    lv_obj_align(music_cont, LV_ALIGN_TOP_LEFT, 20, 20);
    lv_obj_add_style(music_cont, &drop_cont_style, 0);

    lv_obj_t *img = lv_img_create(music_cont);
    lv_img_set_src(img, &_hua_RGB565A8_100x100);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, -30);

    lv_obj_t *last_btn = lv_obj_create(music_cont);
    lv_obj_set_size(last_btn, 40, 40);
    lv_obj_set_style_bg_img_src(last_btn, &_last_music_RGB565A8_40x40, 0);
    lv_obj_align(last_btn, LV_ALIGN_BOTTOM_LEFT, 10, -15);
    lv_obj_set_style_pad_all(last_btn, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(last_btn, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(last_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(last_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(last_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(last_btn, event_cb, LV_EVENT_CLICKED, (void *)DROP_DOWN_EVENT_LAST_MUSIC);

    lv_obj_t *hold_btn = lv_obj_create(music_cont);
    lv_obj_set_size(hold_btn, 40, 40);
    lv_obj_set_style_bg_img_src(hold_btn, &_24gl_pause_RGB565A8_40x40, 0);
    lv_obj_align(hold_btn, LV_ALIGN_BOTTOM_MID, 4, -15);
    lv_obj_set_style_pad_all(hold_btn, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(hold_btn, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(hold_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(hold_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(hold_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(hold_btn, event_cb, LV_EVENT_CLICKED, (void *)DROP_DOWN_EVENT_PALY_OR_HOLD_MUSIC);

    lv_obj_t *next_btn = lv_obj_create(music_cont);
    lv_obj_set_size(next_btn, 40, 40);
    lv_obj_set_style_bg_img_src(next_btn, &_next_music_RGB565A8_40x40, 0);
    lv_obj_align(next_btn, LV_ALIGN_BOTTOM_RIGHT, -10, -15);
    lv_obj_set_style_pad_all(next_btn, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(next_btn, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(next_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(next_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(next_btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(next_btn, event_cb, LV_EVENT_CLICKED, (void *)DROP_DOWN_EVENT_NEXT_MUSIC);

    lv_obj_t *bluetooth_cont = lv_obj_create(drop_cont);
    lv_obj_set_size(bluetooth_cont, 200, 90);
    lv_obj_align(bluetooth_cont, LV_ALIGN_TOP_RIGHT, -20, 20);
    lv_obj_add_style(bluetooth_cont, &drop_cont_style, 0);

    lv_obj_t *bluetooth_icon = lv_label_create(bluetooth_cont);
    lv_label_set_text(bluetooth_icon, "" LV_SYMBOL_BLUETOOTH "");
    lv_obj_set_style_text_font(bluetooth_icon, &lv_font_montserrat_48, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(bluetooth_icon, lv_color_hex(0x0000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(bluetooth_icon, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(bluetooth_icon, LV_OPA_50, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(bluetooth_icon, LV_ALIGN_LEFT_MID, 20, 0);
    // lv_obj_set_style_pad_all(bluetooth_icon, 0, LV_STATE_DEFAULT);
    lv_obj_add_event_cb(bluetooth_icon, event_cb, LV_EVENT_CLICKED, (void *)DROP_DOWN_EVENT_BLUETOOTH);

    lv_obj_t *bule_label = lv_label_create(bluetooth_cont);
    lv_obj_set_size(bule_label, 110, 30);
    lv_obj_set_style_text_font(bule_label, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(bule_label, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_set_style_text_align(bule_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(bule_label, "bluetooth");

    lv_obj_t *wifi_cont = lv_obj_create(drop_cont);
    lv_obj_set_size(wifi_cont, 200, 90);
    lv_obj_align(wifi_cont, LV_ALIGN_TOP_RIGHT, -20, 20 + 110);
    lv_obj_add_style(wifi_cont, &drop_cont_style, 0);

    lv_obj_t *wifi_img = lv_img_create(wifi_cont);
    lv_img_set_src(wifi_img, &_hua_RGB565A8_60x60);
    lv_obj_align(wifi_img, LV_ALIGN_LEFT_MID, 10, 0);

    lv_obj_t *wifi_label = lv_label_create(wifi_cont);
    lv_obj_set_size(wifi_label, 110, 30);
    lv_obj_set_style_text_font(wifi_label, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(wifi_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(wifi_label, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_label_set_text(wifi_label, "wifi");

    lv_obj_t *slider_cont = lv_obj_create(drop_cont);
    lv_obj_add_style(slider_cont, &drop_cont_style, 0);
    lv_obj_set_size(slider_cont, 440, 160);
    lv_obj_align(slider_cont, LV_ALIGN_CENTER, 0, 100);
    lv_obj_set_scrollbar_mode(slider_cont, LV_SCROLLBAR_MODE_OFF);

    // Write codes screen_slider_light
    lv_obj_t *screen_slider_light = lv_slider_create(slider_cont);
    lv_obj_set_size(screen_slider_light, 400, 40);
    lv_obj_align(screen_slider_light, LV_ALIGN_TOP_LEFT, 20, 20);

    lv_slider_set_range(screen_slider_light, 0, 100);
    lv_slider_set_mode(screen_slider_light, LV_SLIDER_MODE_NORMAL);
    lv_slider_set_value(screen_slider_light, 80, LV_ANIM_OFF);

    // Write style for screen_slider_light, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_bg_opa(screen_slider_light, LV_OPA_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(screen_slider_light, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(screen_slider_light, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(screen_slider_light, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(screen_slider_light, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(screen_slider_light, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Write style for screen_slider_light, Part: LV_PART_INDICATOR, State: LV_STATE_DEFAULT.
    lv_obj_set_style_bg_opa(screen_slider_light, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(screen_slider_light, lv_color_hex(0xffffff), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(screen_slider_light, LV_GRAD_DIR_NONE, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(screen_slider_light, 20, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    // Write style for screen_slider_light, Part: LV_PART_KNOB, State: LV_STATE_DEFAULT.
    lv_obj_set_style_bg_opa(screen_slider_light, 0, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(screen_slider_light, 0, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(screen_slider_light, event_cb, LV_EVENT_VALUE_CHANGED, (void *)DROP_DOWN_EVENT_DISPALY_BRIGHTNESS);

    // Write codes screen_label_light
    lv_obj_t *screen_label_light = lv_label_create(screen_slider_light);
    lv_obj_align(screen_label_light, LV_ALIGN_LEFT_MID, 20, 5);
    lv_obj_set_size(screen_label_light, 100, 32);
    lv_label_set_text(screen_label_light, "Light");
    lv_obj_set_style_text_font(screen_label_light, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_long_mode(screen_label_light, LV_LABEL_LONG_WRAP);

    // Write codes screen_slider_video
    lv_obj_t *screen_slider_video = lv_slider_create(slider_cont);
    lv_obj_set_size(screen_slider_video, 400, 40);
    lv_obj_align(screen_slider_video, LV_ALIGN_BOTTOM_LEFT, 20, -20);

    lv_slider_set_range(screen_slider_video, 0, 100);
    lv_slider_set_mode(screen_slider_video, LV_SLIDER_MODE_NORMAL);
    lv_slider_set_value(screen_slider_video, 50, LV_ANIM_OFF);

    // Write style for screen_slider_video, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_bg_opa(screen_slider_video, LV_OPA_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(screen_slider_video, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(screen_slider_video, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(screen_slider_video, 25, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(screen_slider_video, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(screen_slider_video, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    // Write style for screen_slider_video, Part: LV_PART_INDICATOR, State: LV_STATE_DEFAULT.
    lv_obj_set_style_bg_opa(screen_slider_video, 255, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(screen_slider_video, lv_color_hex(0xffffff), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(screen_slider_video, LV_GRAD_DIR_NONE, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(screen_slider_video, 25, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    // Write style for screen_slider_video, Part: LV_PART_KNOB, State: LV_STATE_DEFAULT.
    lv_obj_set_style_bg_opa(screen_slider_video, 0, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(screen_slider_video, 0, LV_PART_KNOB | LV_STATE_DEFAULT);

    // Write codes screen_label_video
    lv_obj_t *screen_label_video = lv_label_create(screen_slider_video);
    lv_obj_align(screen_label_video, LV_ALIGN_LEFT_MID, 20, 5);
    lv_obj_set_size(screen_label_video, 100, 32);
    lv_label_set_text(screen_label_video, "Video");
    lv_obj_set_style_text_font(screen_label_video, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_long_mode(screen_label_video, LV_LABEL_LONG_WRAP);
}

static void lora_page_on_create(struct Page *p)
{
    printf("lora_page_on_create\n");

    lv_obj_t *page_tile = lv_label_create(p->root);
    lv_obj_set_size(page_tile, LV_PCT(50), 30);
    lv_obj_align(page_tile, LV_ALIGN_TOP_MID, 0, 10);
    lv_label_set_text(page_tile, "Lora Config");
    lv_obj_set_style_text_font(page_tile, &lv_font_montserrat_36, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(page_tile, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    static lv_style_t config_cont_style;
    lv_style_init(&config_cont_style);
    lv_style_set_border_width(&config_cont_style, 0);
    lv_style_set_bg_opa(&config_cont_style, LV_OPA_30);
    lv_style_set_pad_all(&config_cont_style, 0);
    lv_style_set_pad_bottom(&config_cont_style, 40);
    lv_style_set_radius(&config_cont_style, 50);

    lv_obj_t *config_cont = lv_obj_create(p->root);
    lv_obj_set_size(config_cont, 460, 320);
    lv_obj_align(config_cont, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_add_style(config_cont, &config_cont_style, 0);

    char *lora_config_label_text[7] = {"Mode", "Freq", "Power", "BW", "SF", "CR", "SyncWord"};
    char *lora_config_ddlist_text[7] = {"LORA\nFSK",
                                        "433Mhz\n868Mhz\n915Mhz",
                                        "22dBm\n14dBm\n0dBm\n-9dBm",
                                        "125kHz\n500kHz",
                                        "7\n12",
                                        "5\n6",
                                        "0x12\n0x34"};
    for (int i = 0; i < sizeof(lora_config_label_text) / sizeof(lora_config_label_text[0]); i++)
    {
        lv_obj_t *label = lv_label_create(config_cont);
        lv_obj_set_size(label, 200, 36);
        lv_obj_align(label, LV_ALIGN_TOP_LEFT, 10, 50 + i * 80);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(label, lora_config_label_text[i]);

        lv_obj_t *lora_mdoe_ddlist = lv_dropdown_create(config_cont);
        lv_obj_set_size(lora_mdoe_ddlist, 200, 50);
        lv_obj_align(lora_mdoe_ddlist, LV_ALIGN_TOP_RIGHT, -40, 40 + i * 80);
        lv_dropdown_set_options(lora_mdoe_ddlist, lora_config_ddlist_text[i]);

        lv_obj_set_style_text_font(lora_mdoe_ddlist, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(lora_mdoe_ddlist, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_opa(lora_mdoe_ddlist, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(lora_mdoe_ddlist, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_opa(lora_mdoe_ddlist, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(lora_mdoe_ddlist, lv_color_hex(0xe1e6ee), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_side(lora_mdoe_ddlist, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_top(lora_mdoe_ddlist, 15, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_left(lora_mdoe_ddlist, 50, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_right(lora_mdoe_ddlist, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(lora_mdoe_ddlist, 15, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(lora_mdoe_ddlist, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(lora_mdoe_ddlist, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_grad_dir(lora_mdoe_ddlist, LV_GRAD_DIR_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_shadow_width(lora_mdoe_ddlist, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_add_event_cb(lora_mdoe_ddlist, event_cb, LV_EVENT_VALUE_CHANGED, (void *)(LORA_EVENT_MODE + i));
    }

    static lv_style_t btn_style;
    lv_style_init(&btn_style);
    lv_style_set_bg_opa(&btn_style, LV_OPA_COVER);
    lv_style_set_radius(&btn_style, 25);
    lv_style_set_pad_all(&btn_style, 0);
    lv_style_set_border_width(&btn_style, 0);
    lv_style_set_shadow_width(&btn_style, 0);

    lv_obj_t *lora_send_btn = lv_btn_create(p->root);
    lv_obj_set_size(lora_send_btn, 150, 50);
    lv_obj_add_style(lora_send_btn, &btn_style, 0);
    lv_obj_align(lora_send_btn, LV_ALIGN_BOTTOM_LEFT, 50, -30);
    lv_obj_set_style_bg_color(lora_send_btn, (lv_color_t)LV_COLOR_GREEN_1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_t *lora_send_label = lv_label_create(lora_send_btn);
    lv_label_set_text(lora_send_label, "Send");
    lv_obj_align(lora_send_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(lora_send_label, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(lora_send_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(lora_send_label, event_cb, LV_EVENT_VALUE_CHANGED, (void *)(LORA_EVENT_SEND_MESSAGE));

    lv_obj_t *lora_receive_btn = lv_btn_create(p->root);
    lv_obj_set_size(lora_receive_btn, 150, 50);
    lv_obj_align(lora_receive_btn, LV_ALIGN_BOTTOM_RIGHT, -50, -30);
    lv_obj_add_style(lora_receive_btn, &btn_style, 0);
    lv_obj_t *lora_receive_label = lv_label_create(lora_receive_btn);
    lv_obj_align(lora_receive_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(lora_receive_label, "Receive");
    lv_obj_set_style_text_font(lora_receive_label, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(lora_receive_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(lora_receive_label, event_cb, LV_EVENT_VALUE_CHANGED, (void *)(LORA_EVENT_RECEIVE_MESSAGE));
}

static void info_page_on_create(struct Page *p)
{
    printf("info_page_on_create\n");
    lv_obj_t *page_tile = lv_label_create(p->root);
    lv_obj_set_size(page_tile, LV_PCT(50), 30);
    lv_obj_align(page_tile, LV_ALIGN_TOP_MID, 0, 10);
    lv_label_set_text(page_tile, "Info");
    lv_obj_set_style_text_font(page_tile, &lv_font_montserrat_36, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(page_tile, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    static lv_style_t info_cont_style;
    lv_style_init(&info_cont_style);
    lv_style_set_border_width(&info_cont_style, 0);
    lv_style_set_bg_opa(&info_cont_style, LV_OPA_30);
    lv_style_set_pad_all(&info_cont_style, 0);
    lv_style_set_pad_top(&info_cont_style, 0);
    lv_style_set_pad_bottom(&info_cont_style, 0);
    lv_style_set_radius(&info_cont_style, 50);

    lv_obj_t *info_cont = lv_obj_create(p->root);
    lv_obj_set_size(info_cont, 460, 400);
    lv_obj_align(info_cont, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_add_style(info_cont, &info_cont_style, 0);

    char *info_label_text[6] = {"Device Name:", "Firmware Version:", "Hareware Version:", "Battery:", "Flash Size:", "Resolution:"};
    char *info_value_text[6] = {"T-Display-SF32", "V1.0.0", "V1.0.0", "3800mAh", "16Mb", "480x480"};
    for (int i = 0; i < sizeof(info_label_text) / sizeof(info_label_text[0]); i++)
    {
        lv_obj_t *cont = lv_obj_create(info_cont);
        lv_obj_set_style_pad_all(cont, 0, LV_PART_MAIN);
        lv_obj_set_size(cont, 440, 50);
        lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);
        lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 30 + i * 50 + i * 10);

        lv_obj_t *label = lv_label_create(cont);
        lv_label_set_text(label, info_label_text[i]);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 5, 0);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_obj_t *value = lv_label_create(cont);
        lv_label_set_text(value, info_value_text[i]);
        lv_obj_align(value, LV_ALIGN_RIGHT_MID, -5, 0);
        lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(value, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static void music_page_on_create(struct Page *p)
{
    printf("music_page_on_create\n");
    lv_obj_t *page_tile = lv_label_create(p->root);
    lv_obj_set_size(page_tile, LV_PCT(50), 30);
    lv_obj_align(page_tile, LV_ALIGN_TOP_MID, 0, 10);
    lv_label_set_text(page_tile, "Music Play");
    lv_obj_set_style_text_font(page_tile, &lv_font_montserrat_36, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(page_tile, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    static lv_style_t music_btn_style;
    lv_style_init(&music_btn_style);
    lv_style_set_size(&music_btn_style, 80, 80);
    lv_style_set_radius(&music_btn_style, 40);
    lv_style_set_text_font(&music_btn_style, &lv_font_montserrat_36);
    lv_style_set_bg_opa(&music_btn_style, LV_OPA_TRANSP);
    lv_style_set_shadow_width(&music_btn_style, 0);

    lv_obj_t *music_volume_btn = lv_btn_create(p->root);
    lv_obj_add_style(music_volume_btn, &music_btn_style, 0);
    lv_obj_align(music_volume_btn, LV_ALIGN_TOP_LEFT, 10, 30);
    lv_obj_add_event_cb(music_volume_btn, event_cb, LV_EVENT_CLICKED, (void *)(MUSIC_EVENT_VOLUME));
    lv_obj_t *music_volume_label = lv_label_create(music_volume_btn);
    lv_label_set_text(music_volume_label, " " LV_SYMBOL_VOLUME_MAX " ");
    lv_obj_align(music_volume_label, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *music_list_btn = lv_btn_create(p->root);
    lv_obj_add_style(music_list_btn, &music_btn_style, 0);
    lv_obj_align(music_list_btn, LV_ALIGN_TOP_RIGHT, -10, 30);
    lv_obj_add_event_cb(music_list_btn, event_cb, LV_EVENT_CLICKED, (void *)(MUSIC_EVENT_LIST));
    lv_obj_t *music_list_label = lv_label_create(music_list_btn);
    lv_label_set_text(music_list_label, " " LV_SYMBOL_BARS " ");
    lv_obj_align(music_list_label, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *music_last_btn = lv_btn_create(p->root);
    lv_obj_add_style(music_last_btn, &music_btn_style, 0);
    lv_obj_align(music_last_btn, LV_ALIGN_BOTTOM_LEFT, 20, -40);
    lv_obj_set_style_border_width(music_last_btn, 1, 0);
    lv_obj_set_style_border_color(music_last_btn, lv_color_hex(0xd0d0d0), 0);
    lv_obj_set_style_border_side(music_last_btn, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(music_last_btn, 1, 0);
    lv_obj_set_style_shadow_color(music_last_btn, lv_color_hex(0xb7b7b7), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(music_last_btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(music_last_btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(music_last_btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(music_last_btn, event_cb, LV_EVENT_CLICKED, (void *)(MUSIC_EVENT_LAST));
    lv_obj_t *music_last_label = lv_label_create(music_last_btn);
    lv_label_set_text(music_last_label, " " LV_SYMBOL_LEFT " ");
    lv_obj_align(music_last_label, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *music_playorpause_btn = lv_btn_create(p->root);
    lv_obj_add_style(music_playorpause_btn, &music_btn_style, 0);
    lv_obj_align(music_playorpause_btn, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_set_style_border_width(music_playorpause_btn, 1, 0);
    lv_obj_set_style_border_color(music_playorpause_btn, lv_color_hex(0xd0d0d0), 0);
    lv_obj_set_style_border_side(music_playorpause_btn, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(music_playorpause_btn, 1, 0);
    lv_obj_set_style_shadow_color(music_playorpause_btn, lv_color_hex(0xb7b7b7), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(music_playorpause_btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(music_playorpause_btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(music_playorpause_btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(music_playorpause_btn, event_cb, LV_EVENT_CLICKED, (void *)(MUSIC_EVENT_PALY_OR_PAUSE));
    p->page_elements.play_music_label = lv_label_create(music_playorpause_btn);
    lv_label_set_text(p->page_elements.play_music_label, " " LV_SYMBOL_PLAY " ");
    lv_obj_align(p->page_elements.play_music_label, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *music_next_btn = lv_btn_create(p->root);
    lv_obj_add_style(music_next_btn, &music_btn_style, 0);
    lv_obj_set_style_border_width(music_next_btn, 1, 0);
    lv_obj_set_style_border_color(music_next_btn, lv_color_hex(0xd0d0d0), 0);
    lv_obj_set_style_border_side(music_next_btn, LV_BORDER_SIDE_FULL, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(music_next_btn, 1, 0);
    lv_obj_set_style_shadow_color(music_next_btn, lv_color_hex(0xb7b7b7), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(music_next_btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_x(music_next_btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_offset_y(music_next_btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(music_next_btn, LV_ALIGN_BOTTOM_RIGHT, -20, -40);
    lv_obj_add_event_cb(music_next_btn, event_cb, LV_EVENT_CLICKED, (void *)(MUSIC_EVENT_NEXT));
    lv_obj_t *music_next_label = lv_label_create(music_next_btn);
    lv_label_set_text(music_next_label, " " LV_SYMBOL_RIGHT " ");
    lv_obj_align(music_next_label, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *music_name = lv_label_create(p->root);
    lv_obj_set_size(music_name, 300, 40);
    lv_obj_align(music_name, LV_ALIGN_TOP_MID, 0, 100);
    lv_label_set_text(music_name, "No Music");
    lv_label_set_long_mode(music_name, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(music_name, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(music_name, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(music_name, 1, 0);
    lv_obj_set_style_radius(music_name, 20, LV_PART_MAIN);
    lv_obj_set_style_border_color(music_name, lv_color_hex(0xd0d0d0), 0);

    p->page_elements.music_volume_slider = lv_slider_create(p->root);
    // lv_obj_add_flag(p->page_elements.music_volume_slider, LV_OBJ_FLAG_HIDDEN); // hidden by default
    lv_obj_set_style_opa(p->page_elements.music_volume_slider, LV_OPA_TRANSP, 0);
    lv_obj_set_size(p->page_elements.music_volume_slider, 40, 240);
    lv_obj_align(p->page_elements.music_volume_slider, LV_ALIGN_TOP_LEFT, 25, 100);
    lv_slider_set_range(p->page_elements.music_volume_slider, 0, 100);
    lv_slider_set_mode(p->page_elements.music_volume_slider, LV_SLIDER_MODE_NORMAL);
    lv_slider_set_value(p->page_elements.music_volume_slider, 80, LV_ANIM_OFF);
    lv_obj_add_event_cb(p->page_elements.music_volume_slider, event_cb, LV_EVENT_VALUE_CHANGED, (void *)(MUSIC_EVENT_VOLUME_SLIDER));

    lv_obj_set_style_bg_color(p->page_elements.music_volume_slider, lv_color_hex(0xacacac), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(p->page_elements.music_volume_slider, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(p->page_elements.music_volume_slider, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(p->page_elements.music_volume_slider, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_opa(p->page_elements.music_volume_slider, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_color(p->page_elements.music_volume_slider, lv_color_hex(0xcfcfcf), LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_style_bg_color(p->page_elements.music_volume_slider, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(p->page_elements.music_volume_slider, 10, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(p->page_elements.music_volume_slider, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_DEFAULT);

    lv_obj_set_style_bg_opa(p->page_elements.music_volume_slider, 0, LV_PART_KNOB | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(p->page_elements.music_volume_slider, 0, LV_PART_KNOB | LV_STATE_DEFAULT);

    // music list
}

static void sd_page_on_create(struct Page *p)
{
    printf("sd_page_on_create\n");
    lv_obj_t *page_tile = lv_label_create(p->root);
    lv_obj_set_size(page_tile, LV_PCT(50), 30);
    lv_obj_align(page_tile, LV_ALIGN_TOP_MID, 0, 10);
    lv_label_set_text(page_tile, "File System");
    lv_obj_set_style_text_font(page_tile, &lv_font_montserrat_36, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(page_tile, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void weather_page_on_create(struct Page *p)
{
    printf("weather_page_on_create\n");
    lv_obj_t *page_tile = lv_label_create(p->root);
    lv_obj_set_size(page_tile, LV_PCT(50), 30);
    lv_obj_align(page_tile, LV_ALIGN_TOP_MID, 0, 10);
    lv_label_set_text(page_tile, "Weather");
    lv_obj_set_style_text_font(page_tile, &lv_font_montserrat_36, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(page_tile, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *weather_cont = lv_obj_create(p->root);
    lv_obj_set_size(weather_cont, 460, 400);
    lv_obj_align(weather_cont, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_radius(weather_cont, 40, LV_PART_MAIN);
    lv_obj_set_style_pad_all(weather_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(weather_cont, 0, 0);

    lv_obj_t *weather_date = lv_label_create(weather_cont);
    lv_obj_set_size(weather_date, 200, 30);
    lv_obj_align(weather_date, LV_ALIGN_TOP_MID, 0, 10);
    lv_label_set_text(weather_date, "" LV_SYMBOL_HOME " 7 days");
    lv_obj_set_style_text_font(weather_date, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(weather_date, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *weather_icon = lv_img_create(weather_cont);
    lv_obj_set_size(weather_icon, 120, 120);
    lv_obj_align(weather_icon, LV_ALIGN_TOP_LEFT, 50, 100);
    lv_obj_set_style_bg_img_src(weather_icon, &_weather1_RGB565A8_120x120, 0);

    lv_obj_t *local_label = lv_label_create(weather_cont);
    lv_obj_set_size(local_label, 200, 30);
    lv_obj_align(local_label, LV_ALIGN_TOP_LEFT, 200, 100);
    lv_label_set_text(local_label, "Shenzhen , CN");
    lv_obj_set_style_text_font(local_label, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(local_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *temp_label = lv_label_create(weather_cont);
    lv_obj_set_size(temp_label, 200, 70);
    lv_obj_align(temp_label, LV_ALIGN_TOP_LEFT, 200, 140);
    lv_label_set_text(temp_label, "28°C");
    lv_obj_set_style_text_font(temp_label, &lv_font_montserrat_48, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(temp_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);

    char *weather_data[3] = {"wind", "hunmidity", "chance of rain"};
    const lv_image_dsc_t *weather_data_icon_dsc[] = {&_wind_RGB565A8_50x40, &_humidity_RGB565A8_50x50, &_rain_RGB565A8_50x50};
    char *weather_value[3] = {"10km/h", "80%", "60%"};

    for (int i = 0; i < 3; i++)
    {
        lv_obj_t *weather_data_cont = lv_obj_create(weather_cont);
        lv_obj_set_size(weather_data_cont, 120, 140);
        lv_obj_align(weather_data_cont, LV_ALIGN_TOP_LEFT, 20 + i * 150, 240);
        lv_obj_set_style_pad_all(weather_data_cont, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(weather_data_cont, 20, LV_PART_MAIN);
        lv_obj_set_style_bg_color(weather_data_cont, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(weather_data_cont, LV_OPA_50, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(weather_data_cont, 0, 0);
        lv_obj_set_style_shadow_color(weather_data_cont, lv_color_hex(0xABABAB), 0);
        lv_obj_set_style_shadow_width(weather_data_cont, 1, 0);
        lv_obj_set_style_shadow_spread(weather_data_cont, 2, 0);
        lv_obj_set_style_shadow_ofs_x(weather_data_cont, 2, 0);
        lv_obj_set_style_shadow_ofs_y(weather_data_cont, 2, 0);
        lv_obj_set_scrollbar_mode(weather_data_cont, LV_SCROLLBAR_MODE_OFF);

        lv_obj_t *weather_data_icon = lv_img_create(weather_data_cont);
        lv_obj_align(weather_data_icon, LV_ALIGN_TOP_MID, 0, 10);
        lv_img_set_src(weather_data_icon, weather_data_icon_dsc[i]);
        lv_obj_set_style_img_recolor(weather_data_icon, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_img_recolor_opa(weather_data_icon, LV_OPA_50, LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_obj_t *weather_data_value = lv_label_create(weather_data_cont);
        lv_obj_set_size(weather_data_value, 100, 30);
        lv_obj_align(weather_data_value, LV_ALIGN_TOP_MID, 0, 70);
        lv_label_set_text(weather_data_value, weather_value[i]);
        lv_obj_set_style_text_font(weather_data_value, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(weather_data_value, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_obj_t *weather_data_label = lv_label_create(weather_data_cont);
        lv_obj_set_height(weather_data_label, 30);
        lv_obj_set_width(weather_data_label, 100);
        lv_label_set_long_mode(weather_data_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_align(weather_data_label, LV_ALIGN_TOP_MID, 0, 100);
        lv_label_set_text(weather_data_label, weather_data[i]);
        lv_obj_set_style_text_font(weather_data_label, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(weather_data_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_opa(weather_data_label, LV_OPA_50, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static void set_page_on_create(struct Page *p)
{
    printf("set_page_on_create\n");
    lv_obj_t *page_tile = lv_label_create(p->root);
    lv_obj_set_size(page_tile, LV_PCT(50), 30);
    lv_obj_align(page_tile, LV_ALIGN_TOP_MID, 0, 10);
    lv_label_set_text(page_tile, "Set");
    lv_obj_set_style_text_font(page_tile, &lv_font_montserrat_36, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(page_tile, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *set_cont = lv_obj_create(p->root);
    lv_obj_set_size(set_cont, 460, 400);
    lv_obj_align(set_cont, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_radius(set_cont, 40, LV_PART_MAIN);
    lv_obj_set_style_pad_all(set_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(set_cont, 0, 0);
}

void draw_status_bar(struct Page *p)
{
    p->page_elements.status_bar = lv_obj_create(p->root);
    lv_obj_set_size(p->page_elements.status_bar, LV_PCT(100), 50);
    lv_obj_set_scrollbar_mode(p->page_elements.status_bar, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_border_width(p->page_elements.status_bar, 0, 0);
    lv_obj_set_style_radius(p->page_elements.status_bar, 5, LV_PART_MAIN);

    lv_obj_set_layout(p->page_elements.status_bar, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(p->page_elements.status_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(p->page_elements.status_bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(p->page_elements.status_bar, 5, 0);
    lv_obj_set_style_pad_left(p->page_elements.status_bar, 200, 0);
    lv_obj_set_style_pad_right(p->page_elements.status_bar, 50, 0);
    lv_obj_set_style_pad_top(p->page_elements.status_bar, 5, 0);
    lv_obj_set_style_pad_bottom(p->page_elements.status_bar, 5, 0);

    lv_obj_t *time_label = lv_label_create(p->page_elements.status_bar);

    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(time_label, "12:00");

    lv_obj_t *battery_bar = lv_bar_create(p->page_elements.status_bar);
    lv_obj_set_style_radius(battery_bar, 2, LV_PART_MAIN);
    lv_obj_set_style_radius(battery_bar, 0, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_size(battery_bar, 50, 25);
    lv_bar_set_value(battery_bar, 70, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(battery_bar, lv_color_hex(0x4CAF50), LV_PART_INDICATOR);

    lv_obj_t *single_cont = lv_obj_create(p->page_elements.status_bar);
    lv_obj_set_size(single_cont, 35, 35);
    lv_obj_set_style_bg_opa(single_cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(single_cont, 0, 0);
    lv_obj_set_style_pad_all(single_cont, 0, 0);
    lv_obj_set_scrollbar_mode(single_cont, LV_SCROLLBAR_MODE_OFF);

    for (int i = 0; i <= 3; i++)
    {
        lv_obj_t *icon = lv_obj_create(single_cont);
        lv_obj_set_style_pad_all(icon, 0, LV_PART_MAIN);
        lv_obj_set_style_border_width(icon, 0, 0);
        lv_obj_set_size(icon, 5, i * 5 + 15);
        lv_obj_set_pos(icon, i * 7 + 5, 35 - (i * 5 + 15));
        lv_obj_set_style_bg_color(icon, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static void event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *target_obj = lv_event_get_target(e);
    int event_id = (int)lv_event_get_user_data(e);
    // printf("event_id: %d\n", event_id);
    switch (current_page)
    {
    case PAGE_HOME:
        switch (code)
        {
        case LV_EVENT_CLICKED:
            if (event_id == HOME_EVENT_LORA)
            {
                load_page(PAGE_LORA, true, GESTURE_LEFT);
            }
            if (event_id == HOME_EVENT_INFO)
            {
                load_page(PAGE_INFO, true, GESTURE_LEFT);
            }
            if (event_id == HOME_EVENT_MUSIC)
            {
                load_page(PAGE_MUSIC, true, GESTURE_LEFT);
            }
            if (event_id == HOME_EVENT_SD)
            {
                load_page(PAGE_SD, true, GESTURE_LEFT);
            }
            if (event_id == HOME_EVENT_WEATHER)
            {
                load_page(PAGE_WEATHER, true, GESTURE_LEFT);
            }
            if (event_id == HOME_EVENT_SET)
            {
                load_page(PAGE_SET, true, GESTURE_NONE);
            }
            break;

        default:
            break;
        }
        break;

    case PAGE_DROP_DOWN:
        switch (code)
        {
        case LV_EVENT_CLICKED:
            if (event_id == DROP_DOWN_EVENT_PALY_OR_HOLD_MUSIC)
            {
                is_music_playing = !is_music_playing;
                if (is_music_playing)
                {
                    lv_obj_set_style_bg_img_src(target_obj, &_24gl_pause_RGB565A8_40x40, 0);
                }
                else
                {
                    lv_obj_set_style_bg_img_src(target_obj, &_24gl_play_RGB565A8_40x40, 0);
                }
            }

            break;

        case LV_EVENT_VALUE_CHANGED:
            if (event_id == DROP_DOWN_EVENT_DISPALY_BRIGHTNESS)
            {
                rt_device_t lcd_device = rt_device_find("lcd");
                uint8_t brightness = lv_slider_get_value(target_obj);
                rt_device_control(lcd_device, RTGRAPHIC_CTRL_SET_BRIGHTNESS,
                                  &brightness);
            }

            break;

        default:
            break;
        }
        break;
    case PAGE_LORA:
        switch (code)
        {
        case LV_EVENT_VALUE_CHANGED:
            if (event_id == LORA_EVENT_MODE)
            {
                char str[32]; // 定义足够大的缓冲区
                lv_dropdown_get_selected_str(target_obj, str, sizeof(str));
                printf("Lora mode: %s\n", str);
            }
            else if (event_id == LORA_EVENT_FREQ)
            {
                char str[32]; // 定义足够大的缓冲区
                lv_dropdown_get_selected_str(target_obj, str, sizeof(str));
                printf("Lora freq: %s\n", str);
            }
            else if (event_id == LORA_EVENT_POWER)
            {
                char str[32]; // 定义足够大的缓冲区
                lv_dropdown_get_selected_str(target_obj, str, sizeof(str));
                printf("Lora power: %s\n", str);
            }
            else if (event_id == LORA_EVENT_BW)
            {
                char str[32]; // 定义足够大的缓冲区
                lv_dropdown_get_selected_str(target_obj, str, sizeof(str));
                printf("Lora bw: %s\n", str);
            }
            else if (event_id == LORA_EVENT_SF)
            {
                char str[32]; // 定义足够大的缓冲区
                lv_dropdown_get_selected_str(target_obj, str, sizeof(str));
                printf("Lora sf: %s\n", str);
            }
            else if (event_id == LORA_EVENT_CR)
            {
                char str[32]; // 定义足够大的缓冲区
                lv_dropdown_get_selected_str(target_obj, str, sizeof(str));
                printf("Lora cr: %s\n", str);
            }
            else if (event_id == LORA_EVENT_SYNC_WORD)
            {
                char str[32]; // 定义足够大的缓冲区
                lv_dropdown_get_selected_str(target_obj, str, sizeof(str));
                printf("Lora sync_word: %s\n", str);
            }
            break;
        case LV_EVENT_CLICKED:
            if (event_id = LORA_EVENT_SEND_MESSAGE)
            {
                printf("Lora Send Message\n");
            }
            break;
        }
    case PAGE_MUSIC:
        switch (code)
        {
        case LV_EVENT_CLICKED:
            if (event_id == MUSIC_EVENT_PALY_OR_PAUSE)
            {
                is_music_playing = !is_music_playing;
                if (is_music_playing)
                {
                    printf("MUSIC_EVENT_PAUSE\n");
                    lv_label_set_text(page_list[current_page].page_elements.play_music_label, " " LV_SYMBOL_PAUSE " ");
                }
                else
                {
                    printf("MUSIC_EVENT_PALY\n");
                    lv_label_set_text(page_list[current_page].page_elements.play_music_label, " " LV_SYMBOL_PLAY " ");
                }
            }
            else if (event_id == MUSIC_EVENT_NEXT)
            {
                printf("MUSIC_EVENT_NEXT\n");
            }
            else if (event_id == MUSIC_EVENT_LAST)
            {
                printf("MUSIC_EVENT_LAST\n");
            }
            else if (event_id == MUSIC_EVENT_VOLUME)
            {
                printf("MUSIC_EVENT_VOLUME\n");
                is_volume_slider_hidden = false;
                if (!is_volume_slider_hidden)
                {
                    lv_anim_t anim_cover_opa;
                    lv_anim_init(&anim_cover_opa);
                    lv_anim_set_var(&anim_cover_opa, page_list[current_page].page_elements.music_volume_slider);
                    lv_anim_set_time(&anim_cover_opa, 300);
                    lv_anim_set_exec_cb(&anim_cover_opa, (lv_anim_exec_xcb_t)lv_obj_set_style_opa);
                    lv_anim_set_values(&anim_cover_opa, LV_OPA_TRANSP, LV_OPA_COVER);
                    lv_anim_set_ready_cb(&anim_cover_opa, anim_complete_cb);
                    lv_anim_set_user_data(&anim_cover_opa, MUSIC_EVENT_VOLUME);
                    lv_anim_start(&anim_cover_opa);
                }
            }
            else if (event_id == MUSIC_EVENT_LIST)
            {
                printf("MUSIC_EVENT_LIST\n");
            }

            break;
        case LV_EVENT_VALUE_CHANGED:
            if (event_id == MUSIC_EVENT_VOLUME_SLIDER)
            {
            }
        default:
        }
        break;
    }
}

static void anim_out_cb_y(void *target, int32_t value)
{
    lv_obj_t *obj = (lv_obj_t *)target;
    lv_obj_set_y(obj, value);
}

static void anim_in_cb_y(void *target, int32_t value)
{
    lv_obj_t *obj = (lv_obj_t *)target;
    lv_obj_set_y(obj, value);
}

static void anim_set_opa_cb(void *target, int32_t value)
{
    lv_obj_t *obj = (lv_obj_t *)target;
    lv_obj_set_style_opa(obj, value, LV_PART_MAIN);
}

static void anim_complete_cb(lv_anim_t *anim)
{
    int event = (int)anim->user_data;
    printf("anim_complete_cb %d\n", event);
    switch (event)
    {
    case MUSIC_EVENT_VOLUME:
        printf("MUSIC_EVENT_VOLUME\n");
        lv_anim_t anim_transp;
        lv_anim_init(&anim_transp);
        lv_anim_set_var(&anim_transp, page_list[current_page].page_elements.music_volume_slider);
        lv_anim_set_time(&anim_transp, 300);
        lv_anim_set_delay(&anim_transp, 3000);
        lv_anim_set_exec_cb(&anim_transp, (lv_anim_exec_xcb_t)anim_set_opa_cb);
        lv_anim_set_values(&anim_transp, LV_OPA_COVER, LV_OPA_TRANSP);
        lv_anim_start(&anim_transp);
        is_volume_slider_hidden = true;

        break;
    }
}
