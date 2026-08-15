#include "rtthread.h"
#include "drv_io.h"
#include "bf0_hal.h"
#include "stdio.h"
#include "board.h"
#include "string.h"
#include "drivers/rt_drv_pwm.h"
#include <rtdevice.h>

#define DBG_TAG "GPIO"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

#define OUT_PIN 41
#define IN_PIN 42
#define hwp_gpio hwp_gpio1

rt_device_t device;
rt_device_t rgb_device;
bool led_status = RT_FALSE;
struct rt_color
{
    char *color_name;
    uint32_t color;
};
struct rt_color rgb_color_arry[] =
    {
        {"black", 0x000000},
        {"blue", 0x0000ff},
        {"green", 0x00ff00},
        {"cyan", 0x00ffff},
        {"red", 0xff0000},
        {"purple", 0xff00ff},
        {"yellow", 0xffff00},
        {"white", 0xffffff}};

void gpio_irq_callback(void *args)
{
    rt_kprintf("gpio irq callback %d\n", (rt_uint32_t)args);

    struct rt_device_pin_status status;
    status.pin = OUT_PIN;
    rt_device_read(device, 0, &status, sizeof(struct rt_device_pin_status));
    LOG_I("OUT_PIN %d status: %d\n", OUT_PIN, status.status);

    status.pin = IN_PIN;
    rt_device_read(device, 0, &status, sizeof(struct rt_device_pin_status));
    LOG_I("IN_PIN %d status: %d\n", IN_PIN, status.status);
}

void gpio_init(void)
{
    device = rt_device_find("pin");

    // 1. pin mux
    HAL_PIN_Set(PAD_PA00 + OUT_PIN, GPIO_A0 + OUT_PIN, PIN_PULLUP, 1);
    HAL_PIN_Set(PAD_PA00 + IN_PIN, GPIO_A0 + IN_PIN, PIN_PULLDOWN, 1);

    // 2. open gpio device
    if (!device)
        LOG_I("Find device gpio fail\n");

    rt_device_open(device, RT_DEVICE_OFLAG_RDWR);

    struct rt_device_pin_mode mode;
    mode.pin = OUT_PIN;
    mode.mode = PIN_MODE_OUTPUT;
    rt_device_control(device, 0, &mode);

    mode.pin = IN_PIN;
    mode.mode = PIN_MODE_INPUT;
    rt_device_control(device, 0, &mode);

    rt_pin_attach_irq(mode.pin, PIN_IRQ_MODE_RISING_FALLING, gpio_irq_callback, (void *)(rt_uint32_t)mode.pin);
    rt_pin_irq_enable(mode.pin, PIN_IRQ_ENABLE);
}

void rgb_led_init(void)
{
    rgb_device = rt_device_find("rgbled");
    if (!rgb_device)
    {
        LOG_I("Find device gpio fail\n");
        RT_ASSERT(0);
    }
}

int main()
{
    rt_kprintf("Hello T-Display-SF32!\n");
    rt_pin_mode(BSP_LED1_PIN, PIN_MODE_OUTPUT);
    gpio_init();
    rgb_led_init();
    struct rt_device_pin_status status;
    uint8_t i = 0;

    while (1)
    {
        status.pin = OUT_PIN;
        status.status ^= 1;
        rt_device_write(device, 0, &status, sizeof(struct rt_device_pin_status));

        if (i < sizeof(rgb_color_arry) / sizeof(struct rt_color))
        {
            HAL_PIN_Set(PAD_PA32,GPTIM2_CH1,PIN_NOPULL,1); // RGB LED 52x  pwm3_cc1
            rt_kprintf("-> %s\n", rgb_color_arry[i].color_name);
            struct rt_rgbled_configuration configuration;
            configuration.color_rgb = rgb_color_arry[i].color;
            rt_device_control(rgb_device, PWM_CMD_SET_COLOR, &configuration);
            i++;
        }
        else
        {
            i = 0;
        }

        rt_thread_mdelay(3000);
    }
}
