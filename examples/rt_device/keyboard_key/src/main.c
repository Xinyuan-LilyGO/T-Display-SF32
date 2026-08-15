#include "aw21009.h"
#include "bf0_hal.h"
#include "drv_io.h"
#include "rtdevice.h"
#include "rtthread.h"
#include "tca8418_rt_device.h"
#include "xl9555.h"

#define DBG_TAG "keyboard.app"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define POWER_HOLD_PIN 41
#define KEY_LED_ON_BRIGHTNESS LED_BRIGHT_50
#define KEY_EVENT_BATCH_SIZE 10U

static rt_bool_t keyboard_led_on;

static void board_power_hold(void)
{
    HAL_PIN_Set(PAD_PA41, GPIO_A41, PIN_NOPULL, 1);
    rt_pin_mode(POWER_HOLD_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(POWER_HOLD_PIN, PIN_HIGH);
}

static rt_err_t keyboard_power_on(void)
{
    rt_err_t result;

    result = xl9555_init();
    if (result != RT_EOK)
        return result;

    /* TCA8418 and AW21009 are supplied by the keyboard board rails. */
    xl9555_pin_mode(XL9555_WIFI_EN_PIN, XL9555_PIN_OUTPUT);
    xl9555_digital_write(XL9555_WIFI_EN_PIN, 1);
    rt_thread_mdelay(20);

    xl9555_pin_mode(XL9555_KEY_LED_EN_PIN, XL9555_PIN_OUTPUT);
    xl9555_digital_write(XL9555_KEY_LED_EN_PIN, 1);
    rt_thread_mdelay(100);
    return RT_EOK;
}

static const char *key_name(KeyCode code)
{
    switch (code)
    {
    case KEY_PREV:
        return "PREV";
    case KEY_ESC:
        return "ESC";
    case KEY_HOME:
        return "HOME";
    case KEY_MAIL:
        return "MAIL";
    case KEY_ENTER:
        return "ENTER";
    case KEY_1:
        return "1";
    case KEY_2:
        return "2";
    case KEY_3:
        return "3";
    case KEY_NEXT:
        return "NEXT";
    case KEY_4:
        return "4";
    case KEY_5:
        return "5";
    case KEY_6:
        return "6";
    case KEY_COLSE:
        return "CLOSE";
    case KEY_7:
        return "7";
    case KEY_8:
        return "8";
    case KEY_9:
        return "9";
    case KEY_STOP:
        return "STOP";
    case KEY_MUL:
        return "*";
    case KEY_0:
        return "0";
    case KEY_WELL:
        return "#";
    default:
        return "UNKNOWN";
    }
}

static rt_err_t keyboard_led_set(rt_bool_t enabled)
{
    rt_err_t result;

    result = aw21009_set_all_brightness(enabled ? KEY_LED_ON_BRIGHTNESS
                                                 : LED_BRIGHT_0);
    if (result == RT_EOK)
    {
        keyboard_led_on = enabled;
        LOG_I("keyboard LEDs %s", enabled ? "ON" : "OFF");
    }
    return result;
}

int main(void)
{
    rt_device_t keyboard;
    key_board_event_msg_t events[KEY_EVENT_BATCH_SIZE];
    rt_size_t event_count;
    rt_size_t index;
    rt_err_t result;

    board_power_hold();

    result = keyboard_power_on();
    if (result != RT_EOK)
    {
        LOG_E("keyboard board power initialization failed: %d", result);
        return result;
    }

    keyboard = rt_device_find(TCA8418_RT_DEVICE_NAME);
    if (keyboard == RT_NULL)
    {
        LOG_E("keyboard device '%s' was not registered",
              TCA8418_RT_DEVICE_NAME);
        return -RT_ENOSYS;
    }

    result = rt_device_open(keyboard, RT_DEVICE_FLAG_RDONLY);
    if (result != RT_EOK)
    {
        LOG_E("open keyboard device '%s' failed: %d",
              TCA8418_RT_DEVICE_NAME, result);
        return result;
    }

    result = rt_device_control(keyboard, TCA8418_RT_CTRL_FLUSH, RT_NULL);
    if (result != RT_EOK)
    {
        LOG_E("flush keyboard events failed: %d", result);
        rt_device_close(keyboard);
        return result;
    }

    result = aw21009_app_init();
    if (result != RT_EOK)
    {
        LOG_E("initialize AW21009 keyboard LEDs failed: %d", result);
        rt_device_close(keyboard);
        return result;
    }

    result = keyboard_led_set(RT_FALSE);
    if (result != RT_EOK)
    {
        LOG_E("turn keyboard LEDs off failed: %d", result);
        rt_device_close(keyboard);
        return result;
    }

    LOG_I("keyboard '%s' ready; press ENTER to toggle LEDs",
          TCA8418_RT_DEVICE_NAME);

    while (1)
    {
        event_count = rt_device_read(keyboard, 0, events,
                                     KEY_EVENT_BATCH_SIZE);
        for (index = 0; index < event_count; index++)
        {
            LOG_I("key=%s, code=%d, long=%d", key_name(events[index].code),
                  events[index].code, events[index].is_long_press ? 1 : 0);

            if (events[index].code == KEY_ENTER)
            {
                result = keyboard_led_set(!keyboard_led_on);
                if (result != RT_EOK)
                    LOG_E("toggle keyboard LEDs failed: %d", result);
            }
        }
    }
}
