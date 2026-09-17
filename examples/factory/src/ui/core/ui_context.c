#include "ui_context.h"

#include <string.h>

#define UI_BRIGHTNESS_DEFAULT 100U

static rt_mutex_t context_lock;
static ui_context_t shared_context;

static rt_err_t context_update(void (*update)(ui_context_t *context,
                                               const void *value),
                               const void *value)
{
    rt_err_t result;

    if (context_lock == RT_NULL || update == RT_NULL)
        return -RT_ERROR;

    result = rt_mutex_take(context_lock, RT_WAITING_FOREVER);
    if (result != RT_EOK)
        return result;

    update(&shared_context, value);
    rt_mutex_release(context_lock);
    return RT_EOK;
}

static void update_display_ready(ui_context_t *context, const void *value)
{
    context->display_ready = *(const bool *)value;
}

typedef struct
{
    ui_page_id_t current;
    ui_page_id_t previous;
} page_context_update_t;

static void update_page(ui_context_t *context, const void *value)
{
    const page_context_update_t *page = value;

    context->current_page = page->current;
    context->previous_page = page->previous;
}

static void update_brightness(ui_context_t *context, const void *value)
{
    context->brightness_percent = *(const uint8_t *)value;
}

typedef struct
{
    uint32_t error_mask;
    bool complete;
} init_status_update_t;

static void update_init_status(ui_context_t *context, const void *value)
{
    const init_status_update_t *status = value;

    context->init_error_mask = status->error_mask;
    context->initialization_complete = status->complete;
}

rt_err_t ui_context_init(void)
{
    if (context_lock != RT_NULL)
        return RT_EOK;

    context_lock = rt_mutex_create("ui_ctx", RT_IPC_FLAG_PRIO);
    if (context_lock == RT_NULL)
        return -RT_ENOMEM;

    memset(&shared_context, 0, sizeof(shared_context));
    shared_context.current_page = UI_PAGE_COUNT;
    shared_context.previous_page = UI_PAGE_COUNT;
    shared_context.brightness_percent = UI_BRIGHTNESS_DEFAULT;
    return RT_EOK;
}

rt_err_t ui_context_get(ui_context_t *context)
{
    rt_err_t result;

    if (context == RT_NULL)
        return -RT_EINVAL;
    if (context_lock == RT_NULL)
        return -RT_ERROR;

    result = rt_mutex_take(context_lock, RT_WAITING_FOREVER);
    if (result != RT_EOK)
        return result;

    *context = shared_context;
    rt_mutex_release(context_lock);
    return RT_EOK;
}

rt_err_t ui_context_set_display_ready(bool ready)
{
    return context_update(update_display_ready, &ready);
}

rt_err_t ui_context_set_page(ui_page_id_t current,
                             ui_page_id_t previous)
{
    const page_context_update_t page = {
        .current = current,
        .previous = previous,
    };

    return context_update(update_page, &page);
}

rt_err_t ui_context_set_brightness(uint8_t percent)
{
    if (percent > 100U)
        return -RT_EINVAL;

    return context_update(update_brightness, &percent);
}

rt_err_t ui_context_set_init_status(uint32_t error_mask, bool complete)
{
    const init_status_update_t status = {
        .error_mask = error_mask,
        .complete = complete,
    };

    return context_update(update_init_status, &status);
}
