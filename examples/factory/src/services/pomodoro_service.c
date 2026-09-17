#include "pomodoro_service.h"

#define POMODORO_DEFAULT_MINUTES 25U
#define POMODORO_MIN_MINUTES      1U
#define POMODORO_MAX_MINUTES     99U

static rt_mutex_t pomodoro_lock;
static bool pomodoro_initialized;
static bool pomodoro_completion_pending;
static pomodoro_status_t pomodoro_status = {
    .state = POMODORO_STATE_READY,
    .duration_seconds = POMODORO_DEFAULT_MINUTES * 60U,
    .remaining_seconds = POMODORO_DEFAULT_MINUTES * 60U,
};
static rt_tick_t pomodoro_anchor_tick;

static void pomodoro_service_update_locked(void)
{
    rt_tick_t now;
    rt_tick_t elapsed_ticks;
    uint32_t elapsed_seconds;

    if (pomodoro_status.state != POMODORO_STATE_RUNNING)
        return;

    now = rt_tick_get();
    elapsed_ticks = now - pomodoro_anchor_tick;
    elapsed_seconds = (uint32_t)(elapsed_ticks / RT_TICK_PER_SECOND);
    if (elapsed_seconds == 0U)
        return;

    pomodoro_anchor_tick +=
        (rt_tick_t)(elapsed_seconds * RT_TICK_PER_SECOND);
    if (elapsed_seconds >= pomodoro_status.remaining_seconds)
    {
        pomodoro_status.remaining_seconds = 0U;
        pomodoro_status.state = POMODORO_STATE_FINISHED;
        pomodoro_completion_pending = true;
    }
    else
    {
        pomodoro_status.remaining_seconds -= elapsed_seconds;
    }
}

rt_err_t pomodoro_service_init(void)
{
    if (pomodoro_initialized)
        return RT_EOK;

    pomodoro_lock = rt_mutex_create("pomodoro", RT_IPC_FLAG_PRIO);
    if (pomodoro_lock == RT_NULL)
        return -RT_ENOMEM;
    pomodoro_initialized = true;
    return RT_EOK;
}

rt_err_t pomodoro_service_set_duration(uint8_t minutes)
{
    if (!pomodoro_initialized || pomodoro_lock == RT_NULL)
        return -RT_ERROR;
    if (minutes < POMODORO_MIN_MINUTES ||
            minutes > POMODORO_MAX_MINUTES)
        return -RT_EINVAL;

    rt_mutex_take(pomodoro_lock, RT_WAITING_FOREVER);
    pomodoro_status.duration_seconds = (uint32_t)minutes * 60U;
    pomodoro_status.remaining_seconds = pomodoro_status.duration_seconds;
    pomodoro_status.state = POMODORO_STATE_READY;
    pomodoro_completion_pending = false;
    rt_mutex_release(pomodoro_lock);
    return RT_EOK;
}

rt_err_t pomodoro_service_start(void)
{
    if (!pomodoro_initialized || pomodoro_lock == RT_NULL)
        return -RT_ERROR;

    rt_mutex_take(pomodoro_lock, RT_WAITING_FOREVER);
    pomodoro_service_update_locked();
    if (pomodoro_status.state == POMODORO_STATE_FINISHED ||
            pomodoro_status.remaining_seconds == 0U)
    {
        pomodoro_status.remaining_seconds =
            pomodoro_status.duration_seconds;
    }
    pomodoro_status.state = POMODORO_STATE_RUNNING;
    pomodoro_completion_pending = false;
    pomodoro_anchor_tick = rt_tick_get();
    rt_mutex_release(pomodoro_lock);
    return RT_EOK;
}

rt_err_t pomodoro_service_pause(void)
{
    if (!pomodoro_initialized || pomodoro_lock == RT_NULL)
        return -RT_ERROR;

    rt_mutex_take(pomodoro_lock, RT_WAITING_FOREVER);
    pomodoro_service_update_locked();
    if (pomodoro_status.state == POMODORO_STATE_RUNNING)
        pomodoro_status.state = POMODORO_STATE_PAUSED;
    rt_mutex_release(pomodoro_lock);
    return RT_EOK;
}

rt_err_t pomodoro_service_reset(void)
{
    if (!pomodoro_initialized || pomodoro_lock == RT_NULL)
        return -RT_ERROR;

    rt_mutex_take(pomodoro_lock, RT_WAITING_FOREVER);
    pomodoro_status.remaining_seconds = pomodoro_status.duration_seconds;
    pomodoro_status.state = POMODORO_STATE_READY;
    pomodoro_completion_pending = false;
    rt_mutex_release(pomodoro_lock);
    return RT_EOK;
}

rt_err_t pomodoro_service_get_status(pomodoro_status_t *status)
{
    if (status == RT_NULL)
        return -RT_EINVAL;
    if (!pomodoro_initialized || pomodoro_lock == RT_NULL)
        return -RT_ERROR;

    rt_mutex_take(pomodoro_lock, RT_WAITING_FOREVER);
    pomodoro_service_update_locked();
    *status = pomodoro_status;
    rt_mutex_release(pomodoro_lock);
    return RT_EOK;
}

bool pomodoro_service_take_completion(void)
{
    bool pending;

    if (!pomodoro_initialized || pomodoro_lock == RT_NULL)
        return false;

    rt_mutex_take(pomodoro_lock, RT_WAITING_FOREVER);
    pomodoro_service_update_locked();
    pending = pomodoro_completion_pending;
    pomodoro_completion_pending = false;
    rt_mutex_release(pomodoro_lock);
    return pending;
}

#ifdef RT_USING_FINSH
static int pomodoro_test(int argc, char **argv)
{
    (void)argv;
    if (argc != 1)
    {
        rt_kprintf("Usage: pomodoro_test\n");
        return -RT_EINVAL;
    }
    if (!pomodoro_initialized || pomodoro_lock == RT_NULL)
    {
        rt_kprintf("Pomodoro service is not ready\n");
        return -RT_ERROR;
    }

    rt_mutex_take(pomodoro_lock, RT_WAITING_FOREVER);
    pomodoro_status.remaining_seconds = 0U;
    pomodoro_status.state = POMODORO_STATE_FINISHED;
    pomodoro_completion_pending = true;
    rt_mutex_release(pomodoro_lock);

    rt_kprintf("Pomodoro completion queued; UI and vibration should start "
               "within one second\n");
    return RT_EOK;
}
MSH_CMD_EXPORT(pomodoro_test, trigger the Pomodoro completion alert);
#endif
