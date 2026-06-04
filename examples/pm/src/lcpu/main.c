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
#include <stdlib.h>

#ifdef SF32LB55X
    #include "shell.h"
#endif /* SF32LB55X */


#define LOG_TAG "pm"
#include "log.h"


#ifndef LXT_LP_CYCLE
    #define LXT_LP_CYCLE 200
#endif
static rt_timer_t rc_10_time_handle;

static uint32_t g_rc_update_seconds = 15;

int cmd_lrc(int argc, char **argv)
{
    if (argc > 1)
    {
        g_rc_update_seconds = atoi(argv[1]);
    }
    if (rc_10_time_handle)
    {
        rt_kprintf("rc10 update every %ds\n", g_rc_update_seconds);
        if (argc > 1)
        {
            rt_tick_t ticks = g_rc_update_seconds * 1000;
            rt_timer_control(rc_10_time_handle, RT_TIMER_CTRL_SET_TIME, &ticks);
        }
    }
    return 0;
}
FINSH_FUNCTION_EXPORT_ALIAS(cmd_lrc, __cmd_lrc, set RC update internal seconds);

void rc10k_timeout_handler(void *parameter)
{
    HAL_RC_CAL_update_reference_cycle_on_48M(LXT_LP_CYCLE);
}

static void wake_key_handle(void)
{
    rt_kprintf("Lcpu wake_key_handle!!!\n");
}

static void lcpu_wakeup(void)
{
    uint8_t pin;
#if defined(SF32LB55X)
#define WAKE_KEY (96+48) //PB48 #WKUP_PIN5

    HAL_LPAON_EnableWakeupSrc(LPAON_WAKEUP_SRC_PIN5, AON_PIN_MODE_NEG_EDGE);

    rt_pin_mode(WAKE_KEY, PIN_MODE_INPUT);

    rt_pin_attach_irq(WAKE_KEY, PIN_IRQ_MODE_FALLING, (void *) wake_key_handle,
                      (void *)(rt_uint32_t) WAKE_KEY);
    rt_pin_irq_enable(WAKE_KEY, 1);

#elif defined(SF32LB56X)

#else

#endif

}


int main(void)
{
    rt_pm_request(PM_SLEEP_MODE_IDLE);
    rt_kprintf("lcpu main!!!\n");
    rt_thread_delay(3000);
    lcpu_wakeup();
    rt_pm_release(PM_SLEEP_MODE_IDLE);

    if (HAL_PMU_LXT_DISABLED())
    {
        lpsys_clk_setting_t clk_setting;

        drv_get_lpsys_clk(&clk_setting);
        /* hclk must be 48MHz if RC10k is used */
        RT_ASSERT(48000000 == clk_setting.hclk);
        HAL_RC_CAL_update_reference_cycle_on_48M(LXT_LP_CYCLE);

        rc_10_time_handle  = rt_timer_create("rc10k", rc10k_timeout_handler,  NULL,
                                             rt_tick_from_millisecond(g_rc_update_seconds * 1000), RT_TIMER_FLAG_SOFT_TIMER | RT_TIMER_FLAG_PERIODIC);
        RT_ASSERT(rc_10_time_handle);
        rt_timer_start(rc_10_time_handle);
    }

    //HAL_RCC_LCPU_SetDiv(2, );
    while (1)
    {
        rt_thread_mdelay(20000);
        rt_kprintf("lcpu timer wakeup!!!\n");
    }
    return RT_EOK;
}



/************************ (C) COPYRIGHT Sifli Technology *******END OF FILE****/

