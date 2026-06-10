#include "rtthread.h"
#include "rtconfig.h"
#include "bf0_hal.h"
#include "drv_io.h"
#include "stdio.h"
#include "string.h"
#include "ulog.h"
#include "button.h"

void btton_handler(int32_t pin, button_action_t button_action);
void key_button_init(void);

/**
 * @brief  Main program
 * @param  None
 * @retval 0 if success, otherwise failure number
 */
int main(void)
{
    rt_err_t ret = RT_EOK;
    key_button_init();
    while (1)
    {
        LOG_D("Hello LILYGO T-Display-SF32!");
        rt_thread_mdelay(1000);
    }
}

#define BUTTON_PIN_33 33
#define BUTTON_PIN_34 34
#define BUTTON_PIN_35 35

void btton_handler(int32_t pin, button_action_t button_action)
{
    switch (button_action)
    {
    case BUTTON_CLICKED:
        if (pin == BUTTON_PIN_33)
        {
            log_d("BUTTON_CLICKED pin:%d, action:%d\n", pin, button_action);
        }
        if (pin == BUTTON_PIN_34)
        {
            log_d("BUTTON_CLICKED pin:%d, action:%d\n", pin, button_action);
        }
        if (pin == BUTTON_PIN_35)
        {
            log_d("BUTTON_CLICKED pin:%d, action:%d\n", pin, button_action);
        }
        break;
    case BUTTON_LONG_PRESSED:
        log_d("BUTTON_LONG_PRESSED pin:%d, action:%d\n", pin, button_action);
        break;
    case BUTTON_RELEASED:
        log_d("BUTTON_RELEASED pin:%d, action:%d\n", pin, button_action);
        break;

    default:
        break;
    }
}

void key_button_init(void)
{
    button_cfg_t btn33_cfg = {
        .pin = BUTTON_PIN_33,
        .mode = PIN_MODE_INPUT,
        .active_state = BUTTON_ACTIVE_LOW,
        .button_handler = btton_handler,
    };

    button_cfg_t btn34_cfg = {
        .pin = BUTTON_PIN_34,
        .mode = PIN_MODE_INPUT_PULLDOWN,
        .active_state = BUTTON_ACTIVE_HIGH,
        .button_handler = btton_handler,
    };

    button_cfg_t btn35_cfg = {
        .pin = BUTTON_PIN_35,
        .mode = PIN_MODE_INPUT,
        .active_state = BUTTON_ACTIVE_LOW,
        .button_handler = btton_handler,
    };

    int32_t button_id = button_init(&btn33_cfg);
    if (button_id < 0)
    {
        log_e("button_init btn33 failed!\n");
    }
    else
    {
        button_enable(button_id);
    }

    button_id = button_init(&btn34_cfg);
    if (button_id < 0)
    {
        log_e("button_init btn34 failed!\n");
    }
    else
    {
        button_enable(button_id);
    }

    button_id = button_init(&btn35_cfg);
    if (button_id < 0)
    {
        log_e("button_init btn35 failed!\n");
    }
    else
    {
        button_enable(button_id);
    }
}