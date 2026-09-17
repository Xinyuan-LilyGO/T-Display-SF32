#include "input_service.h"

#include <button.h>
#include <rtdevice.h>
#include <ulog.h>

#if defined(FACTORY_HW_USING_KEYBOARD) || \
    defined(FACTORY_HW_USING_JOYPAD)
#include "xl9555_rt_device.h"
#endif

#if defined(FACTORY_HW_USING_JOYPAD)
#include <drivers/adc.h>
#include "ads1115_rt_adc.h"
#include "ws2812.h"
#elif defined(FACTORY_HW_USING_KEYBOARD)
#include "tca8418_rt_device.h"
#endif

#define INPUT_BUTTON_33_PIN              33
#define INPUT_BUTTON_34_PIN              34
#define INPUT_BUTTON_35_PIN              35
#define INPUT_BUTTON_COUNT               3U
#define INPUT_EVENT_QUEUE_DEPTH          24U
#define INPUT_KEYBOARD_BATCH_SIZE        10U
#define INPUT_POLL_PERIOD_MS             50U
#define INPUT_THREAD_STACK_SIZE          2048U
#define INPUT_THREAD_PRIORITY            (RT_THREAD_PRIORITY_HIGH + 1U)
#define INPUT_THREAD_TIMESLICE           10U

#if defined(FACTORY_HW_USING_JOYPAD)
#define INPUT_JOYPAD_KEY_MASK            0x007FU
#define INPUT_JOYPAD_X_CHANNEL           0U
#define INPUT_JOYPAD_Y_CHANNEL           1U
#define INPUT_JOYPAD_CENTER_MV           1600
#define INPUT_JOYPAD_PRESS_DELTA_MV      350
#define INPUT_JOYPAD_RELEASE_DELTA_MV    220
#define INPUT_JOYPAD_SWITCH_MARGIN_MV    100
#define INPUT_JOYPAD_REPEAT_DELAY_MS     450U
#define INPUT_JOYPAD_REPEAT_PERIOD_MS    150U
#define INPUT_JOYPAD_THREAD_STACK_SIZE   2048U
#define INPUT_JOYPAD_THREAD_PRIORITY     RT_THREAD_PRIORITY_MIDDLE
#define INPUT_JOYPAD_THREAD_TIMESLICE    10U
#endif

static const int32_t input_button_pins[INPUT_BUTTON_COUNT] = {
    INPUT_BUTTON_33_PIN,
    INPUT_BUTTON_34_PIN,
    INPUT_BUTTON_35_PIN,
};

static int32_t input_button_ids[INPUT_BUTTON_COUNT] = {-1, -1, -1};
#if defined(FACTORY_HW_USING_KEYBOARD) || \
    defined(FACTORY_HW_USING_JOYPAD)
static rt_device_t input_expander;
#endif
static rt_mq_t input_event_queue;
static rt_mutex_t input_callback_lock;
static input_event_callback_t input_event_callback;
static void *input_event_user_data;
static bool input_initialized;
static bool input_init_attempted;
static rt_err_t input_init_result = -RT_ERROR;
static uint32_t input_init_errors;

#if defined(FACTORY_HW_USING_JOYPAD)
static rt_adc_device_t input_joypad_adc;
static rt_mutex_t input_joypad_lock;
static input_joypad_state_t input_joypad_state;
static bool input_joypad_keys_ready;
static bool input_joypad_axis_ready;
static bool input_joypad_rgb_ready;
static bool input_joypad_rgb_enabled;
static uint8_t input_joypad_rgb_brightness = 100U;
static uint8_t input_joypad_rgb_red;
static uint8_t input_joypad_rgb_green = 255U;
static uint8_t input_joypad_rgb_blue;
static int32_t input_joypad_direction;
static rt_tick_t input_joypad_repeat_tick;
static rt_thread_t input_joypad_adc_thread;
static bool input_joypad_first_sample_logged;
static bool input_joypad_first_sample_started;
static bool input_joypad_key_read_failure_reported;
static rt_uint16_t input_joypad_input_cache;
#elif defined(FACTORY_HW_USING_KEYBOARD)
static rt_device_t input_keyboard;
#endif

static const char *input_source_name(input_source_t source)
{
    switch (source)
    {
    case INPUT_SOURCE_BUTTON:
        return "button";
    case INPUT_SOURCE_KEYBOARD:
        return "keyboard";
    case INPUT_SOURCE_JOYPAD_KEY:
        return "joypad-key";
    case INPUT_SOURCE_JOYPAD_AXIS:
        return "joypad-axis";
    default:
        return "unknown";
    }
}

static const char *input_action_name(input_action_t action)
{
    switch (action)
    {
    case INPUT_ACTION_PRESSED:
        return "pressed";
    case INPUT_ACTION_RELEASED:
        return "released";
    case INPUT_ACTION_CLICKED:
        return "clicked";
    case INPUT_ACTION_LONG_PRESSED:
        return "long-pressed";
    default:
        return "unknown";
    }
}

