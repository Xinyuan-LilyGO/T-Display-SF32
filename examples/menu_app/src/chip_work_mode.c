#include "chip_work_mode.h"
#include <rtdevice.h>
#include "rtthread.h"
#include "rtconfig.h"
#include <board.h>
#include <string.h>
#include "log.h"
#include "bf0_hal_wdt.h"

#define WAKEUP_PIN 34
#define LOD_PIN 41

static void gpio_wakeup_handler(void *args)
{
    rt_kprintf("pin34 gpio_wakeup_handler!\n");
}

void set_wakeup_src(void)
{
    uint8_t pin;
    int8_t wakeup_pin;
    HAL_PIN_Set(PAD_PA41, GPIO_A41, PIN_PULLUP, 1);   // set PA41 to GPIO funtion
    rt_pin_mode(LOD_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(LOD_PIN, PIN_HIGH);

    HAL_PIN_Set(PAD_PA34, GPIO_A34, PIN_PULLDOWN, 1); // set PA34 to GPIO funtion
    HAL_HPAON_EnableWakeupSrc(HPAON_WAKEUP_SRC_PIN10, AON_PIN_MODE_POS_EDGE); // Enable #WKUP_PIN10 (PA34)
    rt_pin_mode(WAKEUP_PIN, PIN_MODE_INPUT);

    rt_pin_attach_irq(WAKEUP_PIN, PIN_IRQ_MODE_RISING, (void *)gpio_wakeup_handler,
                      (void *)(rt_uint32_t)WAKEUP_PIN); // PA34 GPIO interrupt
    rt_pin_irq_enable(WAKEUP_PIN, 1);
}

void into_shotdown(void)
{
    log_d("SF32LB52X entry_hibernate\n");
    HAL_PMU_SelectWakeupPin(0, HAL_HPAON_QueryWakeupPin(hwp_gpio1, BSP_KEY1_PIN)); // select PA34 to wake_pin0 BSP_KEY1_PIN
    HAL_PMU_EnablePinWakeup(0, AON_PIN_MODE_HIGH);                                 // enable wake_pin0
    hwp_pmuc->WKUP_CNT = 0x50005;                                                  // 31-16bit:config PIN1 wake CNT , 15-0bit:PIN0 wake CNT
    log_d("SF32LB52X CR:0x%x,WER:0x%x\n", hwp_pmuc->CR, hwp_pmuc->WER);
    
    HAL_PIN_Set(PAD_PA24, GPIO_A24, PIN_PULLDOWN, 1); // #WKUP_PIN0
    HAL_PIN_Set(PAD_PA25, GPIO_A25, PIN_PULLDOWN, 1); // #WKUP_PIN1
    HAL_PIN_Set(PAD_PA26, GPIO_A26, PIN_PULLDOWN, 1); // #WKUP_PIN2
    HAL_PIN_Set(PAD_PA27, GPIO_A27, PIN_PULLDOWN, 1); // #WKUP_PIN3

    HAL_PIN_Set(PAD_PA34, GPIO_A34, PIN_PULLDOWN, 1); // #WKUP_PIN10
    HAL_PIN_Set(PAD_PA35, GPIO_A35, PIN_PULLDOWN, 1); // #WKUP_PIN11
    HAL_PIN_Set(PAD_PA36, GPIO_A36, PIN_PULLDOWN, 1); // #WKUP_PIN12
    HAL_PIN_Set(PAD_PA37, GPIO_A37, PIN_PULLDOWN, 1); // #WKUP_PIN13
    HAL_PIN_Set(PAD_PA38, GPIO_A38, PIN_PULLDOWN, 1); // #WKUP_PIN14
    HAL_PIN_Set(PAD_PA39, GPIO_A39, PIN_PULLDOWN, 1); // #WKUP_PIN15
    HAL_PIN_Set(PAD_PA40, GPIO_A40, PIN_PULLDOWN, 1); // #WKUP_PIN16
    HAL_PIN_Set(PAD_PA41, GPIO_A41, PIN_PULLDOWN, 1); // #WKUP_PIN17
    HAL_PIN_Set(PAD_PA42, GPIO_A42, PIN_PULLDOWN, 1); // #WKUP_PIN18
    HAL_PIN_Set(PAD_PA43, GPIO_A43, PIN_PULLDOWN, 1); // #WKUP_PIN19
    HAL_PIN_Set(PAD_PA44, GPIO_A44, PIN_PULLDOWN, 1); // #WKUP_PIN20

    rt_hw_interrupt_disable();
    HAL_PMU_ConfigPeriLdo(PMUC_PERI_LDO_EN_VDD33_LDO3_Pos, false, false);
    HAL_PMU_ConfigPeriLdo(PMUC_PERI_LDO_EN_VDD33_LDO2_Pos, false, false);
    HAL_PMU_ConfigPeriLdo(PMU_PERI_LDO_1V8, false, false);
    HAL_PMU_EnterHibernate();
    while (1)
        ;
}

void wdt_dog_reset(void)
{
    WDT_HandleTypeDef wdt = {
        .Instance = hwp_iwdt,
        .Init = {.Reload = 100, .Reload2 = 100},
    };
    HAL_WDT_Init(&wdt);
    __HAL_WDT_INT(&wdt, 0);     // 复位模式
    __HAL_WDT_PROTECT(&wdt, 0); // 释放写保护
    __HAL_WDT_START(&wdt);      // 启动，超时后立即复位
}