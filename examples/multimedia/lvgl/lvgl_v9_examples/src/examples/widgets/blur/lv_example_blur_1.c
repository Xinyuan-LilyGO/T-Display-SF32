#include "../../lv_examples.h"
#if LV_USE_IMAGE && LV_BUILD_EXAMPLES

#ifdef BSP_USING_NN_ACC
#include "bf0_lib.h"
#include "drv_epic.h"

static uint32_t gauss_done = 0;
static void done_cb(void)
{
    rt_kprintf("gauss_done_cb \n");
    gauss_done = 1;
}

#define BLUR_RGB_FORMAT 1
static lv_obj_t *label;
static void *p_gauss;
static BlurDataType blur_in, blur_out;
static void anim_x_cb(void *var, int32_t v)
{
    gauss_done = 0;
    p_gauss = gauss_init(&blur_in, &blur_out, (uint16_t)v);
    gauss_start_IT(p_gauss, done_cb);

    while (0 == gauss_done) rt_thread_mdelay(10);
    gauss_deinit(p_gauss);

    char buf[8];
    lv_snprintf(buf, sizeof(buf), "%d", v);
    lv_label_set_text(label, buf);

    lv_obj_invalidate(var);

}

void lv_example_blur_1(void)
{
    LV_IMAGE_DECLARE(img_cogwheel_argb);

#if BLUR_RGB_FORMAT
    blur_in.color_mode = EPIC_COLOR_ARGB8888;
    blur_in.width = img_cogwheel_argb.header.w;
    blur_in.height = img_cogwheel_argb.header.h;
    blur_in.data = (uint8_t *) img_cogwheel_argb.data;

    blur_out.color_mode = blur_in.color_mode;
    blur_out.width = blur_in.width;
    blur_out.height = blur_in.height;
    blur_out.data = (uint8_t *) rt_malloc(img_cogwheel_argb.data_size);

    static lv_image_dsc_t blur_in_image;
    static lv_image_dsc_t blur_out_image;
    lv_memcpy(&blur_in_image, &img_cogwheel_argb, sizeof(blur_in_image));
    lv_memcpy(&blur_out_image, &img_cogwheel_argb, sizeof(blur_out_image));
#else
    blur_in.color_mode = EPIC_COLOR_A8;
    blur_in.width = img_cogwheel_argb.header.w;
    blur_in.height = img_cogwheel_argb.header.h;
    blur_in.data = (uint8_t *) rt_malloc(blur_in.width * blur_in.height);

    for (uint32_t i = 0; i < img_cogwheel_argb.header.h; i++)
        for (uint32_t j = 0; j < img_cogwheel_argb.header.w; j++)
            blur_in.data[i * blur_in.width + j] = img_cogwheel_argb.data[i * img_cogwheel_argb.header.stride + j * 4 + 3];

    blur_out.color_mode = blur_in.color_mode;
    blur_out.width = blur_in.width;
    blur_out.height = blur_in.height;
    blur_out.data = (uint8_t *) rt_malloc(blur_out.width * blur_out.height);



    static lv_image_dsc_t blur_in_image =
    {
        .header.magic = LV_IMAGE_HEADER_MAGIC,
        .header.flags = 0,
        .header.w = 100,
        .header.h = 100,
        .header.stride = 100,
        .header.cf = LV_COLOR_FORMAT_A8,
        .data_size = 100 * 100,
    };
    static lv_image_dsc_t blur_out_image =
    {
        .header.magic = LV_IMAGE_HEADER_MAGIC,
        .header.flags = 0,
        .header.w = 100,
        .header.h = 100,
        .header.stride = 100,
        .header.cf = LV_COLOR_FORMAT_A8,
        .data_size = 100 * 100,
    };
#endif
    blur_in_image.data = blur_in.data;
    blur_out_image.data = blur_out.data;

    lv_obj_t *img1 = lv_image_create(lv_screen_active());
    lv_image_set_src(img1, &blur_in_image);
    lv_obj_align(img1, LV_ALIGN_TOP_MID, 0, 20);







    lv_obj_t *img2 = lv_image_create(lv_screen_active());
    lv_image_set_src(img2, &blur_out_image);
    lv_obj_align_to(img2, img1, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);



    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_bg_color(&style, lv_color_white());
    lv_style_set_bg_opa(&style, LV_OPA_COVER);
#if BLUR_RGB_FORMAT

#else
    lv_style_set_image_recolor_opa(&style, LV_OPA_COVER);
    lv_style_set_image_recolor(&style, lv_palette_main(LV_PALETTE_RED));
#endif

    lv_obj_add_style(img1, &style, 0);
    lv_obj_add_style(img2, &style, 0);



    /*Create a label below the slider*/
    label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, "0");

    lv_obj_align_to(label, img2, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);


    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, img2);
    lv_anim_set_values(&a, 1, 80);
    lv_anim_set_duration(&a, 6000);
    lv_anim_set_exec_cb(&a, anim_x_cb);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_set_repeat_count(&a, 10);
    lv_anim_set_repeat_delay(&a, 1000);
    lv_anim_set_playback_duration(&a, 6000);
    lv_anim_set_playback_delay(&a, 1000);
    //lv_anim_set_path_cb(&a, lv_anim_path_bounce);
    lv_anim_start(&a);

}
#endif

#endif