static input_action_t input_button_action(button_action_t action)
{
    switch (action)
    {
    case BUTTON_PRESSED:
        return INPUT_ACTION_PRESSED;
    case BUTTON_RELEASED:
        return INPUT_ACTION_RELEASED;
    case BUTTON_LONG_PRESSED:
        return INPUT_ACTION_LONG_PRESSED;
    case BUTTON_CLICKED:
    default:
        return INPUT_ACTION_CLICKED;
    }
}

static void input_dispatch_event(const input_event_t *event)
{
    input_event_callback_t callback = RT_NULL;
    void *user_data = RT_NULL;

    LOG_I("%s code=%d %s", input_source_name(event->source), event->code,
          input_action_name(event->action));

    if (input_callback_lock != RT_NULL)
    {
        rt_mutex_take(input_callback_lock, RT_WAITING_FOREVER);
        callback = input_event_callback;
        user_data = input_event_user_data;
        rt_mutex_release(input_callback_lock);
    }

    if (callback != RT_NULL)
        callback(event, user_data);
}

static void input_queue_event(const input_event_t *event)
{
    if (input_event_queue == RT_NULL ||
        rt_mq_send(input_event_queue, (void *)event,
                   sizeof(*event)) != RT_EOK)
    {
        LOG_W("Drop %s event: code=%d action=%d",
              input_source_name(event->source), event->code, event->action);
    }
}

static void input_button_handler(int32_t pin, button_action_t action)
{
    input_event_t event = {
        .source = INPUT_SOURCE_BUTTON,
        .action = input_button_action(action),
        .code = pin,
        .timestamp = rt_tick_get(),
    };

    input_queue_event(&event);
}

#if defined(FACTORY_HW_USING_KEYBOARD) || \
    defined(FACTORY_HW_USING_JOYPAD)
static rt_err_t input_expander_read_inputs(rt_uint16_t *input_state)
{
    struct rt_i2c_bus_device *bus;
    struct rt_i2c_msg messages[2];
    rt_uint8_t reg = XL9555_INPUT_PORT_0;
    rt_uint8_t value[2] = {0U, 0U};

    if (input_state == RT_NULL)
        return -RT_EINVAL;

    bus = rt_i2c_bus_device_find(XL9555_I2C_BUS_NAME);
    if (bus == RT_NULL)
        return -RT_ENOSYS;

    messages[0].addr = XL9555_I2C_ADDR;
    messages[0].flags = RT_I2C_WR;
    messages[0].buf = &reg;
    messages[0].len = 1;
    messages[1].addr = XL9555_I2C_ADDR;
    messages[1].flags = RT_I2C_RD;
    messages[1].buf = value;
    messages[1].len = sizeof(value);

    if (rt_i2c_transfer(bus, messages, 2) != 2)
        return -RT_EIO;

    *input_state = (rt_uint16_t)value[0] |
                   ((rt_uint16_t)value[1] << 8);
    return RT_EOK;
}

static rt_err_t input_expander_probe(void)
{
    rt_uint16_t input_state;

    return input_expander_read_inputs(&input_state);
}

static rt_err_t input_expander_open(void)
{
    rt_err_t result;

    if (input_expander != RT_NULL)
        return RT_EOK;

    input_expander = rt_device_find(XL9555_RT_DEVICE_NAME);
    if (input_expander == RT_NULL)
    {
        input_init_errors |= INPUT_INIT_ERROR_XL9555;
        LOG_E("IO expander '%s' not found", XL9555_RT_DEVICE_NAME);
        return -RT_ENOSYS;
    }

    result = rt_device_open(input_expander, RT_DEVICE_OFLAG_RDWR);
    if (result != RT_EOK)
    {
        input_init_errors |= INPUT_INIT_ERROR_XL9555;
        LOG_E("Open IO expander '%s' failed: %d",
              XL9555_RT_DEVICE_NAME, result);
        input_expander = RT_NULL;
        return result;
    }

    result = input_expander_probe();
    if (result != RT_EOK)
    {
        input_init_errors |= INPUT_INIT_ERROR_XL9555;
        LOG_E("IO expander XL9555 did not respond on '%s'",
              XL9555_I2C_BUS_NAME);
        rt_device_close(input_expander);
        input_expander = RT_NULL;
    }
    return result;
}

static rt_err_t input_expander_set_mode(rt_uint8_t pin, rt_uint8_t mode)
{
    struct xl9555_rt_pin_mode pin_mode = {
        .pin = pin,
        .mode = mode,
    };

    return rt_device_control(input_expander,
                             XL9555_RT_CTRL_SET_PIN_MODE,
                             &pin_mode);
}
#endif

