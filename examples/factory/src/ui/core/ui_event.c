#include "ui_event.h"

#include "ui_config.h"

static rt_mq_t ui_event_queue;

rt_err_t ui_event_init(void)
{
    if (ui_event_queue != RT_NULL)
        return RT_EOK;

    ui_event_queue = rt_mq_create("ui_evt", sizeof(ui_event_t),
                                  UI_EVENT_QUEUE_DEPTH,
                                  RT_IPC_FLAG_FIFO);
    if (ui_event_queue == RT_NULL)
        return -RT_ENOMEM;

    return RT_EOK;
}

bool ui_event_is_ready(void)
{
    return ui_event_queue != RT_NULL;
}

rt_err_t ui_event_post(const ui_event_t *event)
{
    ui_event_t queued_event;

    if (event == RT_NULL || event->type <= UI_EVENT_NONE ||
            event->type >= UI_EVENT_COUNT)
        return -RT_EINVAL;
    if (ui_event_queue == RT_NULL)
        return -RT_ERROR;

    queued_event = *event;
    return rt_mq_send(ui_event_queue, &queued_event,
                      sizeof(queued_event));
}

rt_err_t ui_event_receive(ui_event_t *event, rt_int32_t timeout)
{
    if (event == RT_NULL)
        return -RT_EINVAL;
    if (ui_event_queue == RT_NULL)
        return -RT_ERROR;

    return rt_mq_recv(ui_event_queue, event, sizeof(*event), timeout);
}
