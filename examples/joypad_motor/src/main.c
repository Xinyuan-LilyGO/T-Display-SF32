#include "rtthread.h"
#include "rtconfig.h"
#include "ulog.h"
#include "ads1115.h"
#include "xl9555.h"
#include "ws2812.h"

#define ADC_INT 11
#define RGB_LED_COUNT 4U
#define RGB_LED_LEFT   0U
#define RGB_LED_UP     1U
#define RGB_LED_RIGHT  2U
#define RGB_LED_DOWN   3U
#define RGB_BRIGHTNESS 96U
#define RGB_IDLE_BLUE  20U

#define JOYSTICK_CENTER_UV     1650000
#define JOYSTICK_DEAD_ZONE_UV   150000
#define JOYSTICK_ACTIVE_RANGE_UV 1500000
#define RGB_LEVEL_STEP          16U

#ifndef JOYSTICK_USE_INTERRUPT
#define JOYSTICK_USE_INTERRUPT 1
#endif

static ads1115_device_t *adc;
static rt_bool_t rgb_ready;
static ws2812_color_t rgb_last[RGB_LED_COUNT];

static rt_uint8_t joystick_rgb_level(rt_int32_t delta_uv)
{
    rt_uint32_t magnitude;
    rt_uint32_t level;

    magnitude = (rt_uint32_t)(delta_uv < 0 ? -delta_uv : delta_uv);
    if (magnitude <= JOYSTICK_DEAD_ZONE_UV)
        return 0U;
    if (magnitude >= JOYSTICK_ACTIVE_RANGE_UV)
        return 255U;

    level = (magnitude - JOYSTICK_DEAD_ZONE_UV) * 255U /
            (JOYSTICK_ACTIVE_RANGE_UV - JOYSTICK_DEAD_ZONE_UV);
    level = ((level + RGB_LEVEL_STEP / 2U) / RGB_LEVEL_STEP) *
            RGB_LEVEL_STEP;
    return (rt_uint8_t)(level > 255U ? 255U : level);
}

static rt_err_t rgb_show_colors(const ws2812_color_t *colors)
{
    rt_size_t index;
    rt_bool_t changed = RT_FALSE;
    rt_err_t result;

    if (!rgb_ready)
        return -RT_ENOSYS;

    for (index = 0; index < RGB_LED_COUNT; index++)
    {
        if (colors[index].red != rgb_last[index].red ||
                colors[index].green != rgb_last[index].green ||
                colors[index].blue != rgb_last[index].blue)
        {
            changed = RT_TRUE;
        }

        result = ws2812_set_pixel_color(index, colors[index]);
        if (result != RT_EOK)
            return result;
    }

    if (!changed)
        return RT_EOK;

    result = ws2812_show();
    if (result == RT_EOK)
        rt_memcpy(rgb_last, colors, sizeof(rgb_last));
    return result;
}

static void rgb_show_joystick(rt_int32_t x_uv, rt_int32_t y_uv)
{
    ws2812_color_t colors[RGB_LED_COUNT] = {0};
    rt_int32_t x_delta = x_uv - JOYSTICK_CENTER_UV;
    rt_int32_t y_delta = y_uv - JOYSTICK_CENTER_UV;
    rt_uint8_t x_level = joystick_rgb_level(x_delta);
    rt_uint8_t y_level = joystick_rgb_level(y_delta);
    rt_err_t result;

    if (!rgb_ready)
        return;

    if (x_level == 0U && y_level == 0U)
    {
        for (rt_size_t index = 0; index < RGB_LED_COUNT; index++)
            colors[index].blue = RGB_IDLE_BLUE;
    }
    else
    {
        if (x_delta < -JOYSTICK_DEAD_ZONE_UV)
        {
            colors[RGB_LED_LEFT].green = x_level;
            colors[RGB_LED_LEFT].blue = x_level;
        }
        else if (x_delta > JOYSTICK_DEAD_ZONE_UV)
        {
            colors[RGB_LED_RIGHT].red = x_level;
            colors[RGB_LED_RIGHT].blue = x_level;
        }

        /* AIN1 voltage increasing is treated as the up direction. */
        if (y_delta > JOYSTICK_DEAD_ZONE_UV)
        {
            colors[RGB_LED_UP].green = y_level;
        }
        else if (y_delta < -JOYSTICK_DEAD_ZONE_UV)
        {
            colors[RGB_LED_DOWN].red = y_level;
            colors[RGB_LED_DOWN].green = (rt_uint8_t)(y_level / 3U);
        }
    }

    result = rgb_show_colors(colors);
    if (result != RT_EOK)
    {
        LOG_E("update WS2812 display failed: %d", result);
        rgb_ready = RT_FALSE;
    }
}

static void rgb_show_error(void)
{
    ws2812_color_t colors[RGB_LED_COUNT];

    for (rt_size_t index = 0; index < RGB_LED_COUNT; index++)
    {
        colors[index].red = 255U;
        colors[index].green = 0U;
        colors[index].blue = 0U;
    }
    (void)rgb_show_colors(colors);
}