#if defined(FACTORY_HW_USING_JOYPAD)
static void input_joypad_process_keys(rt_uint16_t input_state,
                                      rt_uint16_t changed_mask)
{
    rt_uint16_t relevant_changes = changed_mask & INPUT_JOYPAD_KEY_MASK;
    rt_tick_t timestamp = rt_tick_get();
    rt_uint8_t pin;

    if (relevant_changes == 0U)
        return;

    if (input_joypad_lock != RT_NULL &&
        rt_mutex_take(input_joypad_lock, RT_WAITING_FOREVER) == RT_EOK)
    {
        input_joypad_state.key_state =
            (rt_uint16_t)(~input_state) & INPUT_JOYPAD_KEY_MASK;
        input_joypad_state.keys_valid = true;
        input_joypad_state.timestamp = timestamp;
        rt_mutex_release(input_joypad_lock);
    }

    for (pin = 0; pin < 7U; pin++)
    {
        rt_uint16_t mask = (rt_uint16_t)(1U << pin);
        input_event_t event;

        if ((relevant_changes & mask) == 0U)
            continue;

        event.source = INPUT_SOURCE_JOYPAD_KEY;
        event.action = (input_state & mask) != 0U
                           ? INPUT_ACTION_RELEASED
                           : INPUT_ACTION_PRESSED;
        event.code = pin;
        event.timestamp = timestamp;
        input_queue_event(&event);
    }
}

static void input_joypad_keys_poll(void)
{
    rt_uint16_t input_state;
    rt_uint16_t changed_mask;
    rt_err_t result;

    if (!input_joypad_keys_ready)
        return;

    result = input_expander_read_inputs(&input_state);
    if (result != RT_EOK)
    {
        if (!input_joypad_key_read_failure_reported)
        {
            LOG_W("Read Joypad keys failed: %d", result);
            input_joypad_key_read_failure_reported = true;
        }
        return;
    }
    if (input_joypad_key_read_failure_reported)
    {
        LOG_I("Joypad key polling recovered");
        input_joypad_key_read_failure_reported = false;
    }

    changed_mask = (input_state ^ input_joypad_input_cache) &
                   INPUT_JOYPAD_KEY_MASK;
    input_joypad_input_cache = input_state;
    input_joypad_process_keys(input_state, changed_mask);
}

static rt_err_t input_joypad_keys_init(void)
{
    rt_bool_t irq_enable = RT_FALSE;
    rt_uint16_t input_state;
    rt_uint8_t pin;
    rt_err_t result;

    result = input_expander_open();
    if (result != RT_EOK)
    {
        input_init_errors |= INPUT_INIT_ERROR_JOYPAD_KEYS;
        return result;
    }

    result = rt_device_control(input_expander,
                               XL9555_RT_CTRL_ENABLE_IRQ,
                               &irq_enable);
    if (result != RT_EOK)
    {
        LOG_E("Disable XL9555 IRQ mode failed: %d", result);
        goto fail;
    }

    for (pin = 0; pin < 7U; pin++)
    {
        result = input_expander_set_mode(pin, XL9555_PIN_INPUT);
        if (result != RT_EOK)
        {
            LOG_E("Set joypad P0_%u input mode failed: %d", pin, result);
            goto fail;
        }
    }

    result = input_expander_read_inputs(&input_state);
    if (result != RT_EOK)
    {
        LOG_E("Read initial joypad key state failed: %d", result);
        goto fail;
    }

    rt_mutex_take(input_joypad_lock, RT_WAITING_FOREVER);
    input_joypad_state.key_state =
        (rt_uint16_t)(~input_state) & INPUT_JOYPAD_KEY_MASK;
    input_joypad_state.keys_valid = true;
    input_joypad_state.timestamp = rt_tick_get();
    rt_mutex_release(input_joypad_lock);
    input_joypad_input_cache = input_state;
    input_joypad_key_read_failure_reported = false;

    LOG_I("Joypad initial key state: raw=0x%04x pressed=0x%02x",
          (unsigned int)input_state,
          (unsigned int)((~input_state) & INPUT_JOYPAD_KEY_MASK));
    if (((~input_state) & INPUT_JOYPAD_KEY_MASK) != 0U)
    {
        LOG_W("Joypad keys active during initialization: mask=0x%02x",
              (unsigned int)((~input_state) & INPUT_JOYPAD_KEY_MASK));
    }

    input_joypad_keys_ready = true;
    LOG_I("Joypad keys use 50 ms polling; XL9555 IRQ disabled");
    return RT_EOK;

fail:
    input_init_errors |= INPUT_INIT_ERROR_JOYPAD_KEYS;
    LOG_E("Configure joypad keys P0_0..P0_6 failed: %d", result);
    return result;
}

