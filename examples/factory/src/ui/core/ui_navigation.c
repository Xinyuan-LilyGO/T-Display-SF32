#include "ui_navigation.h"

#include <stdbool.h>

#include <ulog.h>

#include "ui_context.h"
#include "ui_event.h"
#include "ui_keypad.h"
#include "ui_status_bar.h"
#include "ui_theme.h"

static ui_page_t page_table[UI_PAGE_COUNT];
static ui_page_t *current_page;
static rt_mutex_t navigation_lock;
static bool navigation_initialized;

static void navigation_take(void)
{
    if (navigation_lock != RT_NULL)
        rt_mutex_take(navigation_lock, RT_WAITING_FOREVER);
}

static void navigation_release(void)
{
    if (navigation_lock != RT_NULL)
        rt_mutex_release(navigation_lock);
}

static ui_gesture_t navigation_get_gesture(lv_event_t *event)
{
    lv_indev_t *input = lv_event_get_indev(event);
    lv_dir_t direction;

    if (input == RT_NULL)
        input = lv_indev_active();
    if (input == RT_NULL)
        return UI_GESTURE_NONE;

    direction = lv_indev_get_gesture_dir(input);
    switch (direction)
    {
    case LV_DIR_LEFT:
        return UI_GESTURE_LEFT;
    case LV_DIR_RIGHT:
        return UI_GESTURE_RIGHT;
    case LV_DIR_TOP:
        return UI_GESTURE_UP;
    case LV_DIR_BOTTOM:
        return UI_GESTURE_DOWN;
    default:
        return UI_GESTURE_NONE;
    }
}

static void navigation_gesture_callback(lv_event_t *event)
{
    ui_page_id_t page_id =
        (ui_page_id_t)(uintptr_t)lv_event_get_user_data(event);
    ui_page_t *page = ui_navigation_get_page(page_id);
    ui_gesture_t gesture;

    if (page == RT_NULL || page->on_gesture == RT_NULL)
        return;

    gesture = navigation_get_gesture(event);
    if (gesture != UI_GESTURE_NONE)
        page->on_gesture(page, gesture);
}

static bool navigation_create_page_root(ui_page_t *page)
{
    if (page == RT_NULL)
        return false;

    if (page->root == RT_NULL)
    {
        page->root = lv_obj_create(RT_NULL);
        if (page->root == RT_NULL)
        {
            LOG_E("Create UI page root failed: %d", page->id);
            return false;
        }
    }

    return true;
}

