#ifndef FACTORY_UI_CONFIG_H
#define FACTORY_UI_CONFIG_H

#include <rtthread.h>

#define UI_DISPLAY_DEVICE_NAME "lcd"

#define UI_SCREEN_WIDTH  480
#define UI_SCREEN_HEIGHT 480

/* The crash dump measured 95% usage with a 6 KB stack. This stack is in PSRAM. */
#define UI_TASK_STACK_SIZE (8U * 1024U)
#define UI_TASK_PRIORITY   RT_THREAD_PRIORITY_HIGH
#define UI_TASK_TICK       10U

#define UI_EVENT_QUEUE_DEPTH 16U
#define UI_HANDLER_MIN_DELAY_MS 2U
#define UI_HANDLER_MAX_DELAY_MS 20U
#define UI_SUSPENDED_DELAY_MS   50U

#endif /* FACTORY_UI_CONFIG_H */