static rt_err_t input_joypad_adc_init(void)
{
    rt_err_t result;

    input_joypad_adc =
        (rt_adc_device_t)rt_device_find(ADS1115_ADC_DEVICE_NAME);
    if (input_joypad_adc == RT_NULL)
    {
        input_init_errors |= INPUT_INIT_ERROR_ADS1115;
        LOG_E("Joypad ADC '%s' not found", ADS1115_ADC_DEVICE_NAME);
        return -RT_ENOSYS;
    }

    result = rt_adc_enable(input_joypad_adc, INPUT_JOYPAD_X_CHANNEL);
    if (result == RT_EOK)
        result = rt_adc_enable(input_joypad_adc, INPUT_JOYPAD_Y_CHANNEL);
    if (result != RT_EOK)
    {
        input_init_errors |= INPUT_INIT_ERROR_ADS1115;
        input_joypad_adc = RT_NULL;
        LOG_E("Enable joypad ADC channels failed: %d", result);
        return result;
    }

    input_joypad_axis_ready = true;
    return RT_EOK;
}

static rt_err_t input_joypad_rgb_init(void)
{
    rt_err_t result;
    rt_err_t stop_result;

    result = ws2812_init();
    if (result == RT_EOK)
        result = ws2812_clear();
    stop_result = ws2812_deinit();
    if (result == RT_EOK)
        result = stop_result;
    if (result != RT_EOK)
    {
        input_init_errors |= INPUT_INIT_ERROR_WS2812;
        LOG_E("Initialize joypad WS2812 LEDs failed: %d", result);
        return result;
    }

    input_joypad_rgb_ready = true;
    input_joypad_rgb_enabled = false;

    LOG_I("Joypad WS2812 initialized: %u LEDs",
          (unsigned int)ws2812_get_pixel_count());
    return RT_EOK;
}

static rt_err_t input_joypad_read_channel_mv(rt_uint32_t channel,
                                              rt_int32_t *millivolts)
{
    rt_adc_cmd_read_arg_t read_arg = {
        .channel = channel,
        .value = 0,
    };
    rt_err_t result;

    result = rt_device_control((rt_device_t)input_joypad_adc,
                               RT_ADC_CMD_READ, &read_arg);
    if (result == RT_EOK)
        *millivolts = (rt_int32_t)(read_arg.value / 10U);
    return result;
}

static rt_int32_t input_joypad_abs(rt_int32_t value)
{
    return value < 0 ? -value : value;
}

static rt_int32_t input_joypad_direction_magnitude(rt_int32_t direction,
                                                    rt_int32_t x_delta,
                                                    rt_int32_t y_delta)
{
    if (direction == INPUT_JOYPAD_CODE_LEFT ||
            direction == INPUT_JOYPAD_CODE_RIGHT)
    {
        return input_joypad_abs(x_delta);
    }
    if (direction == INPUT_JOYPAD_CODE_UP ||
            direction == INPUT_JOYPAD_CODE_DOWN)
    {
        return input_joypad_abs(y_delta);
    }
    return 0;
}

static rt_int32_t input_joypad_decode_direction(rt_int32_t x_delta,
                                                 rt_int32_t y_delta)
{
    rt_int32_t x_magnitude = input_joypad_abs(x_delta);
    rt_int32_t y_magnitude = input_joypad_abs(y_delta);

    if (x_magnitude < INPUT_JOYPAD_PRESS_DELTA_MV &&
            y_magnitude < INPUT_JOYPAD_PRESS_DELTA_MV)
    {
        return 0;
    }

    if (x_magnitude > y_magnitude)
    {
        return x_delta < 0 ? INPUT_JOYPAD_CODE_LEFT
                           : INPUT_JOYPAD_CODE_RIGHT;
    }
    return y_delta > 0 ? INPUT_JOYPAD_CODE_UP
                       : INPUT_JOYPAD_CODE_DOWN;
}

static const char *input_joypad_direction_name(rt_int32_t direction)
{
    switch (direction)
    {
    case INPUT_JOYPAD_CODE_UP:
        return "up";
    case INPUT_JOYPAD_CODE_DOWN:
        return "down";
    case INPUT_JOYPAD_CODE_LEFT:
        return "left";
    case INPUT_JOYPAD_CODE_RIGHT:
        return "right";
    default:
        return "center";
    }
}

static int joypad_dump(int argc, char **argv)
{
    input_joypad_state_t state;
    rt_int32_t direction;

    (void)argc;
    (void)argv;
    if (input_service_get_joypad_state(&state) != RT_EOK ||
            !state.axis_valid)
    {
        rt_kprintf("Joypad ADC is not ready\n");
        return -RT_ENOSYS;
    }

    direction = input_joypad_decode_direction(
        state.x_mv - INPUT_JOYPAD_CENTER_MV,
        state.y_mv - INPUT_JOYPAD_CENTER_MV);
    rt_kprintf("Joypad X=%d mV, Y=%d mV, center=%d mV, "
               "press=%d mV, direction=%s\n",
               (int)state.x_mv, (int)state.y_mv,
               INPUT_JOYPAD_CENTER_MV, INPUT_JOYPAD_PRESS_DELTA_MV,
               input_joypad_direction_name(direction));
    return RT_EOK;
}
MSH_CMD_EXPORT(joypad_dump, show joypad ADC values and decoded direction);