static void rgb_init(void)
{
    rt_err_t result;

    result = ws2812_init();
    if (result != RT_EOK)
    {
        LOG_E("initialize WS2812 failed: %d", result);
        return;
    }
    if (ws2812_get_pixel_count() < RGB_LED_COUNT)
    {
        LOG_E("WS2812 requires at least %u LEDs", RGB_LED_COUNT);
        ws2812_deinit();
        return;
    }

    result = ws2812_set_brightness(RGB_BRIGHTNESS);
    if (result != RT_EOK)
    {
        LOG_E("set WS2812 brightness failed: %d", result);
        ws2812_deinit();
        return;
    }

    rt_memset(rgb_last, 0, sizeof(rgb_last));
    rgb_ready = RT_TRUE;
    rgb_show_joystick(JOYSTICK_CENTER_UV, JOYSTICK_CENTER_UV);
    LOG_I("WS2812 joystick display initialized with %u LEDs",
          RGB_LED_COUNT);
}

#if JOYSTICK_USE_INTERRUPT
typedef enum
{
    JOYSTICK_CHANNEL_X = 0,
    JOYSTICK_CHANNEL_Y
} joystick_channel_t;

static struct rt_semaphore adc_sem;
static joystick_channel_t current_channel = JOYSTICK_CHANNEL_X;
static rt_bool_t discard_conversion;
static rt_int32_t joystick_x_uv;
static rt_int32_t joystick_y_uv;

static void ads1115_isr(void *args)
{
    (void)args;
    rt_pin_irq_enable(ADC_INT, PIN_IRQ_DISABLE);
    rt_sem_release(&adc_sem);
}

static rt_err_t joystick_select_channel(ads1115_device_t *device,
                                        joystick_channel_t channel)
{
    ads1115_mux_t mux;
    rt_err_t result;

    mux = (channel == JOYSTICK_CHANNEL_X)
              ? ADS1115_MUX_AIN0_GND
              : ADS1115_MUX_AIN1_GND;

    /*
     * start_continuous() 会重新生成 CONFIG，并关闭 ALERT/RDY，
     * 所以每次切换后都要重新启用 conversion-ready。
     */
    result = ads1115_start_continuous(device,
                                      mux,
                                      ADS1115_PGA_4_096V,
                                      ADS1115_RATE_250_SPS);
    if (result != RT_EOK)
        return result;

    return ads1115_enable_conversion_ready_pin(device, RT_FALSE);
}

static rt_err_t joystick_interrupt_init(void)
{
    rt_err_t result;

    result = rt_sem_init(&adc_sem, "adc_irq", 0, RT_IPC_FLAG_FIFO);
    if (result != RT_EOK)
        return result;

    rt_pin_mode(ADC_INT, PIN_MODE_INPUT_PULLUP);
    result = rt_pin_attach_irq(ADC_INT, PIN_IRQ_MODE_FALLING,
                               ads1115_isr, RT_NULL);
    if (result != RT_EOK)
    {
        rt_sem_detach(&adc_sem);
        return result;
    }

    current_channel = JOYSTICK_CHANNEL_X;
    result = joystick_select_channel(adc, current_channel);
    if (result != RT_EOK)
    {
        rt_pin_detach_irq(ADC_INT);
        rt_sem_detach(&adc_sem);
        return result;
    }
    discard_conversion = RT_TRUE;

    result = rt_pin_irq_enable(ADC_INT, PIN_IRQ_ENABLE);
    if (result != RT_EOK)
    {
        rt_pin_detach_irq(ADC_INT);
        rt_sem_detach(&adc_sem);
    }
    return result;
}
#endif

static void xl9555_changed(rt_uint16_t state, rt_uint16_t changed, void *user_data)
{
    (void)user_data;

    for (rt_uint8_t pin = 0; pin < 16; pin++)
    {
        rt_uint16_t mask = 1U << pin;

        if (changed & mask)
        {
            rt_kprintf("XL9555 pin %d changed to %s\n",
                       pin,
                       (state & mask) ? "HIGH" : "LOW");
        }
    }
}

#if !JOYSTICK_USE_INTERRUPT
static void read_joystick(void)
{
    rt_int16_t raw_x;
    rt_int16_t raw_y;
    rt_int32_t uv_x;
    rt_int32_t uv_y;

    if (ads1115_read_single_ended(adc,
                                  0, /* AIN0: ADC_X */
                                  ADS1115_PGA_4_096V,
                                  ADS1115_RATE_250_SPS,
                                  &raw_x,
                                  &uv_x) != RT_EOK)
    {
        LOG_E("read ADC_X failed");
        return;
    }

    if (ads1115_read_single_ended(adc,
                                  1, /* AIN1: ADC_Y */
                                  ADS1115_PGA_4_096V,
                                  ADS1115_RATE_250_SPS,
                                  &raw_y,
                                  &uv_y) != RT_EOK)
    {
        LOG_E("read ADC_Y failed");
        return;
    }

    rt_kprintf("X: raw=%d, %d mV; Y: raw=%d, %d mV\n",
               raw_x, (int)(uv_x / 1000),
               raw_y, (int)(uv_y / 1000));
    rgb_show_joystick(uv_x, uv_y);
}
#endif

