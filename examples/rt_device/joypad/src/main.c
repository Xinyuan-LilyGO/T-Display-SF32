#include <drivers/adc.h>
#include <rtdevice.h>
#include <rtthread.h>

#define DBG_TAG "joypad"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#include "ads1115_rt_adc.h"
#include "aw86224_rt_device.h"
#include "xl9555_rt_device.h"

#ifdef JOYPAD_USING_ADS1115_IRQ
#include "ads1115.h"
#endif

#define JOYSTICK_ADC_NAME ADS1115_ADC_DEVICE_NAME
#define JOYSTICK_X_CHANNEL 0U /* ADC_X is connected to AIN0. */
#define JOYSTICK_Y_CHANNEL 1U /* ADC_Y is connected to AIN1. */
#define JOYSTICK_POLL_PERIOD_MS 50U
#define JOYSTICK_ADC_INT_PIN 11
#define JOYPAD_BUTTON_MASK 0x007FU /* XL9555 P0_0..P0_6, active low. */
#define JOYPAD_HAPTIC_GAIN 0x80U
#define JOYPAD_HAPTIC_WAVE_ID 1U
#define JOYPAD_BUTTON_DEBOUNCE_MS 80U

static rt_adc_device_t g_joystick_adc;
static rt_device_t g_xl9555;
static rt_device_t g_motor;
static rt_tick_t g_last_haptic_tick;

static rt_err_t joypad_vibrate(void)
{
    struct aw86224_rt_ram_effect effect = {
        .wave_id = JOYPAD_HAPTIC_WAVE_ID,
        .loop = 0,
        .auto_brake = RT_TRUE,
    };

    if (g_motor == RT_NULL)
        return -RT_ENOSYS;

    return rt_device_control(g_motor, AW86224_RT_CTRL_PLAY_RAM, &effect);
}

static void joypad_button_changed(rt_uint16_t state, rt_uint16_t changed,
                                  void *user_data)
{
    rt_uint16_t pressed;
    rt_tick_t now;
    rt_err_t result;

    (void)user_data;
    pressed = changed & JOYPAD_BUTTON_MASK & (rt_uint16_t)(~state);
    if (pressed == 0U)
        return;

    now = rt_tick_get();
    if (g_last_haptic_tick != 0 &&
        now - g_last_haptic_tick <
            rt_tick_from_millisecond(JOYPAD_BUTTON_DEBOUNCE_MS))
    {
        return;
    }
    g_last_haptic_tick = now;

    result = joypad_vibrate();
    if (result != RT_EOK)
        LOG_E("button vibration failed: %d", result);
    else
        LOG_I("button pressed: mask=0x%02x", pressed);
}

static rt_err_t joypad_motor_init(void)
{
    rt_uint8_t gain = JOYPAD_HAPTIC_GAIN;
    rt_err_t result;

    g_motor = rt_device_find(AW86224_RT_DEVICE_NAME);
    if (g_motor == RT_NULL)
    {
        LOG_E("motor device '%s' was not found", AW86224_RT_DEVICE_NAME);
        return -RT_ENOSYS;
    }

    result = rt_device_open(g_motor, RT_DEVICE_OFLAG_RDWR);
    if (result != RT_EOK)
        return result;

    result = rt_device_control(g_motor, AW86224_RT_CTRL_SET_GAIN, &gain);
    if (result != RT_EOK)
    {
        rt_device_close(g_motor);
        g_motor = RT_NULL;
    }
    return result;
}

static rt_err_t joypad_buttons_init(void)
{
    struct xl9555_rt_irq_config irq_config = {
        .callback = joypad_button_changed,
        .user_data = RT_NULL,
    };
    struct xl9555_rt_pin_mode pin_mode = {
        .mode = XL9555_PIN_INPUT,
    };
    rt_bool_t irq_enable = RT_TRUE;
    rt_uint8_t pin;
    rt_err_t result;

    g_xl9555 = rt_device_find(XL9555_RT_DEVICE_NAME);
    if (g_xl9555 == RT_NULL)
    {
        LOG_E("IO expander device '%s' was not found", XL9555_RT_DEVICE_NAME);
        return -RT_ENOSYS;
    }

    result = rt_device_open(g_xl9555, RT_DEVICE_OFLAG_RDWR);
    if (result != RT_EOK)
        return result;

    for (pin = 0; pin < 7U; pin++)
    {
        pin_mode.pin = pin;
        result = rt_device_control(g_xl9555, XL9555_RT_CTRL_SET_PIN_MODE,
                                   &pin_mode);
        if (result != RT_EOK)
            goto fail;
    }

    result = rt_device_control(g_xl9555, XL9555_RT_CTRL_ATTACH_IRQ,
                               &irq_config);
    if (result != RT_EOK)
        goto fail;

    result = rt_device_control(g_xl9555, XL9555_RT_CTRL_ENABLE_IRQ,
                               &irq_enable);
    if (result == RT_EOK)
        return RT_EOK;

fail:
    rt_device_close(g_xl9555);
    g_xl9555 = RT_NULL;
    return result;
}

