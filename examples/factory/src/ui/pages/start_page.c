#include "start_page.h"

#include "ui_config.h"
#include "ui_navigation.h"
#include "ui_theme.h"

#define START_PAGE_BRAND_Y_INITIAL 225
#define START_PAGE_BRAND_Y_FINAL   165
#define START_PAGE_PROGRESS_WIDTH  240

static start_page_complete_callback_t completion_callback;
static void *completion_user_data;

static void start_page_set_width(void *object, int32_t width)
{
    lv_obj_set_width((lv_obj_t *)object, width);
}

static void start_page_set_y(void *object, int32_t y)
{
    lv_obj_set_y((lv_obj_t *)object, y);
}

static void start_page_set_opacity(void *object, int32_t opacity)
{
    lv_obj_set_style_opa((lv_obj_t *)object, (lv_opa_t)opacity, 0);
}

static void start_page_animation_completed(lv_anim_t *animation)
{
    (void)animation;
    if (completion_callback != RT_NULL)
        completion_callback(completion_user_data);
}

static void start_page_animate(lv_obj_t *object,
                               lv_anim_exec_xcb_t callback,
                               int32_t start_value,
                               int32_t end_value,
                               uint32_t delay_ms,
                               uint32_t duration_ms,
                               lv_anim_path_cb_t path,
                               lv_anim_completed_cb_t completed_cb)
{
    lv_anim_t animation;

    lv_anim_init(&animation);
    lv_anim_set_var(&animation, object);
    lv_anim_set_values(&animation, start_value, end_value);
    lv_anim_set_delay(&animation, delay_ms);
    lv_anim_set_duration(&animation, duration_ms);
    lv_anim_set_exec_cb(&animation, callback);
    lv_anim_set_path_cb(&animation, path);
    if (completed_cb != RT_NULL)
        lv_anim_set_completed_cb(&animation, completed_cb);
    lv_anim_start(&animation);
}

static lv_obj_t *start_page_create_label(lv_obj_t *parent,
                                         const char *text,
                                         const lv_font_t *font,
                                         uint32_t color,
                                         lv_opa_t opacity)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_obj_set_width(label, UI_SCREEN_WIDTH - 48);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(label, 0, 0);
    lv_obj_set_style_pad_all(label, 0, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_letter_space(label, 0, 0);
    lv_obj_set_style_opa(label, opacity, 0);
    return label;
}

static void start_page_on_create(ui_page_t *page)
{
    lv_obj_t *brand;
    lv_obj_t *product;
    lv_obj_t *progress_track;
    lv_obj_t *progress_fill;
    lv_obj_t *mode;

    if (page == RT_NULL || page->root == RT_NULL)
        return;

    lv_obj_set_style_bg_color(page->root,
                              lv_color_hex(UI_COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(page->root, LV_OPA_COVER, 0);
    lv_obj_set_scrollbar_mode(page->root, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(page->root, LV_DIR_NONE);

    brand = start_page_create_label(page->root, "LILYGO",
                                    &lv_font_montserrat_48,
                                    UI_COLOR_TEXT, LV_OPA_COVER);
    lv_obj_set_height(brand, 64);
    lv_obj_set_pos(brand, 24, START_PAGE_BRAND_Y_INITIAL);

    product = start_page_create_label(page->root, "T-SF32 DISPLAY",
                                      &lv_font_montserrat_20,
                                      UI_COLOR_TEXT, LV_OPA_TRANSP);
    lv_obj_set_height(product, 30);
    lv_obj_set_pos(product, 24, 238);

    progress_track = lv_obj_create(page->root);
    lv_obj_set_size(progress_track, START_PAGE_PROGRESS_WIDTH, 4);
    lv_obj_set_pos(progress_track,
                   (UI_SCREEN_WIDTH - START_PAGE_PROGRESS_WIDTH) / 2, 284);
    lv_obj_set_style_bg_color(progress_track,
                              lv_color_hex(UI_COLOR_TRACK), 0);
    lv_obj_set_style_bg_opa(progress_track, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(progress_track, 0, 0);
    lv_obj_set_style_radius(progress_track, 0, 0);
    lv_obj_set_style_pad_all(progress_track, 0, 0);
    lv_obj_remove_flag(progress_track, LV_OBJ_FLAG_SCROLLABLE);

    progress_fill = lv_obj_create(progress_track);
    lv_obj_set_size(progress_fill, 0, 4);
    lv_obj_set_pos(progress_fill, 0, 0);
    lv_obj_set_style_bg_color(progress_fill,
                              lv_color_hex(UI_COLOR_PRIMARY), 0);
    lv_obj_set_style_bg_opa(progress_fill, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(progress_fill, 0, 0);
    lv_obj_set_style_radius(progress_fill, 0, 0);
    lv_obj_set_style_pad_all(progress_fill, 0, 0);
    lv_obj_remove_flag(progress_fill, LV_OBJ_FLAG_SCROLLABLE);

    mode = start_page_create_label(page->root, "FACTORY TEST",
                                   &lv_font_montserrat_16,
                                   UI_COLOR_MUTED, LV_OPA_TRANSP);
    lv_obj_set_height(mode, 24);
    lv_obj_set_pos(mode, 24, 310);

    start_page_animate(brand, start_page_set_y,
                       START_PAGE_BRAND_Y_INITIAL,
                       START_PAGE_BRAND_Y_FINAL,
                       150, 1100, lv_anim_path_overshoot, RT_NULL);
    start_page_animate(progress_fill, start_page_set_width,
                       0, START_PAGE_PROGRESS_WIDTH,
                       250, 1800, lv_anim_path_ease_in_out,
                       start_page_animation_completed);
    start_page_animate(product, start_page_set_opacity,
                       LV_OPA_TRANSP, LV_OPA_COVER,
                       850, 700, lv_anim_path_ease_out, RT_NULL);
    start_page_animate(mode, start_page_set_opacity,
                       LV_OPA_TRANSP, 180,
                       1200, 700, lv_anim_path_ease_out, RT_NULL);
}

rt_err_t start_page_register(void)
{
    const ui_page_t page = {
        .id = UI_PAGE_START,
        .app_id = UI_APP_NONE,
        .name = "Start Page",
        .on_create = start_page_on_create,
        .timer_interval_ms = 0,
    };

    return ui_navigation_register_page(&page);
}

void start_page_set_complete_callback(start_page_complete_callback_t callback,
                                      void *user_data)
{
    completion_callback = callback;
    completion_user_data = user_data;
}
