#include <rtdevice.h>
#include <rtthread.h>

#define DBG_TAG "xl9555.example"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#include "xl9555_rt_device.h"

#ifndef XL9555_EXAMPLE_OUTPUT_PIN
#define XL9555_EXAMPLE_OUTPUT_PIN 15
#endif

#ifndef XL9555_EXAMPLE_INPUT_MASK
#define XL9555_EXAMPLE_INPUT_MASK 0x00FFU
#endif

#ifndef XL9555_EXAMPLE_TOGGLE_PERIOD_MS
#define XL9555_EXAMPLE_TOGGLE_PERIOD_MS 1000
#endif

static rt_device_t g_xl9555;
static rt_uint16_t g_input_mask = XL9555_EXAMPLE_INPUT_MASK;

static rt_err_t xl9555_example_set_mode(rt_uint8_t pin, rt_uint8_t mode)
{
    struct xl9555_rt_pin_mode pin_mode =
    {
        .pin = pin,
        .mode = mode,
    };

    return rt_device_control(g_xl9555, XL9555_RT_CTRL_SET_PIN_MODE,
                             &pin_mode);
}

static rt_err_t xl9555_example_read_pin(rt_uint8_t pin, rt_uint8_t *value)
{
    if (value == RT_NULL || pin >= 16)
        return -RT_EINVAL;

    return (rt_device_read(g_xl9555, pin, value, 1) == 1)
               ? RT_EOK
               : -RT_ERROR;
}

static rt_err_t xl9555_example_read_all(rt_uint16_t *state)
{
    if (state == RT_NULL)
        return -RT_EINVAL;

    return rt_device_control(g_xl9555, XL9555_RT_CTRL_READ_ALL, state);
}

static void xl9555_example_irq(rt_uint16_t input_state,
                               rt_uint16_t changed_mask,
                               void *user_data)
{
    rt_uint16_t relevant_changes;
    rt_uint8_t pin;

    (void)user_data;
    relevant_changes = changed_mask & g_input_mask;
    if (relevant_changes == 0)
        return;

    rt_kprintf("XL9555 IRQ: state=0x%04X, changed=0x%04X\n",
               input_state, relevant_changes);
    for (pin = 0; pin < 16; pin++)
    {
        rt_uint16_t mask = (rt_uint16_t)(1U << pin);

        if (relevant_changes & mask)
        {
            rt_kprintf("  P%d_%d -> %s\n", pin / 8, pin % 8,
                       (input_state & mask) ? "HIGH" : "LOW");
        }
    }
}

static rt_err_t xl9555_example_configure(void)
{
    struct xl9555_rt_irq_config irq_config =
    {
        .callback = xl9555_example_irq,
        .user_data = RT_NULL,
    };
    rt_bool_t irq_enable = RT_TRUE;
    rt_uint8_t pin;
    rt_err_t result;

    if (XL9555_EXAMPLE_OUTPUT_PIN >= 16)
        return -RT_EINVAL;

    /* An output pin cannot also be monitored as an interrupt input. */
    g_input_mask &= (rt_uint16_t)~(1U << XL9555_EXAMPLE_OUTPUT_PIN);

    result = xl9555_example_set_mode(XL9555_EXAMPLE_OUTPUT_PIN,
                                     XL9555_PIN_OUTPUT);
    if (result != RT_EOK)
        return result;

    for (pin = 0; pin < 16; pin++)
    {
        if (g_input_mask & (rt_uint16_t)(1U << pin))
        {
            result = xl9555_example_set_mode(pin, XL9555_PIN_INPUT);
            if (result != RT_EOK)
                return result;
        }
    }

    if (g_input_mask == 0)
        return RT_EOK;

    result = rt_device_control(g_xl9555, XL9555_RT_CTRL_ATTACH_IRQ,
                               &irq_config);
    if (result != RT_EOK)
        return result;

    return rt_device_control(g_xl9555, XL9555_RT_CTRL_ENABLE_IRQ,
                             &irq_enable);
}

static int xl9555_dump(int argc, char **argv)
{
    rt_uint16_t state;

    (void)argc;
    (void)argv;

    if (g_xl9555 == RT_NULL)
    {
        rt_kprintf("XL9555 device is not ready\n");
        return -RT_ENOSYS;
    }

    if (xl9555_example_read_all(&state) != RT_EOK)
    {
        rt_kprintf("Read XL9555 IO failed\n");
        return -RT_ERROR;
    }

    rt_kprintf("XL9555 IO state=0x%04X, inputs=0x%04X\n",
               state, state & g_input_mask);
    return RT_EOK;
}
MSH_CMD_EXPORT(xl9555_dump, read all XL9555 IO states);

int main(void)
{
    rt_uint8_t output = 0;
    rt_uint8_t output_readback;
    rt_uint16_t state;
    rt_err_t result;

    rt_thread_mdelay(1000);

    g_xl9555 = rt_device_find(XL9555_RT_DEVICE_NAME);
    if (g_xl9555 == RT_NULL)
    {
        LOG_E("device '%s' was not found", XL9555_RT_DEVICE_NAME);
        return -RT_ENOSYS;
    }

    result = rt_device_open(g_xl9555, RT_DEVICE_OFLAG_RDWR);
    if (result != RT_EOK)
    {
        LOG_E("open '%s' failed: %d", XL9555_RT_DEVICE_NAME, result);
        return result;
    }

    result = xl9555_example_configure();
    if (result != RT_EOK)
    {
        LOG_E("configure XL9555 example failed: %d", result);
        rt_device_close(g_xl9555);
        return result;
    }

    LOG_I("output=P%d_%d, input_mask=0x%04X, MCU IRQ pin=%d",
          XL9555_EXAMPLE_OUTPUT_PIN / 8,
          XL9555_EXAMPLE_OUTPUT_PIN % 8,
          g_input_mask, XL9555_IRQ_PIN);

    while (1)
    {
        output = output ? 0U : 1U;
        if (rt_device_write(g_xl9555, XL9555_EXAMPLE_OUTPUT_PIN,
                            &output, 1) != 1)
        {
            LOG_E("write output pin failed");
        }
        else if (xl9555_example_read_pin(XL9555_EXAMPLE_OUTPUT_PIN,
                                         &output_readback) != RT_EOK)
        {
            LOG_E("read output pin failed");
        }
        else if (xl9555_example_read_all(&state) != RT_EOK)
        {
            LOG_E("read all IO failed");
        }
        else
        {
            rt_kprintf("OUT P%d_%d=%s (read=%s), inputs=0x%04X\n",
                       XL9555_EXAMPLE_OUTPUT_PIN / 8,
                       XL9555_EXAMPLE_OUTPUT_PIN % 8,
                       output ? "HIGH" : "LOW",
                       output_readback ? "HIGH" : "LOW",
                       state & g_input_mask);
        }

        rt_thread_mdelay(XL9555_EXAMPLE_TOGGLE_PERIOD_MS);
    }
}