static rt_err_t joystick_adc_init(void)
{
    rt_err_t result;

    g_joystick_adc =
        (rt_adc_device_t)rt_device_find(JOYSTICK_ADC_NAME);
    if (g_joystick_adc == RT_NULL)
    {
        LOG_E("ADC device '%s' was not found", JOYSTICK_ADC_NAME);
        return -RT_ENOSYS;
    }

    result = rt_adc_enable(g_joystick_adc, JOYSTICK_X_CHANNEL);
    if (result != RT_EOK)
        return result;

    return rt_adc_enable(g_joystick_adc, JOYSTICK_Y_CHANNEL);
}

#ifndef JOYPAD_USING_ADS1115_IRQ
static rt_err_t joystick_read_channel_mv(rt_uint32_t channel,
                                         rt_int32_t *millivolts)
{
    rt_adc_cmd_read_arg_t read_arg =
        {
            .channel = channel,
            .value = 0,
        };
    rt_err_t result;

    if (millivolts == RT_NULL)
        return -RT_EINVAL;

    result = rt_device_control((rt_device_t)g_joystick_adc,
                               RT_ADC_CMD_READ, &read_arg);
    if (result != RT_EOK)
        return result;

    /* This SiFli ADC interface reports one unit as 0.1 mV. */
    *millivolts = (rt_int32_t)(read_arg.value / 10U);
    return RT_EOK;
}

static void joystick_poll(void)
{
    rt_int32_t x_mv;
    rt_int32_t y_mv;
    rt_err_t result;

    result = joystick_read_channel_mv(JOYSTICK_X_CHANNEL, &x_mv);
    if (result != RT_EOK)
    {
        LOG_E("read X/AIN0 failed: %d", result);
        return;
    }

    result = joystick_read_channel_mv(JOYSTICK_Y_CHANNEL, &y_mv);
    if (result != RT_EOK)
    {
        LOG_E("read Y/AIN1 failed: %d", result);
        return;
    }

    rt_kprintf("X=%d mV, Y=%d mV\n", (int)x_mv, (int)y_mv);
}
#else
typedef enum
{
    JOYSTICK_AXIS_X = 0,
    JOYSTICK_AXIS_Y,
} joystick_axis_t;

static ads1115_device_t *g_ads1115;
static struct rt_semaphore g_adc_ready;
static joystick_axis_t g_current_axis;
static rt_bool_t g_discard_conversion;
static rt_int32_t g_x_uv;
static rt_int32_t g_y_uv;

static void joystick_adc_isr(void *parameter)
{
    (void)parameter;

    rt_pin_irq_enable(JOYSTICK_ADC_INT_PIN, PIN_IRQ_DISABLE);
    rt_sem_release(&g_adc_ready);
}

static rt_err_t joystick_select_axis(joystick_axis_t axis)
{
    ads1115_mux_t mux;
    rt_err_t result;

    mux = (axis == JOYSTICK_AXIS_X)
              ? ADS1115_MUX_AIN0_GND
              : ADS1115_MUX_AIN1_GND;

    result = ads1115_start_continuous(g_ads1115, mux,
                                      ADS1115_PGA_4_096V,
                                      ADS1115_RATE_250_SPS);
    if (result != RT_EOK)
        return result;

    /* start_continuous() disables the comparator, so restore ALERT/RDY. */
    return ads1115_enable_conversion_ready_pin(g_ads1115, RT_FALSE);
}

