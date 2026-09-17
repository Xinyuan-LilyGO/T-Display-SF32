#include "platform_power.h"

#include <rtdevice.h>
#include <rthw.h>
#include <ulog.h>

#include <bf0_hal.h>

#include "battery_service.h"

#define MAIN_3V3_EN_PIN     41
#define PLATFORM_WAKEUP_PIN 34

static void platform_power_wakeup_handler(void *args)
{
    (void)args;
}

static void platform_power_set_wakeup_src(void)
{
    rt_err_t result;

    HAL_PIN_Set(PAD_PA34, GPIO_A34, PIN_PULLDOWN, 1);
    HAL_HPAON_EnableWakeupSrc(HPAON_WAKEUP_SRC_PIN10,
                              AON_PIN_MODE_POS_EDGE);
    rt_pin_mode(PLATFORM_WAKEUP_PIN, PIN_MODE_INPUT);

    /* PA34 is also a runtime button. Hand its GPIO IRQ to the wake handler
     * immediately before shutdown. */
    rt_pin_irq_enable(PLATFORM_WAKEUP_PIN, PIN_IRQ_DISABLE);
    rt_pin_detach_irq(PLATFORM_WAKEUP_PIN);
    result = rt_pin_attach_irq(
        PLATFORM_WAKEUP_PIN, PIN_IRQ_MODE_RISING,
        platform_power_wakeup_handler,
        (void *)(rt_uint32_t)PLATFORM_WAKEUP_PIN);
    if (result == RT_EOK)
        rt_pin_irq_enable(PLATFORM_WAKEUP_PIN, PIN_IRQ_ENABLE);
    else
        LOG_W("Attach PA34 wake IRQ failed: %d", result);
}

rt_err_t platform_power_init(void)
{
    /* 主板3.3V电源 */
    rt_pin_mode(MAIN_3V3_EN_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(MAIN_3V3_EN_PIN, PIN_HIGH);

    /* 等待LDO和I2C设备电源稳定。 */
    rt_thread_mdelay(50);

    LOG_I("Platform power initialized");
    return RT_EOK;
}

void platform_power_deinit(void)
{
    rt_pin_write(MAIN_3V3_EN_PIN, PIN_LOW);
}

void platform_power_shutdown(void)
{
    int8_t wakeup_pin;
    rt_err_t result;

    platform_power_set_wakeup_src();
    wakeup_pin = HAL_HPAON_QueryWakeupPin(hwp_gpio1,
                                          PLATFORM_WAKEUP_PIN);
    if (wakeup_pin >= 0)
    {
        HAL_PMU_SelectWakeupPin(0U, (uint8_t)wakeup_pin);
        HAL_PMU_EnablePinWakeup(0U, AON_PIN_MODE_HIGH);
    }

    LOG_I("Power off: entering SGM41562B shipping mode");
    result = battery_service_enter_shipping_mode();
    if (result != RT_EOK)
    {
        LOG_W("Enter SGM41562B shipping mode failed: %d; use PMU shutdown",
              result);
    }

    /* If shipping mode cannot remove VSYS immediately (for example while
     * USB is already connected), PMU shutdown remains the final fallback. */
    platform_power_deinit();
    rt_hw_interrupt_disable();
    HAL_PMU_EnterShutdown();
    while (1)
    {
    }
}

void platform_power_restart(void)
{
    rt_hw_cpu_reset();
    while (1)
    {
    }
}
