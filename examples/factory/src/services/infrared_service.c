#include "infrared_service.h"

#include <rtdevice.h>
#include <ulog.h>

#if defined(FACTORY_HW_USING_KEYBOARD)
#include "ir_nec.h"
#endif

#define INFRARED_QUEUE_DEPTH       4U
#define INFRARED_WORKER_STACK_SIZE 1024U
#define INFRARED_WORKER_PRIORITY   RT_THREAD_PRIORITY_HIGH
#define INFRARED_WORKER_TICK       10U

typedef enum
{
    INFRARED_WORK_SEND = 0,
    INFRARED_WORK_REPEAT,
} infrared_work_type_t;

typedef struct
{
    infrared_work_type_t type;
    uint8_t address;
    uint8_t command;
} infrared_work_t;

static rt_mq_t infrared_queue;
static rt_thread_t infrared_worker;
static volatile infrared_service_state_t infrared_state =
    INFRARED_SERVICE_STATE_UNAVAILABLE;
static bool infrared_initialized;

#if defined(FACTORY_HW_USING_KEYBOARD)
static void infrared_service_worker(void *parameter)
{
    infrared_work_t work;

    (void)parameter;
    while (true)
    {
        int result;

        if (rt_mq_recv(infrared_queue, &work, sizeof(work),
                       RT_WAITING_FOREVER) != RT_EOK)
            continue;

        infrared_state = INFRARED_SERVICE_STATE_SENDING;
        if (work.type == INFRARED_WORK_REPEAT)
        {
            ir_nec_send_repeat();
            result = RT_EOK;
        }
        else
        {
            result = ir_nec_send(work.address, work.command);
        }
        if (result == RT_EOK)
        {
            infrared_state = INFRARED_SERVICE_STATE_SENT;
            LOG_D("NEC sent: address=0x%02x command=0x%02x",
                  work.address, work.command);
        }
        else
        {
            infrared_state = INFRARED_SERVICE_STATE_ERROR;
            LOG_E("NEC send failed: %d", result);
        }
    }
}
#endif

rt_err_t infrared_service_init(void)
{
#if defined(FACTORY_HW_USING_KEYBOARD)
    rt_err_t result;

    if (infrared_initialized)
        return infrared_state != INFRARED_SERVICE_STATE_UNAVAILABLE
                   ? RT_EOK
                   : -RT_ENOSYS;

    result = ir_nec_init();
    if (result != RT_EOK)
    {
        LOG_E("NEC infrared initialization failed: %d", result);
        return result;
    }

    infrared_queue = rt_mq_create("ir_nec", sizeof(infrared_work_t),
                                  INFRARED_QUEUE_DEPTH,
                                  RT_IPC_FLAG_FIFO);
    if (infrared_queue == RT_NULL)
        return -RT_ENOMEM;

    infrared_worker = rt_thread_create(
        "ir_nec", infrared_service_worker, RT_NULL,
        INFRARED_WORKER_STACK_SIZE, INFRARED_WORKER_PRIORITY,
        INFRARED_WORKER_TICK);
    if (infrared_worker == RT_NULL)
    {
        rt_mq_delete(infrared_queue);
        infrared_queue = RT_NULL;
        return -RT_ENOMEM;
    }
    result = rt_thread_startup(infrared_worker);
    if (result != RT_EOK)
    {
        rt_thread_delete(infrared_worker);
        rt_mq_delete(infrared_queue);
        infrared_worker = RT_NULL;
        infrared_queue = RT_NULL;
        return result;
    }

    infrared_initialized = true;
    infrared_state = INFRARED_SERVICE_STATE_READY;
    LOG_I("NEC infrared service initialized");
    return RT_EOK;
#else
    return -RT_ENOSYS;
#endif
}

bool infrared_service_is_ready(void)
{
    return infrared_initialized && infrared_queue != RT_NULL &&
           infrared_state != INFRARED_SERVICE_STATE_UNAVAILABLE;
}

static rt_err_t infrared_service_queue_work(infrared_work_type_t type,
                                            uint8_t address,
                                            uint8_t command)
{
    infrared_work_t work = {
        .type = type,
        .address = address,
        .command = command,
    };

    if (!infrared_service_is_ready())
        return -RT_ENOSYS;
    return rt_mq_send(infrared_queue, &work, sizeof(work));
}

rt_err_t infrared_service_send_nec(uint8_t address, uint8_t command)
{
    return infrared_service_queue_work(INFRARED_WORK_SEND,
                                       address, command);
}

rt_err_t infrared_service_send_nec_repeat(void)
{
    return infrared_service_queue_work(INFRARED_WORK_REPEAT, 0U, 0U);
}

infrared_service_state_t infrared_service_get_state(void)
{
    return infrared_state;
}