static void input_joypad_dispatch_direction(rt_int32_t direction,
                                             rt_tick_t timestamp)
{
    input_event_t event = {
        .source = INPUT_SOURCE_JOYPAD_AXIS,
        .action = INPUT_ACTION_CLICKED,
        .code = direction,
        .timestamp = timestamp,
    };

    input_queue_event(&event);
}

static void input_joypad_update_direction(rt_int32_t x_mv,
                                           rt_int32_t y_mv)
{
    rt_int32_t x_delta = x_mv - INPUT_JOYPAD_CENTER_MV;
    rt_int32_t y_delta = y_mv - INPUT_JOYPAD_CENTER_MV;
    rt_int32_t next_direction = input_joypad_decode_direction(x_delta,
                                                               y_delta);
    rt_tick_t now = rt_tick_get();

    if (input_joypad_direction != 0)
    {
        rt_int32_t current_magnitude = input_joypad_direction_magnitude(
            input_joypad_direction, x_delta, y_delta);

        if (next_direction == 0 &&
                current_magnitude >= INPUT_JOYPAD_RELEASE_DELTA_MV)
        {
            next_direction = input_joypad_direction;
        }
        else if (next_direction != 0 &&
                 next_direction != input_joypad_direction &&
                 input_joypad_direction_magnitude(next_direction,
                                                   x_delta, y_delta) <
                     current_magnitude + INPUT_JOYPAD_SWITCH_MARGIN_MV)
        {
            next_direction = input_joypad_direction;
        }
    }

    if (next_direction != input_joypad_direction)
    {
        input_joypad_direction = next_direction;
        LOG_I("Joypad X=%d mV, Y=%d mV -> %s",
              x_mv, y_mv, input_joypad_direction_name(next_direction));
        if (next_direction != 0)
        {
            input_joypad_dispatch_direction(next_direction, now);
            input_joypad_repeat_tick = now + rt_tick_from_millisecond(
                INPUT_JOYPAD_REPEAT_DELAY_MS);
        }
        return;
    }

    if (next_direction != 0 &&
            (rt_int32_t)(now - input_joypad_repeat_tick) >= 0)
    {
        input_joypad_dispatch_direction(next_direction, now);
        input_joypad_repeat_tick = now + rt_tick_from_millisecond(
            INPUT_JOYPAD_REPEAT_PERIOD_MS);
    }
}

static void input_joypad_poll(void)
{
    rt_int32_t x_mv;
    rt_int32_t y_mv;
    rt_err_t result;

    if (!input_joypad_first_sample_started)
    {
        input_joypad_first_sample_started = true;
        LOG_I("Joypad ADC first sample: reading X/AIN0");
    }
    result = input_joypad_read_channel_mv(INPUT_JOYPAD_X_CHANNEL, &x_mv);
    if (result == RT_EOK)
    {
        if (!input_joypad_first_sample_logged)
            LOG_I("Joypad ADC first sample: X=%d mV, reading Y/AIN1", x_mv);
        result = input_joypad_read_channel_mv(INPUT_JOYPAD_Y_CHANNEL, &y_mv);
    }
    if (result != RT_EOK)
    {
        LOG_W("Read joypad ADC failed: %d", result);
        return;
    }

    rt_mutex_take(input_joypad_lock, RT_WAITING_FOREVER);
    input_joypad_state.x_mv = x_mv;
    input_joypad_state.y_mv = y_mv;
    input_joypad_state.axis_valid = true;
    input_joypad_state.timestamp = rt_tick_get();
    rt_mutex_release(input_joypad_lock);

    if (!input_joypad_first_sample_logged)
    {
        input_joypad_first_sample_logged = true;
        LOG_I("Joypad ADC polling active: X=%d mV, Y=%d mV",
              x_mv, y_mv);
    }

    input_joypad_update_direction(x_mv, y_mv);
}

static void input_joypad_adc_thread_entry(void *parameter)
{
    (void)parameter;

    while (input_joypad_axis_ready)
    {
        input_joypad_poll();
        rt_thread_mdelay(INPUT_POLL_PERIOD_MS);
    }
}

static rt_err_t input_joypad_adc_thread_start(void)
{
    rt_err_t result;

    if (!input_joypad_axis_ready || input_joypad_adc_thread != RT_NULL)
        return RT_EOK;

    input_joypad_adc_thread = rt_thread_create(
        "joy_adc", input_joypad_adc_thread_entry, RT_NULL,
        INPUT_JOYPAD_THREAD_STACK_SIZE, INPUT_JOYPAD_THREAD_PRIORITY,
        INPUT_JOYPAD_THREAD_TIMESLICE);
    if (input_joypad_adc_thread == RT_NULL)
        return -RT_ENOMEM;

    result = rt_thread_startup(input_joypad_adc_thread);
    if (result != RT_EOK)
    {
        rt_thread_delete(input_joypad_adc_thread);
        input_joypad_adc_thread = RT_NULL;
    }
    else
    {
        LOG_I("Joypad ADC polling thread started");
    }
    return result;
}

