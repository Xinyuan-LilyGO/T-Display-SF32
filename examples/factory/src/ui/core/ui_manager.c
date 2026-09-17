#include "ui_manager.h"

#include <stdint.h>

#include <lvgl.h>
#include <ulog.h>

#include "ui_config.h"
#include "ui_context.h"
#include "ui_event.h"
#include "ui_keypad.h"
#include "ui_navigation.h"
#include "ui_status_bar.h"
#include "ui_theme.h"

extern rt_err_t littlevgl2rtt_init(const char *name);

#if defined(LVGL_VERSION_MAJOR) && (LVGL_VERSION_MAJOR >= 8)
#define UI_LVGL_HANDLER lv_timer_handler
#else
#define UI_LVGL_HANDLER lv_task_handler
#endif

static rt_thread_t ui_thread;
static rt_mutex_t ui_mutex;
static bool ui_ready;
static bool ui_init_attempted;
static rt_err_t ui_init_result = -RT_ERROR;
static bool ui_suspended;

static bool ui_manager_is_ui_thread(void)
{
    return ui_thread != RT_NULL && rt_thread_self() == ui_thread;
}

void ui_manager_lock(void)
{
    if (ui_mutex != RT_NULL && !ui_manager_is_ui_thread())
        rt_mutex_take(ui_mutex, RT_WAITING_FOREVER);
}

void ui_manager_unlock(void)
{
    if (ui_mutex != RT_NULL && !ui_manager_is_ui_thread())
        rt_mutex_release(ui_mutex);
}

static uint32_t ui_manager_ticks_to_ms(rt_tick_t ticks)
{
    return (uint32_t)(((uint64_t)ticks * 1000ULL) /
                      RT_TICK_PER_SECOND);
}

static void ui_manager_process_events(void)
{
    ui_event_t event;

    while (ui_event_receive(&event, 0) == RT_EOK)
    {
        rt_err_t result;

        if (event.type == UI_EVENT_KEY)
        {
            ui_page_id_t page_id = ui_navigation_get_current_page_id();
            uint32_t raw_key = (uint32_t)event.data & UI_EVENT_KEY_CODE_MASK;
            bool long_pressed = ((uint32_t)event.data &
                                 UI_EVENT_KEY_LONG_PRESSED) != 0U;

            /* The input diagnostics page consumes raw input events. Do not
             * let the same physical key navigate away or change focus. */
            if (page_id == UI_PAGE_INPUT)
                continue;

            if (page_id == UI_PAGE_HOME)
            {
                event.data = (uintptr_t)ui_keypad_translate_key(
                    raw_key);
            }
            else
            {
                ui_page_t *page = ui_navigation_get_current_page();
                uint32_t mapped_key = ui_keypad_translate_key(
                    raw_key);
                bool editing = page != RT_NULL &&
                               page->group != RT_NULL &&
                               lv_group_get_editing(page->group);

                if (editing &&
                    ui_keypad_handle_text_key(raw_key, long_pressed))
                {
                    continue;
                }
                if (editing)
                {
                    if (mapped_key == LV_KEY_PREV)
                        mapped_key = LV_KEY_LEFT;
                    else if (mapped_key == LV_KEY_NEXT)
                        mapped_key = LV_KEY_RIGHT;
                }
                else
                {
                    if (mapped_key == LV_KEY_UP)
                        mapped_key = LV_KEY_PREV;
                    else if (mapped_key == LV_KEY_DOWN)
                        mapped_key = LV_KEY_NEXT;
                }
                event.data = (uintptr_t)ui_keypad_handle_mapped_key(
                    mapped_key);
            }
            if (event.data == 0U)
                continue;
        }

        result = ui_status_bar_handle_event(&event);
        if (result != RT_EOK)
        {
            LOG_E("Update status bar for event %d failed: %d",
                  event.type, result);
        }

        result = ui_navigation_handle_event(&event);

        if (result != RT_EOK)
            LOG_E("Handle UI event %d failed: %d", event.type, result);
    }
}