static rt_err_t joystick_interrupt_init(void)
{
    rt_err_t result;

    g_ads1115 = ads1115_get_device();
    if (g_ads1115 == RT_NULL)
        return -RT_ENOSYS;

    result = rt_sem_init(&g_adc_ready, "adc_rdy", 0, RT_IPC_FLAG_FIFO);
    if (result != RT_EOK)
        return result;

    rt_pin_mode(JOYSTICK_ADC_INT_PIN, PIN_MODE_INPUT_PULLUP);
    result = rt_pin_attach_irq(JOYSTICK_ADC_INT_PIN,
                               PIN_IRQ_MODE_FALLING,
                               joystick_adc_isr, RT_NULL);
    if (result != RT_EOK)
        goto fail_sem;

    g_current_axis = JOYSTICK_AXIS_X;
    result = joystick_select_axis(g_current_axis);
    if (result != RT_EOK)
        goto fail_irq;

    g_discard_conversion = RT_TRUE;
    result = rt_pin_irq_enable(JOYSTICK_ADC_INT_PIN, PIN_IRQ_ENABLE);
    if (result == RT_EOK)
        return RT_EOK;

fail_irq:
    rt_pin_detach_irq(JOYSTICK_ADC_INT_PIN);
fail_sem:
    rt_sem_detach(&g_adc_ready);
    return result;
}

static void joystick_interrupt_process(void)
{
    joystick_axis_t next_axis;
    rt_int16_t raw;
    rt_int32_t microvolts;
    rt_err_t result;

    if (rt_sem_take(&g_adc_ready, RT_WAITING_FOREVER) != RT_EOK)
        return;

    result = ads1115_read_conversion(g_ads1115, &raw, &microvolts);
    if (result != RT_EOK)
    {
        LOG_E("read ADS1115 conversion failed: %d", result);
        rt_pin_irq_enable(JOYSTICK_ADC_INT_PIN, PIN_IRQ_ENABLE);
        return;
    }

    /* The first conversion after a MUX change may belong to the old input. */
    if (g_discard_conversion)
    {
        g_discard_conversion = RT_FALSE;
        rt_pin_irq_enable(JOYSTICK_ADC_INT_PIN, PIN_IRQ_ENABLE);
        return;
    }

    if (g_current_axis == JOYSTICK_AXIS_X)
    {
        g_x_uv = microvolts;
        next_axis = JOYSTICK_AXIS_Y;
    }
    else
    {
        g_y_uv = microvolts;
        next_axis = JOYSTICK_AXIS_X;
        rt_kprintf("X=%d mV, Y=%d mV\n",
                   (int)(g_x_uv / 1000), (int)(g_y_uv / 1000));
    }

    do
    {
        result = joystick_select_axis(next_axis);
        if (result != RT_EOK)
        {
            LOG_E("switch ADS1115 axis failed: %d", result);
            rt_thread_mdelay(10);
        }
    } while (result != RT_EOK);

    g_current_axis = next_axis;
    g_discard_conversion = RT_TRUE;
    rt_pin_irq_enable(JOYSTICK_ADC_INT_PIN, PIN_IRQ_ENABLE);
}
#endif

int main(void)
{
    rt_err_t result;

    rt_thread_mdelay(1000);

    result = joypad_motor_init();
    if (result != RT_EOK)
    {
        LOG_E("initialize AW86224 motor failed: %d", result);
        return result;
    }

    result = joypad_buttons_init();
    if (result != RT_EOK)
    {
        LOG_E("initialize joypad buttons failed: %d", result);
        return result;
    }

    result = joystick_adc_init();
    if (result != RT_EOK)
    {
        LOG_E("initialize joystick ADC failed: %d", result);
        return result;
    }

#ifdef JOYPAD_USING_ADS1115_IRQ
    result = joystick_interrupt_init();
    if (result != RT_EOK)
    {
        LOG_E("initialize ADS1115 interrupt failed: %d", result);
        return result;
    }

    LOG_I("ADS1115 interrupt mode, X=AIN0, Y=AIN1, ALERT/RDY=PIN%d",
          JOYSTICK_ADC_INT_PIN);
    while (1)
        joystick_interrupt_process();
#else
    LOG_I("RT-Thread ADC polling mode, device=%s, X=AIN0, Y=AIN1",
          JOYSTICK_ADC_NAME);

    while (1)
    {
        joystick_poll();
        rt_thread_mdelay(JOYSTICK_POLL_PERIOD_MS);
    }
#endif
}