/**
 * @brief  Main program
 * @param  None
 * @retval 0 if success, otherwise failure number
 */
int main(void)
{
#if JOYSTICK_USE_INTERRUPT
    rt_err_t result;
#endif

    rt_thread_mdelay(1000);
    if (xl9555_init() != RT_EOK)
    {
        LOG_E("xl9555 init failed");
        return -1;
    }
    if (xl9555_irq_attach(xl9555_changed, RT_NULL) != RT_EOK)
    {
        LOG_E("xl9555_irq_attach failed");
    }

    for (int i = 0; i < 8; i++)
    {
        xl9555_pin_mode(i, XL9555_PIN_INPUT);
    }

    rgb_init();

    if (ads1115_init() != RT_EOK)
    {
        LOG_E("ads1115 init failed");
        rgb_show_error();
        return -1;
    }

    adc = ads1115_get_device();
    if (adc == RT_NULL)
    {
        LOG_E("get ads1115 device failed");
        rgb_show_error();
        return -1;
    }

#if JOYSTICK_USE_INTERRUPT
    result = joystick_interrupt_init();
    if (result != RT_EOK)
    {
        LOG_E("initialize ADS1115 interrupt failed: %d", result);
        rgb_show_error();
        return -1;
    }

    joystick_channel_t next_channel;
    rt_int16_t raw;
    rt_int32_t uv;

    while (1)
    {
        if (rt_sem_take(&adc_sem, RT_WAITING_FOREVER) != RT_EOK)
            continue;

        result = ads1115_read_conversion(adc, &raw, &uv);
        if (result != RT_EOK)
        {
            LOG_E("read ADS1115 failed: %d", result);
            rt_pin_irq_enable(ADC_INT, PIN_IRQ_ENABLE);
            continue;
        }

        /* The first result after changing MUX can still belong to the old input. */
        if (discard_conversion)
        {
            discard_conversion = RT_FALSE;
            rt_pin_irq_enable(ADC_INT, PIN_IRQ_ENABLE);
            continue;
        }

        if (current_channel == JOYSTICK_CHANNEL_X)
        {
            joystick_x_uv = uv;
            next_channel = JOYSTICK_CHANNEL_Y;
        }
        else
        {
            joystick_y_uv = uv;
            next_channel = JOYSTICK_CHANNEL_X;

            /* 得到一组完整的 X/Y 数据 */
            rt_kprintf("X=%d mV, Y=%d mV\n",
                       (int)(joystick_x_uv / 1000),
                       (int)(joystick_y_uv / 1000));
            rgb_show_joystick(joystick_x_uv, joystick_y_uv);
        }

        do
        {
            result = joystick_select_channel(adc, next_channel);
            if (result != RT_EOK)
            {
                LOG_E("switch ADS1115 channel failed: %d", result);
                rt_thread_mdelay(10);
            }
        } while (result != RT_EOK);

        current_channel = next_channel;
        discard_conversion = RT_TRUE;
        rt_pin_irq_enable(ADC_INT, PIN_IRQ_ENABLE);
    }
#else
    while (1)
    {
        read_joystick();
        rt_thread_mdelay(20);
    }
#endif
}

static void i2c_scan()
{
    uint8_t i2c_address[8];
    struct rt_i2c_bus_device *i2c_bus = rt_i2c_bus_device_find("i2c1");
    if (i2c_bus == RT_NULL)
    {
        LOG_E("I2C bus %s not found!\n", "i2c1");
        return;
    }

    if (rt_device_open((rt_device_t)i2c_bus, RT_DEVICE_OFLAG_RDWR) != RT_EOK)
    {
        LOG_E("open %s device failed", "i2c1");
        return;
    }

    struct rt_i2c_configuration configuration = {
        .timeout = 500,
        .max_hz = 100000,
    };

    rt_i2c_configure(i2c_bus, &configuration);

    rt_uint8_t found = 0;
    LOG_D("I2C Bus scanning...\n");

    for (uint8_t addr = 0x00; addr <= 0x7f; addr++)
    {
        struct rt_i2c_msg msgs;
        rt_uint8_t dummy = 0x00;

        msgs.addr = addr;
        msgs.flags = RT_I2C_WR;
        msgs.buf = &dummy;
        msgs.len = 1;

        if (rt_i2c_transfer(i2c_bus, &msgs, 1) == 1)
        {
            i2c_address[found] = addr;
            found++;
        }
    }

    if (found == 0)
    {
        LOG_D("No I2C devices found on bus i2c1\n");
    }
    else
    {
        LOG_D("Total %d device(s) found\n", found);
        for (int i = 0; i < found; i++)
        {
            LOG_D("Device %d: 0x%02X", i + 1, i2c_address[i]);
        }
    }
}
MSH_CMD_EXPORT(i2c_scan, i2c scan);
