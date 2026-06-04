/**
 ******************************************************************************
 * @file   main.c
 * @author Sifli software development team
 ******************************************************************************
 */
/**
 * @attention
 * Copyright (c) 2021 - 2021,  Sifli Technology
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form, except as embedded into a Sifli integrated circuit
 *    in a product or a software update for such product, must reproduce the above
 *    copyright notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of Sifli nor the names of its contributors may be used to endorse
 *    or promote products derived from this software without specific prior written permission.
 *
 * 4. This software, with or without modification, must only be used with a
 *    Sifli integrated circuit.
 *
 * 5. Any software provided in binary form under this license must not be reverse
 *    engineered, decompiled, modified and/or disassembled.
 *
 * THIS SOFTWARE IS PROVIDED BY SIFLI TECHNOLOGY "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL SIFLI TECHNOLOGY OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <string.h>
#include "data_service_subscriber.h"
#ifdef BLUETOOTH
#include "ancs_service.h"
#endif /* BLUETOOTH */
#include "flashdb.h"
#include "drv_flash.h"
#ifdef BLUETOOTH
#include "bf0_ble_gap.h"
#include "bf0_sibles.h"
#include "bf0_sibles_advertising.h"
#endif /* BLUETOOTH */
#include "button.h"

#define LOG_TAG "pm"
#include "log.h"

#define WAKEUP_PIN 34
#define BUTTON_PIN_33 33
#define BUTTON_PIN_35 35
#define LOD_PIN 41

void entry_hibernate(void);

static void gpio_wakeup_handler(void *args)
{
    rt_kprintf("gpio_wakeup_handler!\n");
}