static rt_err_t input_profile_init(void)
{
    rt_err_t key_result = input_joypad_keys_init();
    rt_err_t axis_result = input_joypad_adc_init();

    return (key_result == RT_EOK || axis_result == RT_EOK)
               ? RT_EOK
               : -RT_ERROR;
}
#elif defined(FACTORY_HW_USING_KEYBOARD)
static rt_err_t input_expander_set_output(rt_uint8_t pin, rt_uint8_t value)
{
    rt_err_t result;

    result = input_expander_set_mode(pin, XL9555_PIN_OUTPUT);
    if (result != RT_EOK)
        return result;

    return (rt_device_write(input_expander, pin, &value, 1) == 1)
               ? RT_EOK
               : -RT_EIO;
}

static rt_err_t input_keyboard_power_on(void)
{
    rt_err_t result = input_expander_open();

    if (result != RT_EOK)
        return result;

    result = input_expander_set_output(XL9555_WIFI_EN_PIN, 1U);
    if (result == RT_EOK)
    {
        rt_thread_mdelay(20);
        result = input_expander_set_output(XL9555_KEY_LED_EN_PIN, 1U);
    }
    if (result != RT_EOK)
    {
        input_init_errors |= INPUT_INIT_ERROR_XL9555;
        LOG_E("Enable keyboard power rails failed: %d", result);
        return result;
    }

    rt_thread_mdelay(100);
    return RT_EOK;
}

static rt_err_t input_profile_init(void)
{
    rt_int32_t timeout;
    rt_err_t result;

    result = input_keyboard_power_on();
    if (result != RT_EOK)
    {
        input_init_errors |= INPUT_INIT_ERROR_TCA8418;
        return result;
    }

    input_keyboard = rt_device_find(TCA8418_RT_DEVICE_NAME);
    if (input_keyboard == RT_NULL)
    {
        input_init_errors |= INPUT_INIT_ERROR_TCA8418;
        LOG_E("Keyboard device '%s' not found", TCA8418_RT_DEVICE_NAME);
        return -RT_ENOSYS;
    }

    result = rt_device_open(input_keyboard, RT_DEVICE_OFLAG_RDONLY);
    if (result != RT_EOK)
    {
        input_init_errors |= INPUT_INIT_ERROR_TCA8418;
        LOG_E("Open keyboard device '%s' failed: %d",
              TCA8418_RT_DEVICE_NAME, result);
        input_keyboard = RT_NULL;
        return result;
    }

    timeout = (rt_int32_t)rt_tick_from_millisecond(INPUT_POLL_PERIOD_MS);
    result = rt_device_control(input_keyboard,
                               TCA8418_RT_CTRL_SET_READ_TIMEOUT,
                               &timeout);
    if (result == RT_EOK)
    {
        result = rt_device_control(input_keyboard,
                                   TCA8418_RT_CTRL_FLUSH,
                                   RT_NULL);
    }
    if (result != RT_EOK)
    {
        input_init_errors |= INPUT_INIT_ERROR_TCA8418;
        LOG_E("Configure keyboard device failed: %d", result);
    }
    return result;
}
#else
static rt_err_t input_profile_init(void)
{
    return RT_EOK;
}
#endif

static rt_err_t input_buttons_init(void)
{
    button_cfg_t configs[INPUT_BUTTON_COUNT] = {
        {
            .pin = INPUT_BUTTON_33_PIN,
            .active_state = BUTTON_ACTIVE_LOW,
            .mode = PIN_MODE_INPUT_PULLUP,
            .button_handler = input_button_handler,
        },
        {
            .pin = INPUT_BUTTON_34_PIN,
            .active_state = BUTTON_ACTIVE_HIGH,
            .mode = PIN_MODE_INPUT_PULLDOWN,
            .button_handler = input_button_handler,
        },
        {
            .pin = INPUT_BUTTON_35_PIN,
            .active_state = BUTTON_ACTIVE_LOW,
            .mode = PIN_MODE_INPUT_PULLUP,
            .button_handler = input_button_handler,
        },
    };
    rt_uint32_t index;

    for (index = 0; index < INPUT_BUTTON_COUNT; index++)
    {
        input_button_ids[index] = button_init(&configs[index]);
        if (input_button_ids[index] < 0)
        {
            input_init_errors |= INPUT_INIT_ERROR_BUTTONS;
            LOG_E("Register button pin %d failed: %d",
                  input_button_pins[index], input_button_ids[index]);
            return -RT_ERROR;
        }
    }

    for (index = 0; index < INPUT_BUTTON_COUNT; index++)
    {
        if (button_enable(input_button_ids[index]) != 0)
        {
            input_init_errors |= INPUT_INIT_ERROR_BUTTONS;
            LOG_E("Enable button pin %d failed", input_button_pins[index]);
            return -RT_ERROR;
        }
    }
    return RT_EOK;
}