static void navigation_build_page(ui_page_t *page)
{
    if (page == RT_NULL || page->root == RT_NULL || page->is_created)
        return;

    ui_theme_apply_screen(page->root);
    lv_obj_add_flag(page->root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(page->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(page->root, navigation_gesture_callback,
                        LV_EVENT_GESTURE,
                        (void *)(uintptr_t)page->id);

    page->group = lv_group_create();
    if (page->group != RT_NULL && page->group_cb != RT_NULL)
        lv_group_set_focus_cb(page->group, page->group_cb);
    if (page->group != RT_NULL)
        lv_group_set_wrap(page->group, false);

    if (page->on_create != RT_NULL)
        page->on_create(page);

    page->is_created = true;
    page->timer_elapsed_ms = 0;
}

static void navigation_activate_page(ui_page_t *page, bool load_screen)
{
    if (page == RT_NULL || !page->is_created)
        return;

    if (load_screen)
        lv_screen_load(page->root);
    ui_keypad_set_group(page->group);
    ui_status_bar_set_visible(page->id != UI_PAGE_START);
    if (page->on_enter != RT_NULL)
        page->on_enter(page);
}

static void navigation_enter_page(ui_page_t *page)
{
    if (!navigation_create_page_root(page))
        return;

    navigation_build_page(page);
    navigation_activate_page(page, true);
}

static void navigation_leave_page(ui_page_t *page)
{
    if (page == RT_NULL)
        return;

    if (page->group != RT_NULL)
        page->last_group_object = lv_group_get_focused(page->group);
    page->timer_elapsed_ms = 0;

    if (page->on_leave != RT_NULL)
        page->on_leave(page);
}

static void navigation_destroy_page_objects(ui_page_t *page)
{
    if (page == RT_NULL || !page->is_created)
        return;

    if (page->on_destroy != RT_NULL)
        page->on_destroy(page);

    if (page->group != RT_NULL)
    {
        lv_group_del(page->group);
        page->group = RT_NULL;
    }

    if (page->root != RT_NULL)
    {
        lv_obj_del(page->root);
        page->root = RT_NULL;
    }

    page->last_group_object = RT_NULL;
    page->is_created = false;
    page->timer_elapsed_ms = 0;
}

static rt_err_t navigation_switch_page(ui_page_id_t page_id)
{
    ui_page_t *next;
    ui_page_t *last;

    if (page_id >= UI_PAGE_COUNT)
        return -RT_EINVAL;

    navigation_take();
    next = &page_table[page_id];
    if (next->name == RT_NULL || next->id != page_id)
    {
        navigation_release();
        return -RT_EINVAL;
    }

    if (current_page == next)
    {
        navigation_release();
        return RT_EOK;
    }

    last = current_page;
    navigation_release();

    /* Only allocate the next screen root while the old page is resident.
     * Loading this empty screen makes the old root safe to delete before the
     * next page allocates its controls and styles. */
    if (!navigation_create_page_root(next))
        return -RT_ENOMEM;

    navigation_leave_page(last);
    ui_keypad_set_group(RT_NULL);
    lv_screen_load(next->root);

    navigation_take();
    current_page = next;
    navigation_release();

    navigation_destroy_page_objects(last);
    navigation_build_page(next);
    navigation_activate_page(next, false);
    ui_context_set_page(next->id,
                        last != RT_NULL ? last->id : UI_PAGE_COUNT);
    return RT_EOK;
}

rt_err_t ui_navigation_register_page(const ui_page_t *page)
{
    if (page == RT_NULL || page->name == RT_NULL ||
            page->id >= UI_PAGE_COUNT)
        return -RT_EINVAL;

    navigation_take();
    page_table[page->id] = *page;
    page_table[page->id].root = RT_NULL;
    page_table[page->id].group = RT_NULL;
    page_table[page->id].last_group_object = RT_NULL;
    page_table[page->id].is_created = false;
    page_table[page->id].timer_elapsed_ms = 0;
    navigation_release();
    return RT_EOK;
}

rt_err_t ui_navigation_init(void)
{
    if (navigation_initialized)
        return RT_EOK;

    navigation_lock = rt_mutex_create("ui_nav", RT_IPC_FLAG_PRIO);
    if (navigation_lock == RT_NULL)
        return -RT_ENOMEM;

    if (page_table[UI_PAGE_START].name == RT_NULL)
    {
        LOG_E("UI start page is not registered");
        return -RT_ERROR;
    }

    current_page = &page_table[UI_PAGE_START];
    ui_context_set_page(UI_PAGE_START, UI_PAGE_COUNT);
    navigation_enter_page(current_page);
    navigation_initialized = current_page->is_created;
    return navigation_initialized ? RT_EOK : -RT_ERROR;
}

rt_err_t ui_navigation_switch_async(ui_page_id_t page_id)
{
    const ui_event_t event = {
        .type = UI_EVENT_PAGE_SWITCH,
        .data = (uintptr_t)page_id,
    };

    if (!navigation_initialized || page_id >= UI_PAGE_COUNT)
        return -RT_EINVAL;

    return ui_event_post(&event);
}

rt_err_t ui_navigation_handle_event(const ui_event_t *event)
{
    ui_page_t *page;

    if (event == RT_NULL)
        return -RT_EINVAL;

    if (event->type == UI_EVENT_PAGE_SWITCH)
        return navigation_switch_page((ui_page_id_t)event->data);

    navigation_take();
    page = current_page;
    navigation_release();
    if (page != RT_NULL && page->on_event != RT_NULL)
        page->on_event(page, event);
    return RT_EOK;
}

void ui_navigation_tick(uint32_t elapsed_ms)
{
    ui_page_t *page;
    bool fire_timer = false;

    navigation_take();
    page = current_page;
    if (page != RT_NULL && page->is_created &&
            page->on_timer != RT_NULL && page->timer_interval_ms > 0U)
    {
        page->timer_elapsed_ms += elapsed_ms;
        if (page->timer_elapsed_ms >= page->timer_interval_ms)
        {
            page->timer_elapsed_ms = 0;
            fire_timer = true;
        }
    }
    navigation_release();

    if (fire_timer)
        page->on_timer(page);
}

ui_page_t *ui_navigation_get_page(ui_page_id_t page_id)
{
    if (page_id >= UI_PAGE_COUNT || page_table[page_id].name == RT_NULL)
        return RT_NULL;

    return &page_table[page_id];
}

ui_page_t *ui_navigation_get_current_page(void)
{
    return current_page;
}

ui_page_id_t ui_navigation_get_current_page_id(void)
{
    return current_page != RT_NULL ? current_page->id : UI_PAGE_COUNT;
}

rt_err_t ui_navigation_destroy_page(ui_page_id_t page_id)
{
    ui_page_t *page;

    if (page_id >= UI_PAGE_COUNT)
        return -RT_EINVAL;

    page = &page_table[page_id];
    if (page == current_page)
        return -RT_EBUSY;

    navigation_destroy_page_objects(page);
    return RT_EOK;
}