static void app_wakeup(void)
{
    uint8_t pin;
    int8_t wakeup_pin;
    HAL_PIN_Set(PAD_PA41, GPIO_A41, PIN_PULLUP, 1); // set PA41 to GPIO funtion
    rt_pin_mode(LOD_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(LOD_PIN, PIN_HIGH);

    HAL_PIN_Set(PAD_PA34, GPIO_A34, PIN_PULLDOWN, 1); // set PA34 to GPIO funtion

    HAL_HPAON_EnableWakeupSrc(HPAON_WAKEUP_SRC_PIN10, AON_PIN_MODE_POS_EDGE); // Enable #WKUP_PIN10 (PA34)

    rt_pin_mode(WAKEUP_PIN, PIN_MODE_INPUT);

    rt_pin_attach_irq(WAKEUP_PIN, PIN_IRQ_MODE_RISING, (void *)gpio_wakeup_handler,
                      (void *)(rt_uint32_t)WAKEUP_PIN); // PA34 GPIO interrupt
    rt_pin_irq_enable(WAKEUP_PIN, 1);
}

void rc10k_timeout_handler(void *parameter)
{
    HAL_RC_CAL_update_reference_cycle_on_48M(LXT_LP_CYCLE);
}

#ifdef BSP_USING_PM
// #if defined(SF32LB52X) || defined(SF32LB58X)
// HAL_RAM_RET_CODE_SECT(sleep, int sleep(int argc, char **argv))
// #else
static int sleep(int argc, char *argv[])
// #endif
{
    char i;
    if (argc > 1)
    {
        if (strcmp("standby", argv[1]) == 0)
        {
            log_d("sleep on\r\n");
            rt_pm_release(PM_SLEEP_MODE_IDLE);
        }
        else if (strcmp("off", argv[1]) == 0)
        {
            log_d("sleep off\r\n");
            rt_pm_request(PM_SLEEP_MODE_IDLE);
        }
        else if (strcmp("down", argv[1]) == 0)
        {
            log_d("SF32LB52X entry_hibernate\n");
            // HAL_PMU_SelectWakeupPin(0, HAL_HPAON_QueryWakeupPin(hwp_gpio1, BSP_KEY1_PIN)); // select PA34 to wake_pin0
            // HAL_PMU_EnablePinWakeup(0, AON_PIN_MODE_HIGH);                                 // enable wake_pin0
            // hwp_pmuc->WKUP_CNT = 0x50005;                                                  // 31-16bit:config PIN1 wake CNT , 15-0bit:PIN0 wake CNT
            // log_d("SF32LB52X CR:0x%x,WER:0x%x\n", hwp_pmuc->CR, hwp_pmuc->WER);
            // rt_hw_interrupt_disable();
            // HAL_PMU_ConfigPeriLdo(PMUC_PERI_LDO_EN_VDD33_LDO3_Pos, false, false);
            // HAL_PMU_ConfigPeriLdo(PMUC_PERI_LDO_EN_VDD33_LDO2_Pos, false, false);
            // HAL_PMU_ConfigPeriLdo(PMU_PERI_LDO_1V8, false, false);
            // HAL_PMU_EnterHibernate();
            // while (1)
            // ;
            entry_hibernate();
        }
        else
        {
            log_d("sleep cmd err\r\n");
        }
    }
    else
    {
        log_d("please input sleep standby|off|down\r\n");
    }
    return 0;
}
MSH_CMD_EXPORT(sleep, sleep command); /* 导出到 msh 命令列表中 */
#endif                                /* BSP_USING_PM */

void entry_hibernate(void)
{
    rt_kprintf("entry hibernate!!!\n");

    HAL_PMU_SelectWakeupPin(0, HAL_HPAON_QueryWakeupPin(hwp_gpio1, WAKEUP_PIN)); // select PA34 to wake_pin0
    HAL_PMU_EnablePinWakeup(0, AON_PIN_MODE_HIGH);                               // enable wake_pin0
    hwp_pmuc->WKUP_CNT = 0x50005;                                                // 31-16bit:config PIN1 wake CNT , 15-0bit:PIN0 wake CNT
    rt_kprintf("SF32LB52X CR:0x%x,WER:0x%x\n", hwp_pmuc->CR, hwp_pmuc->WER);

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

void btton_handler(int32_t pin, button_action_t button_action)
{
    log_d("button_handler pin:%d, action:%d\n", pin, button_action);
}

int main(void)
{
    //*(volatile uint32_t *)0x4004f000 = 1;
    MODIFY_REG(hwp_pmuc->LXT_CR, PMUC_LXT_CR_BM_Msk, MAKE_REG_VAL(0xF, PMUC_LXT_CR_BM_Msk, PMUC_LXT_CR_BM_Pos)); // Increase current
    app_wakeup();

    button_cfg_t btn33_cfg = {
        .pin = BUTTON_PIN_33,
        .mode = PIN_MODE_INPUT_PULLUP,
        .active_state = BUTTON_ACTIVE_LOW,
        .button_handler = btton_handler,
    };

    button_cfg_t btn35_cfg = {
        .pin = BUTTON_PIN_35,
        .mode = PIN_MODE_INPUT_PULLUP,
        .active_state = BUTTON_ACTIVE_LOW,
        .button_handler = btton_handler,
    };

    int32_t button_id = button_init(&btn33_cfg);
    if(button_id < 0)
    {
        log_e("button_init btn33 failed!\n");
    }
    else
    {
        button_enable(button_id);
    }

    button_id = button_init(&btn35_cfg);
    if(button_id < 0)
    {
        log_e("button_init btn35 failed!\n");
    }
    else
    {
        button_enable(button_id);
    }

#ifdef BSP_USING_PM
    rt_pm_request(PM_SLEEP_MODE_IDLE);
    rt_kprintf("hcpu main!!!\n");
#endif /* BSP_USING_PM */

#ifdef BSP_USING_PM
    rt_thread_delay(2000);
    rt_pm_release(PM_SLEEP_MODE_IDLE);

    if (PM_HIBERNATE_BOOT == SystemPowerOnModeGet())
        rt_kprintf("boot from hibernate!!!\n");
#endif /* BSP_USING_PM */

    // entry_hibernate();

    while (1)
    {
        // rt_thread_mdelay(5000);
        // rt_kprintf("hcpu timer wakeup!!!\n");
        // button_update_handler();
        rt_thread_mdelay(500);
    }
    return RT_EOK;
}

/************************ (C) COPYRIGHT Sifli Technology *******END OF FILE****/
