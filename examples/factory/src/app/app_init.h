#ifndef APP_INIT_H
#define APP_INIT_H

#include <rtthread.h>

enum
{
    APP_INIT_ERROR_NONE      = 0,
    APP_INIT_ERROR_BATTERY   = (1UL << 0),
    APP_INIT_ERROR_STORAGE   = (1UL << 1),
    APP_INIT_ERROR_UI        = (1UL << 2),
    APP_INIT_ERROR_BUTTONS   = (1UL << 3),
    APP_INIT_ERROR_XL9555    = (1UL << 4),
    APP_INIT_ERROR_TCA8418   = (1UL << 5),
    APP_INIT_ERROR_BME280    = (1UL << 6),
    APP_INIT_ERROR_BHI260AP  = (1UL << 7),
    APP_INIT_ERROR_AUDPRC    = (1UL << 8),
    APP_INIT_ERROR_AUDCODEC  = (1UL << 9),
    APP_INIT_ERROR_AUDIO_MGR = (1UL << 10),
    APP_INIT_ERROR_UART2     = (1UL << 11),
    APP_INIT_ERROR_UART_MUX  = (1UL << 12),
    APP_INIT_ERROR_L76K      = (1UL << 13),
    APP_INIT_ERROR_ESP32C6   = (1UL << 14),
    APP_INIT_ERROR_JOYPAD_KEYS = (1UL << 15),
    APP_INIT_ERROR_ADS1115     = (1UL << 16),
    APP_INIT_ERROR_WS2812      = (1UL << 17),
    APP_INIT_ERROR_SX1262      = (1UL << 18),
    APP_INIT_ERROR_AW21009     = (1UL << 19),
    APP_INIT_ERROR_AW86224     = (1UL << 20),
    APP_INIT_ERROR_INFRARED    = (1UL << 21),
};

rt_err_t app_init(void);
rt_uint32_t app_get_init_errors(void);
void app_update_status_bar(void);

#endif