static void input_drain_event_queue(void)
{
    input_event_t event;

    while (rt_mq_recv(input_event_queue, &event, sizeof(event),
                      RT_WAITING_NO) == RT_EOK)
    {
        input_dispatch_event(&event);
    }
}

static void input_thread_entry(void *parameter)
{
    input_event_t event;

    (void)parameter;
    LOG_I("Input event thread started");
    while (1)
    {
#if defined(FACTORY_HW_USING_JOYPAD)
        if (rt_mq_recv(input_event_queue, &event, sizeof(event),
                       rt_tick_from_millisecond(INPUT_POLL_PERIOD_MS)) ==
            RT_EOK)
        {
            input_dispatch_event(&event);
        }
        input_joypad_keys_poll();
        input_drain_event_queue();
#elif defined(FACTORY_HW_USING_KEYBOARD)
        key_board_event_msg_t keyboard_events[INPUT_KEYBOARD_BATCH_SIZE];
        rt_size_t count = 0;
        rt_size_t index;

        if (input_keyboard != RT_NULL)
        {
            count = rt_device_read(input_keyboard, 0, keyboard_events,
                                   INPUT_KEYBOARD_BATCH_SIZE);
        }
        else if (rt_mq_recv(input_event_queue, &event, sizeof(event),
                            rt_tick_from_millisecond(
                                INPUT_POLL_PERIOD_MS)) == RT_EOK)
        {
            input_dispatch_event(&event);
        }

        for (index = 0; index < count; index++)
        {
            event.source = INPUT_SOURCE_KEYBOARD;
            event.action = keyboard_events[index].is_long_press
                               ? INPUT_ACTION_LONG_PRESSED
                               : INPUT_ACTION_CLICKED;
            event.code = (int32_t)keyboard_events[index].code;
            event.timestamp = rt_tick_get();
            input_dispatch_event(&event);
        }
        input_drain_event_queue();
#else
        if (rt_mq_recv(input_event_queue, &event, sizeof(event),
                       RT_WAITING_FOREVER) == RT_EOK)
        {
            input_dispatch_event(&event);
        }
        input_drain_event_queue();
#endif
    }
}

rt_err_t input_service_init(void)
{
    rt_thread_t thread;
    rt_err_t button_result;
    rt_err_t profile_result;
    rt_err_t result;

    if (input_initialized)
        return RT_EOK;
    if (input_init_attempted)
        return input_init_result;
    input_init_attempted = true;
    input_init_errors = INPUT_INIT_ERROR_NONE;

    input_callback_lock = rt_mutex_create("input_cb", RT_IPC_FLAG_FIFO);
    if (input_callback_lock == RT_NULL)
    {
        input_init_result = -RT_ENOMEM;
        return input_init_result;
    }

#if defined(FACTORY_HW_USING_JOYPAD)
    input_joypad_lock = rt_mutex_create("joypad", RT_IPC_FLAG_FIFO);
    if (input_joypad_lock == RT_NULL)
    {
        input_init_result = -RT_ENOMEM;
        return input_init_result;
    }
#endif

    input_event_queue = rt_mq_create("input_q", sizeof(input_event_t),
                                     INPUT_EVENT_QUEUE_DEPTH,
                                     RT_IPC_FLAG_FIFO);
    if (input_event_queue == RT_NULL)
    {
        input_init_result = -RT_ENOMEM;
        return input_init_result;
    }

    profile_result = input_profile_init();
    button_result = input_buttons_init();
    if (profile_result != RT_EOK && button_result != RT_EOK)
    {
        input_init_result = -RT_ERROR;
        return input_init_result;
    }

    thread = rt_thread_create("input", input_thread_entry, RT_NULL,
                              INPUT_THREAD_STACK_SIZE,
                              INPUT_THREAD_PRIORITY,
                              INPUT_THREAD_TIMESLICE);
    if (thread == RT_NULL)
    {
        input_init_result = -RT_ENOMEM;
        return input_init_result;
    }

    result = rt_thread_startup(thread);
    if (result != RT_EOK)
    {
        input_init_result = result;
        return result;
    }

    input_initialized = true;
    input_init_result = RT_EOK;
    if (input_init_errors == INPUT_INIT_ERROR_NONE)
    {
#if defined(FACTORY_HW_USING_JOYPAD)
        LOG_I("Input service initialized: GPIO buttons, joypad, ADC and RGB");
#elif defined(FACTORY_HW_USING_KEYBOARD)
        LOG_I("Input service initialized: GPIO buttons and keyboard '%s'",
              TCA8418_RT_DEVICE_NAME);
#else
        LOG_I("Input service initialized: GPIO buttons");
#endif
    }
    else
    {
        LOG_W("Input service running in degraded mode, errors=0x%02x",
              (unsigned int)input_init_errors);
    }
    return RT_EOK;
}

