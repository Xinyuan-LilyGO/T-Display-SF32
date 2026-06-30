#include "chip_work_mode.h"
#include <rtdevice.h>
#include "rtthread.h"
#include "rtconfig.h"
#include <board.h>
#include <string.h>
#include "log.h"
#include "bf0_hal_wdt.h"
#include "bf0_hal_aon.h"


#define WAKEUP_PIN 34
#define LOD_PIN 41

static void gpio_wakeup_handler(void *args)
{
    rt_kprintf("pin34 gpio_wakeup_handler!\n");
}

void enable_ldo(bool enable)
{
    HAL_PIN_Set(PAD_PA41, GPIO_A41, PIN_PULLUP, 1); // set PA41 to GPIO funtion
    rt_pin_mode(LOD_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(LOD_PIN, enable);
}

void set_wakeup_src(void)
{
    uint8_t pin;
    int8_t wakeup_pin;

    HAL_PIN_Set(PAD_PA34, GPIO_A34, PIN_PULLDOWN, 1);                         // set PA34 to GPIO funtion
    HAL_HPAON_EnableWakeupSrc(HPAON_WAKEUP_SRC_PIN10, AON_PIN_MODE_POS_EDGE); // Enable #WKUP_PIN10 (PA34)
    rt_pin_mode(WAKEUP_PIN, PIN_MODE_INPUT);

    rt_pin_attach_irq(WAKEUP_PIN, PIN_IRQ_MODE_RISING, (void *)gpio_wakeup_handler,
                      (void *)(rt_uint32_t)WAKEUP_PIN); // PA34 GPIO interrupt
    rt_pin_irq_enable(WAKEUP_PIN, 1);
}

/* ============ Sleep (Light Sleep) ============
 * CPU stop, SRAM/IO retained, any IRQ wakes, <1us latency
 */
void into_sleep(void)
{
    log_d("SF32LB52X entry_sleep\n");

    /* Enable PIN10 (PA34) low level as wakeup source */
    HAL_HPAON_EnableWakeupSrc(HPAON_WAKEUP_SRC_PIN10, AON_PIN_MODE_LOW);

    /* Set PMR_MODE = 1 (Light Sleep) */
    HAL_HPAON_EnterLightSleep(0);

    /* WFI to actually enter sleep */
    __WFI();
}

/* ============ Deepsleep ============
 * CPU stop, SRAM retained, IO retained (except PA24~PA27),
 * Wake: RTC/PIN/IO/LPTIM1/LPSYS/MAILBOX2, ~250us latency
 *
 * Manual sequence (Section 4.2.3):
 *   1. Prepare wakeup sources
 *   2. Disable interrupts (PRIMASK=1)
 *   3. Stop clocks (clk_hpsys, clk_hrc48, clk_dll)
 *   4. HPSYS_AON Deactivate (clear HP_ACTIVE, disable PAD/VHP)
 *   5. Set PMR_MODE = 2 (Deep Sleep)
 *   6. WFI
 */
void into_deepsleep(void)
{
    register rt_base_t level;
    log_d("SF32LB52X entry_deepssleep\n");
    /*
    Enable PIN0 low level wakeup
    HPAON_WAKEUP_SRC_PIN0~HPAON_WAKEUP_SRC_PIN20 –> for sf32lb52x, PA24~PA44
     */
    /* Step 1: Enable wakeup source - PIN10 (PA34) low level */
    HAL_HPAON_EnableWakeupSrc(HPAON_WAKEUP_SRC_PIN10, AON_PIN_MODE_LOW);

    /* Step 2: Disable interrupts */
    level = rt_hw_interrupt_disable();

    /* Step 3: Stop high-speed clocks before entering deepsleep */
    HAL_HPAON_DisableRC();   // Stop HRC48
    HAL_HPAON_DisableXT48(); // Stop HXT48 if enabled

    /* Step 4: Deactivate HPSYS - clear HP_ACTIVE, disable PAD and VHP */
    HAL_HPAON_Deactivate();

    /* Step 5: Set PMR_MODE = 2 (Deep Sleep) */
    HAL_HPAON_EnterDeepSleep(0);

    /* Step 6: Wait For Interrupt to actually enter deepsleep */
    __WFI();

    /* === Wake-up recovery (CPU resumes here after wakeup) === */
    /* Re-enable clocks */
    HAL_HPAON_EnableRC();
    /* Re-enable interrupts */
    rt_hw_interrupt_enable(level);
}

/* ============ Standby ============
 * CPU reset, SRAM only 384KB retention (0x20020000~0x2007FFFF),
 * IO retained (except PA24~PA27),
 * Wake: RTC/PIN/LPTIM1/LPSYS/MAILBOX2, ~1ms latency
 *
 * Manual sequence (Section 4.2.4):
 *   1. Prepare wakeup sources
 *   2. Save SRAM data to retention area
 *   3. Disable interrupts
 *   4. Stop clocks (clk_hpsys, clk_hrc48)
 *   5. HPSYS_AON Deactivate
 *   6. Clear ISSR_HP_ACTIVE
 *   7. Set ANACR = 3 (PA_ISO + VHP_ISO)
 *   8. Set PMR_MODE = 3 with FORCE_SLEEP (0x80000003)
 *   9. WFI
 */
void into_standby(void)
{
    log_d("SF32LB52X entry_standby\n");
    /* Enable PIN0 low level wakeup
    HPAON_WAKEUP_SRC_PIN0~HPAON_WAKEUP_SRC_PIN20 –> for sf32lb52x, PA24~PA44
     */
      /* Step 1: Enable wakeup source - PIN10 (PA34) low level */
      HAL_HPAON_EnableWakeupSrc(HPAON_WAKEUP_SRC_PIN10, AON_PIN_MODE_LOW);

      /* Step 2: Save SRAM to retention area (app-specific) */
      // TODO: save critical data to 0x20020000~0x2007FFFF

      /* Step 3: Disable interrupts */
      rt_hw_interrupt_disable();

      /* Step 4: Stop clocks */
      HAL_HPAON_DisableRC();
      HAL_HPAON_DisableXT48();

      /* Step 5-6: Deactivate HPSYS (clears HP_ACTIVE, disables PAD/VHP) */
      HAL_HPAON_Deactivate();

      /* Step 7: Set ANACR = 3 (PA_ISO + VHP_ISO)
       * The manual explicitly says ANACR should be set to 3 for standby */
      hwp_hpsys_aon->ANACR = (HPSYS_AON_ANACR_PA_ISO | HPSYS_AON_ANACR_VHP_ISO);

      /* Step 8: Set PMR_MODE = 3 (Standby) with FORCE_SLEEP bit
       * Manual says PMR_MODE should be 0x80000003 for standby */
      HAL_HPAON_SET_POWER_MODE(AON_PMR_STANDBY);
      hwp_hpsys_aon->PMR |= HPSYS_AON_PMR_FORCE_SLEEP;  // Set bit31 to force

      /* Step 9: WFI to enter standby */
      __WFI();

      /* CPU does NOT reach here - standby causes CPU reset on wakeup.
       * Recovery is through ROM boot sequence, which checks PMR and restores context. */
}

/* ============ Hibernate ============
 * Full shutdown: CPU/HPSYS/LPSYS/SRAM all off, only PMU/RTC/IWDT alive
 * Wake: RTC, PIN, >2ms latency
 * CPU restarts from ROM on wakeup
 */
void into_hibernate(void)
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