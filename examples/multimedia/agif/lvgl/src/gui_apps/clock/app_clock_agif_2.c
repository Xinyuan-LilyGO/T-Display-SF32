#include <rtthread.h>
#include <rtdevice.h>
#include "littlevgl2rtt.h"
#include "lvgl.h"
#include "gui_app_fwk.h"

#include "app_clock_main.h"
#include "app_mem.h"
#include "log.h"
#include "agif.h"


/* Image decalration */
LV_IMG_DECLARE(agif_icon);

typedef struct
{
    lv_obj_t *gif;
    lv_timer_t *refresh_task;
} app_clock_agif_t;

static app_clock_agif_t *p_clk_agif = NULL;


static void agif_loop_end_func(void)
{
    LOG_I("%s", __func__);
}

static void agif_refresh_timer_cb(struct _lv_timer_t *t)
{
    /* Next frame. */
    int ret = lv_gif_dec_next_frame(p_clk_agif->gif);

    /* if ret == 0, it means that reach the last frame. */
    if (0 == ret)
    {
        /* Playback complete. */
        agif_loop_end_func();
        /* Replay it. */
        lv_gif_dec_restart(p_clk_agif->gif);
    }
}

static rt_int32_t resume_callback(void)
{
    /* Resume gif animation refresh */
    lv_timer_resume(p_clk_agif->refresh_task);
    return RT_EOK;
}

static rt_int32_t pause_callback(void)
{
    /* Pause gif animation refresh */
    lv_timer_pause(p_clk_agif->refresh_task);
    return RT_EOK;
}

static rt_int32_t init(lv_obj_t *parent)
{
    p_clk_agif = (app_clock_agif_t *)rt_malloc(sizeof(app_clock_agif_t));
    RT_ASSERT(p_clk_agif);

    /* Create agif. */
    lv_color_t bg_color;
    p_clk_agif->gif = lv_gif_dec_create(parent, LV_EXT_IMG_GET(agif_icon), &bg_color, LV_COLOR_DEPTH);
    RT_ASSERT(p_clk_agif->gif);
    lv_obj_align(p_clk_agif->gif, LV_ALIGN_CENTER, 0, 0);

    /* Pause gif animation refresh */
    lv_gif_dec_task_pause(p_clk_agif->gif, 0);

    /* Create lv timer to refresh gif. */
    p_clk_agif->refresh_task = lv_timer_create(agif_refresh_timer_cb, 16, RT_NULL);
    RT_ASSERT(p_clk_agif->refresh_task);

    return RT_EOK;
}


static rt_int32_t deinit(void)
{
    if (p_clk_agif->refresh_task)
    {
        lv_timer_del(p_clk_agif->refresh_task);
        p_clk_agif->refresh_task = NULL;
    }
    if (p_clk_agif->gif)
    {
        /* Release gif context. */
        lv_gif_dec_destroy(p_clk_agif->gif);
        p_clk_agif->gif = NULL;
    }

    rt_free(p_clk_agif);
    p_clk_agif = NULL;

    return RT_EOK;
}

static const app_clock_ops_t ops =
{
    .init = init,
    .pause = pause_callback,
    .resume = resume_callback,
    .deinit = deinit,
};

void app_clock_agif02_register(void)
{
    app_clock_register("agif02", &ops);
}