rt_err_t input_service_start_background(void)
{
#if defined(FACTORY_HW_USING_JOYPAD)
    rt_err_t rgb_result;
    rt_err_t result;

    if (!input_initialized)
        return -RT_ERROR;

    rgb_result = input_joypad_rgb_init();
    result = input_joypad_adc_thread_start();
    if (result != RT_EOK)
    {
        input_init_errors |= INPUT_INIT_ERROR_ADS1115;
        input_joypad_axis_ready = false;
        LOG_E("Start joypad ADC polling thread failed: %d", result);
        return result;
    }
    LOG_I("Joypad background services started after shared devices: "
          "ADC=%d RGB=%d", result, rgb_result);
#endif
    return RT_EOK;
}

bool input_service_is_ready(void)
{
    return input_initialized;
}

input_profile_t input_service_get_profile(void)
{
#if defined(FACTORY_HW_USING_JOYPAD)
    return INPUT_PROFILE_JOYPAD;
#elif defined(FACTORY_HW_USING_KEYBOARD)
    return INPUT_PROFILE_KEYBOARD;
#else
    return INPUT_PROFILE_NONE;
#endif
}

uint32_t input_service_get_init_errors(void)
{
    return input_init_errors;
}

rt_err_t input_service_get_joypad_state(input_joypad_state_t *state)
{
    if (state == RT_NULL)
        return -RT_EINVAL;

#if defined(FACTORY_HW_USING_JOYPAD)
    if (!input_initialized || input_joypad_lock == RT_NULL ||
        (!input_joypad_keys_ready && !input_joypad_axis_ready))
    {
        return -RT_ENOSYS;
    }

    rt_mutex_take(input_joypad_lock, RT_WAITING_FOREVER);
    *state = input_joypad_state;
    rt_mutex_release(input_joypad_lock);
    return RT_EOK;
#else
    return -RT_ENOSYS;
#endif
}

rt_err_t input_service_set_event_callback(input_event_callback_t callback,
                                          void *user_data)
{
    if (!input_initialized || input_callback_lock == RT_NULL)
        return -RT_ERROR;

    rt_mutex_take(input_callback_lock, RT_WAITING_FOREVER);
    input_event_callback = callback;
    input_event_user_data = user_data;
    rt_mutex_release(input_callback_lock);
    return RT_EOK;
}

rt_err_t input_service_set_joypad_rgb(bool enabled, uint8_t brightness,
                                      uint8_t red, uint8_t green,
                                      uint8_t blue)
{
#if defined(FACTORY_HW_USING_JOYPAD)
    rt_err_t result;
    rt_err_t stop_result;
    rt_uint8_t driver_brightness;

    if (!input_initialized || !input_joypad_rgb_ready)
        return -RT_ENOSYS;
    if (brightness > 100U)
        return -RT_EINVAL;

    driver_brightness = (rt_uint8_t)(
        ((rt_uint16_t)brightness * 255U + 50U) / 100U);
    result = ws2812_init();
    if (result == RT_EOK)
        result = ws2812_set_brightness(driver_brightness);
    if (result == RT_EOK)
    {
        result = enabled ? ws2812_fill(red, green, blue)
                         : ws2812_fill(0U, 0U, 0U);
    }
    if (result == RT_EOK)
        result = ws2812_show();
    stop_result = ws2812_deinit();
    if (result == RT_EOK)
        result = stop_result;
    if (result == RT_EOK)
    {
        input_joypad_rgb_enabled = enabled;
        input_joypad_rgb_brightness = brightness;
        input_joypad_rgb_red = red;
        input_joypad_rgb_green = green;
        input_joypad_rgb_blue = blue;
    }
    return result;
#else
    (void)enabled;
    (void)brightness;
    (void)red;
    (void)green;
    (void)blue;
    return -RT_ENOSYS;
#endif
}

rt_err_t input_service_get_joypad_rgb(bool *enabled, uint8_t *brightness,
                                      uint8_t *red, uint8_t *green,
                                      uint8_t *blue)
{
    if (enabled == RT_NULL || brightness == RT_NULL || red == RT_NULL ||
        green == RT_NULL || blue == RT_NULL)
        return -RT_EINVAL;
#if defined(FACTORY_HW_USING_JOYPAD)
    if (!input_initialized || !input_joypad_rgb_ready)
        return -RT_ENOSYS;
    *enabled = input_joypad_rgb_enabled;
    *brightness = input_joypad_rgb_brightness;
    *red = input_joypad_rgb_red;
    *green = input_joypad_rgb_green;
    *blue = input_joypad_rgb_blue;
    return RT_EOK;
#else
    return -RT_ENOSYS;
#endif
}