static void ui_manager_thread_entry(void *parameter)
{
    rt_tick_t last_tick = rt_tick_get();

    (void)parameter;

    while (1)
    {
        rt_tick_t current_tick;
        uint32_t elapsed_ms;
        uint32_t delay_ms;

        ui_manager_lock();
        ui_manager_process_events();

        current_tick = rt_tick_get();
        elapsed_ms = ui_manager_ticks_to_ms(current_tick - last_tick);
        if (elapsed_ms > 0U)
        {
            last_tick = current_tick;
            if (!ui_suspended)
                ui_navigation_tick(elapsed_ms);
        }

        delay_ms = ui_suspended ? UI_SUSPENDED_DELAY_MS : UI_LVGL_HANDLER();
        ui_manager_unlock();

        if (delay_ms < UI_HANDLER_MIN_DELAY_MS)
            delay_ms = UI_HANDLER_MIN_DELAY_MS;
        else if (delay_ms > UI_HANDLER_MAX_DELAY_MS)
            delay_ms = UI_HANDLER_MAX_DELAY_MS;

        rt_thread_mdelay(delay_ms);
    }
}

rt_err_t ui_manager_init(void)
{
    rt_err_t result;

    if (ui_ready)
        return RT_EOK;
    if (ui_init_attempted)
        return ui_init_result;

    ui_init_attempted = true;

    result = littlevgl2rtt_init(UI_DISPLAY_DEVICE_NAME);
    if (result != RT_EOK)
    {
        LOG_E("Initialize LVGL display '%s' failed: %d",
              UI_DISPLAY_DEVICE_NAME, result);
        goto exit;
    }

    ui_mutex = rt_mutex_create("ui_lock", RT_IPC_FLAG_FIFO);
    if (ui_mutex == RT_NULL)
    {
        result = -RT_ENOMEM;
        goto exit;
    }

    result = ui_context_init();
    if (result != RT_EOK)
        goto exit;

    result = ui_event_init();
    if (result != RT_EOK)
        goto exit;

    result = ui_theme_init();
    if (result != RT_EOK)
        goto exit;

    result = ui_keypad_init();
    if (result != RT_EOK)
    {
        LOG_E("LVGL keypad initialization failed: %d", result);
        goto exit;
    }

    result = ui_status_bar_init();
    if (result != RT_EOK)
    {
        LOG_E("Global status bar initialization failed: %d", result);
        goto exit;
    }

    ui_manager_lock();
    result = ui_navigation_init();
    ui_manager_unlock();
    if (result != RT_EOK)
        goto exit;

    ui_thread = rt_thread_create("ui_task", ui_manager_thread_entry,
                                 RT_NULL, UI_TASK_STACK_SIZE,
                                 UI_TASK_PRIORITY, UI_TASK_TICK);
    if (ui_thread == RT_NULL)
    {
        result = -RT_ENOMEM;
        goto exit;
    }

    result = rt_thread_startup(ui_thread);
    if (result != RT_EOK)
        goto exit;

    ui_ready = true;
    ui_context_set_display_ready(true);
    LOG_I("UI manager started");
    result = RT_EOK;

exit:
    ui_init_result = result;
    return result;
}

bool ui_manager_is_ready(void)
{
    return ui_ready;
}

bool ui_manager_is_suspended(void)
{
    return ui_suspended;
}

rt_err_t ui_manager_set_suspended(bool suspended)
{
    lv_display_t *display;
    lv_timer_t *refresh_timer;

    if (!ui_ready)
        return -RT_ERROR;

    ui_manager_lock();
    if (ui_suspended == suspended)
    {
        ui_manager_unlock();
        return RT_EOK;
    }

    display = lv_display_get_default();
    refresh_timer = display != RT_NULL ?
                    lv_display_get_refr_timer(display) : RT_NULL;
    if (suspended)
    {
        lv_indev_enable(RT_NULL, false);
        if (refresh_timer != RT_NULL)
            lv_timer_pause(refresh_timer);
    }
    else
    {
        lv_indev_enable(RT_NULL, true);
        if (display != RT_NULL)
        {
            lv_display_trigger_activity(display);
            lv_obj_invalidate(lv_screen_active());
        }
        if (refresh_timer != RT_NULL)
            lv_timer_resume(refresh_timer);
    }
    ui_suspended = suspended;
    ui_manager_unlock();
    return RT_EOK;
}
