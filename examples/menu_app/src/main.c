#include "rtthread.h"
#include "rtconfig.h"
#include "bf0_hal.h"
#include "drv_io.h"
#include "stdio.h"
#include "string.h"
#include "ulog.h"

#include "ui.h"
#include "config.h"
#include "rtc_time.h"
#include "chip_work_mode.h"

#ifdef USING_BUTTON_LIB
#include "button.h"
#endif

static rt_device_t s_adc_dev;
static rt_adc_cmd_read_arg_t read_arg;
static rt_mq_t esp32c6_msg_queue;

#ifdef PKG_USING_SGM41562B
#include "sgm41562b.h"
static sgm41562b_handle_t charger;
#endif

#if defined(PKG_USING_BHI260AP)
#include "bhi260ap.h"

#define BHI260AP_SAMPLE_RATE 100

lv_obj_t *acc_label_data, *gyro_label_data, *step_label_data;
struct bhy2_data_xyz_float accel_data = {0};
struct bhy2_data_xyz_float gyro_data = {0};
uint32_t step_counter;
#elif defined(PKG_USING_ICM20948)
#include "icm20948_driver.h"
lv_obj_t *acc_label_data, *gyro_label_data, *mag_label_data, *temp_label_data;

ImuReal accel_data;
ImuReal gyro_data;
ImuReal mag_data;
float temp_data;
#endif

init_error_t init_error = 0;
void key_button_init(void);
void key_borad_device_ldo_enable(bool enable);
void low_battery_countdown_alert(void *countdown);
void low_battery_countdown_dismiss(void *unused);
static void at_callback(int resp_id, const char *response, int result);
static void low_battery_monitor_entry(void *param);

/**
 * @brief  Main program
 * @param  None
 * @retval 0 if success, otherwise failure number
 */
int main(void)
{
    rt_thread_mdelay(200);

    if (PM_HIBERNATE_BOOT == SystemPowerOnModeGet())
        rt_kprintf("boot from hibernate!!!\n");

    enable_ldo(true);
#ifdef BSP_USING_PM
    rt_pm_request(PM_SLEEP_MODE_IDLE);
#endif /* BSP_USING_PM */

    device_switch_t *device_switch = get_device_switch();
#ifdef PKG_USING_SGM41562B
    charger = sgm41562b_init(SGM41562B_I2C_BUS_NAME, SGM41562B_IRQ_PIN);
    if (charger == RT_NULL)
    {
        rt_kprintf("Charger init failed\n");
        init_error |= INIT_CHARGE_ERROR;
    }
#endif

#ifdef LCD_USING_TFT_CO5300_T_SF32
    ui_init();
    rt_sem_take(start_ui_sem, RT_WAITING_FOREVER);
#endif

#ifdef USING_BUTTON_LIB
    key_button_init();
#endif

#ifdef AUDIO
    if (audio_manager_init() != RT_EOK)
    {
        init_error |= INIT_AUDPRC_ERROR;
    }
#endif

#if defined(RT_USING_SPI_MSD)
    if (sdcard_init() != RT_EOK)
    {
        init_error |= INIT_SD_ERROR;
    }
#endif

#ifdef BLUETOOTH
    if (sf32_bluetooth_init() != RT_EOK)
    {
        init_error |= INIT_BLUETOOTH_ERROR;
    }
    else
    {
        device_switch->bluetooth_enable = true;
    }
#endif

#if defined(PKG_USING_LORA_RADIO_DRIVER)
    if (lora_app_init() != RT_EOK)
    {
        init_error |= INIT_LORA_SX1262_ERROR;
    }

    if (!(init_error & INIT_LORA_SX1262_ERROR))
    {
        radio_set_freq(868000000);
        radio_set_outpower(22);
        radio_set_lora_bandwidth(LORA_BANDWIDTH);
        radio_set_lora_sf(LORA_SPREADING_FACTOR_5);
        radio_set_lora_cr(LORA_CODINGRATE_4_5);
        radio_set_lora_preamble(12);
        radio_set_lora_crc(false);
        radio_set_lora_iq(false);
        radio_set_lora_sync_word(false);
        radio_set_rx_boost(true);
    }

#endif

#if defined(PKG_USING_BHI260AP)
    if (rt_bhi260ap_init() != RT_EOK)
    {
        LOG_E("Failed to initialize BHI260AP");
        init_error |= INIT_IMU_BHI260AP_ERROR;
    }
    if (!bhi260ap_configure(BHY2_SENSOR_ID_STC, BHI260AP_SAMPLE_RATE, 0))
    {
        LOG_E("Failed to configure BHY2_SENSOR_ID_STC BHI260AP");
        init_error |= INIT_IMU_BHI260AP_ERROR;
    }
    if (!bhi260ap_configure(BHY2_SENSOR_ID_GAMERV, BHI260AP_SAMPLE_RATE, 0))
    {
        LOG_E("Failed to configure BHY2_SENSOR_ID_GAMERV BHI260AP");
        init_error |= INIT_IMU_BHI260AP_ERROR;
    }
#endif

#if defined(PKG_USING_PKG_KEY_BOARD)
    if (xl9555_init() != RT_EOK)
    {
        init_error |= INIT_IO_XL9555_ERROR;
    }
    else
    {
        key_borad_device_ldo_enable(true);
    }

    if (aw21009_app_init() != RT_EOK)
    {
        init_error |= INIT_LED_AW21009_ERROR;
        device_switch->keyboard_led_enable = false;
    }
    else
        device_switch->keyboard_led_enable = true;

    if (key_board_tca8418_init() != RT_EOK)
    {
        init_error |= INIT_KEY_BOARD_ERROR;
    }

    if (bme280_init() != RT_EOK)
    {
        init_error |= INIT_SENSOR_BME280_ERROR;
    }

    if (aw86224_init() != RT_EOK)
    {
        init_error |= INIT_MOTOR_AW86224_ERROR;
    }
    aw86224_play_ram_short(1, RT_TRUE);

    if (uart_mux_init(UART_BUS_NAME) == RT_EOK)
    {
        if (l76k_init(UART_BUS_NAME, 115200) != RT_EOK)
        {
            init_error |= INIT_GPS_L76K_ERROR;
        }

        at_async_register_callback(at_callback);
        if (at_async_init(UART_BUS_NAME, 115200) != RT_EOK)
        {
            init_error |= INIT_ESP32C6_ERROR;
        }
    }

    if (ir_nec_init() != 0)
    {
        rt_kprintf("IR init failed\n");
    }

    if (!(init_error & INIT_ESP32C6_ERROR))
    {
        rt_thread_mdelay(500);
        uart_mux_switch_to(UART_MUX_DEVICE_ESP32C6, 115200);
        esp32_at_test(AT_RESP_ID_TEST);
        esp32_at_cwinit(AT_RESP_ID_CWINIT);
        esp32_at_cwmode(AT_RESP_ID_CWMODE, 1);
        esp32_at_cwreconncfg(AT_RESP_ID_CWRECONNCFG, 0, 0); // disable auto reconnect
        esp32_at_cwjap(AT_RESP_ID_CWJAP, WIFI_SSID, WIFI_PASSWORD);
    }
#endif
    log_d("init error:%d", init_error);

    rt_thread_mdelay(100);
    rt_mb_send(get_lvgl_mb(), UI_UPDATE_INIT_ERROR);

    rt_thread_mdelay(1000);
    rt_thread_t low_bat_tid = rt_thread_create("low_bat",
                                               low_battery_monitor_entry,
                                               RT_NULL, 1024,
                                               RT_THREAD_PRIORITY_MIDDLE, 10);
    if (low_bat_tid != RT_NULL)
        rt_thread_startup(low_bat_tid);

#ifdef BSP_USING_PM
    rt_kprintf("start deepsleep mode\n");
    rt_pm_release(PM_SLEEP_MODE_IDLE);
#endif /* BSP_USING_PM */

    /**  debug
    rt_uint32_t total, used, max_used;
    while (1)
    {
        rt_thread_mdelay(1000);
        rt_memory_info(&total, &used, &max_used);
        log_d("total memory: %d, used memory: %d, max used memory: %d", total, used, max_used);
    }
    **/
}

#ifdef USING_BUTTON_LIB
#define BUTTON_PIN_33 33
#define BUTTON_PIN_34 34
#define BUTTON_PIN_35 35

void btton_handler(int32_t pin, button_action_t button_action)
{
    rt_device_t lcd_device;
    static uint8_t set_brightness = 70;
    device_switch_t *device_switch = get_device_switch();

    switch (button_action)
    {
    case BUTTON_CLICKED:
        log_d("button_handler pin:%d, action:%d\n", pin, button_action);
        if (pin == BUTTON_PIN_33)
        {
            lcd_device = rt_device_find("lcd");
            device_switch->lcd_backlight_enable = !device_switch->lcd_backlight_enable;
            if (!device_switch->lcd_backlight_enable)
            {
                rt_device_control(lcd_device, RTGRAPHIC_CTRL_SET_BRIGHTNESS, &set_brightness);
#if defined(PKG_USING_PKG_KEY_BOARD)
                aw21009_set_all_brightness(4095);
                device_switch->keyboard_led_enable = true;
#endif
            }
            else
            {
                rt_device_control(lcd_device, RTGRAPHIC_CTRL_GET_BRIGHTNESS, &set_brightness);
                uint8_t bri = 0;
                rt_device_control(lcd_device, RTGRAPHIC_CTRL_SET_BRIGHTNESS, &bri);
#if defined(PKG_USING_PKG_KEY_BOARD)
                aw21009_set_all_brightness(0);
                device_switch->keyboard_led_enable = false;
#endif
            }
            break;
        }
        else if (pin == BUTTON_PIN_34)
        {
            audio_volume_up();
            lv_async_call(volume_progress_bar, (void *)audio_get_volume());
        }
        else if (pin == BUTTON_PIN_35)
        {
            audio_volume_down();
            lv_async_call(volume_progress_bar, (void *)audio_get_volume());
        }
        break;
    case BUTTON_LONG_PRESSED:
        log_d("button_long pin:%d, action:%d\n", pin, button_action);
        break;

    default:
        break;
    }
}

void key_button_init(void)
{
    button_cfg_t btn33_cfg = {
        .pin = BUTTON_PIN_33,
        .mode = PIN_MODE_INPUT_PULLUP,
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
        .mode = PIN_MODE_INPUT_PULLUP,
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
#endif

#ifdef PKG_USING_PKG_KEY_BOARD
void key_borad_device_ldo_enable(bool enable)
{
    xl9555_pin_mode(XL9555_WIFI_EN_PIN, XL9555_PIN_OUTPUT);
    xl9555_digital_write(XL9555_WIFI_EN_PIN, enable);
    rt_thread_mdelay(20);

    xl9555_pin_mode(XL9555_GPS_EN_PIN, XL9555_PIN_OUTPUT);
    xl9555_digital_write(XL9555_GPS_EN_PIN, enable);
    rt_thread_mdelay(20);

    xl9555_pin_mode(XL9555_KEY_LED_EN_PIN, XL9555_PIN_OUTPUT);
    xl9555_digital_write(XL9555_KEY_LED_EN_PIN, enable);
    rt_thread_mdelay(20);
}

static void at_callback(int resp_id, const char *response, int result)
{
    device_switch_t *device_switch = get_device_switch();
    rt_kprintf("resp_id:%d\n", resp_id);
    switch (resp_id)
    {
    case AT_RESP_ID_TEST:
        rt_kprintf("%s\n", response);
        break;
    case AT_RESP_ID_RST:
        rt_kprintf("%s\n", response);
        break;
    case AT_RESP_ID_GMR:
        rt_kprintf("%s\n", response);
        break;
    case AT_RESP_ID_CWSTATE:
        rt_kprintf("%s\n", response);
        break;
    case AT_RESP_ID_CWJAP:
        if (result == AT_RESULT_OK)
        {
            rt_kprintf("WiFi connected!\n");
            device_switch->wifi_enable = true;
            esp32_at_sntp_cfg(AT_RESP_ID_SNTPCFG, true, 8, "cn.ntp.org.cn");
            lv_async_call(wifi_update_wifi_status, (void *)device_switch->wifi_enable);
        }
        break;
    case AT_RESP_ID_CWQAP:
        if (result == AT_RESULT_OK)
        {
            rt_kprintf("CWQAP config %s\n", response);
        }
        break;
    case AT_RESP_ID_CIFSR:
        rt_kprintf("%s\n", response);
        break;
    case AT_RESP_ID_SNTPCFG:
        if (result == AT_RESULT_OK)
        {
            rt_kprintf("SNTP config %s\n", response);
        }
        break;
    case AT_RESP_ID_SNTPTIME_Q:
        if (result == AT_RESULT_OK && response != NULL)
        {
            // rt_kprintf("SNTP time %s\n", response);
            struct tm tm = {0};
            char month_str[4] = {0};
            int year, mon, day, hour, min, sec;

            // +CIPSNTPTIME:Fri Apr 10 15:20:45 2025
            if (sscanf(response, "%*[^:]:%3s %3s %d %d:%d:%d %d",
                       month_str, month_str, &day, &hour, &min, &sec, &year) >= 6)
            {
                // 月份字符串转数字
                static const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                               "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
                for (mon = 0; mon < 12; mon++)
                {
                    if (strncmp(month_str, months[mon], 3) == 0)
                        break;
                }
                if (mon < 12)
                {
                    date_time_t dt = {
                        .year = year,
                        .month = mon + 1,
                        .day = day,
                        .hour = hour,
                        .minute = min,
                        .second = sec,
                    };
                    set_date_time(dt);
                }
            }
        }
        break;
    }
    if (resp_id == -1) /* 主动上报 */
    {
        rt_kprintf("URC: %s\n", response);
        if (strstr(response, "WIFI CONNECTED"))
        {
            rt_kprintf("WiFi connected!\n");
            device_switch->wifi_enable = true;
        }
        else if (strstr(response, "WIFI DISCONNECTED"))
        {
            rt_kprintf("WiFi disconnected!\n");
            device_switch->wifi_enable = false;
        }
        else if (strstr(response, "TIME_UPDATED"))
        {
            rt_kprintf("Time updated!\n");
            esp32_at_sntp_time_q(AT_RESP_ID_SNTPTIME_Q);
        }
    }

    if (response != RT_NULL)
    {
        char *resp_copy = rt_malloc(strlen(response) + 1);
        if (resp_copy != RT_NULL)
        {
            strncpy(resp_copy, response, strlen(response) + 1);
            lv_async_call(wifi_update_resp_cb, (void *)resp_copy);
        }
    }
}
#endif

int32_t get_voltage(void)
{
    rt_err_t ret;

    int32_t data;
    /* find device */
    s_adc_dev = rt_device_find("bat1");
    if (s_adc_dev != RT_NULL)
    {
        // /* set channel 0*/
        read_arg.channel = 7;
        ret = rt_adc_enable((rt_adc_device_t)s_adc_dev, read_arg.channel);
        rt_uint32_t value = rt_adc_read((rt_adc_device_t)s_adc_dev, 7);
        data = (int32_t)value * 0.1;
        // LOG_I("rt_adc_read:%d,value:%.2f mV", read_arg.channel, value * 0.1); /* (0.1mV), 20700 is 2070mV or 2.070V */
        /* disable adc */
        rt_adc_disable((rt_adc_device_t)s_adc_dev, read_arg.channel);
    }
    else
    {
        log_e("adc device not found");
    }

    return data;
}

uint8_t get_charge_percent(void)
{
    int32_t voltage = get_voltage();

    uint8_t percent = 0;

    if (voltage >= 4200)
    {
        percent = 100;
    }
    else if (voltage >= 4000)
    {
        percent = 80 + (voltage - 4000) * 10 / 25;
    }
    else if (voltage >= 3900)
    {
        percent = 58 + (voltage - 3900) * 22 / 100;
    }
    else if (voltage >= 3700)
    {
        percent = 18 + (voltage - 3700) * 40 / 200;
    }
    else if (voltage >= 3600)
    {
        percent = 6 + (voltage - 3600) * 12 / 100;
    }
    else if (voltage >= 3500)
    {
        percent = (voltage - 3500) * 6 / 100;
    }

    return percent > 100 ? 100 : percent;
}

#define LOW_BATTERY_THRESHOLD 3550
#define LOW_BATTERY_DEBOUNCE_CNT 10
#define SHUTDOWN_COUNTDOWN_SEC 5

static void low_battery_monitor_entry(void *param)
{
    int low_cnt = 0;
    int countdown;
    int cancelled;
    int need_recovery = 0;

    while (1)
    {
        while (1)
        {
            rt_thread_mdelay(5000);

            if (get_voltage() < LOW_BATTERY_THRESHOLD)
            {
                if (!need_recovery)
                    low_cnt++;
                if (low_cnt >= LOW_BATTERY_DEBOUNCE_CNT)
                    break;
            }
            else
            {
                low_cnt = 0;
                need_recovery = 0;
            }
        }

        log_d("low battery, starting shutdown countdown");

        cancelled = 0;
        lv_async_call(low_battery_countdown_reset, NULL);
        for (countdown = SHUTDOWN_COUNTDOWN_SEC; countdown > 0; countdown--)
        {
            rt_mb_send(get_lvgl_mb(), UI_UPDATE_LOW_BATTERY_COUNTDOWN);
            lv_async_call(low_battery_countdown_alert, (void *)(rt_uint32_t)countdown);

#ifdef PKG_USING_SGM41562B
            int st = sgm41562b_get_charge_status(charger);
            if (st == CHG_STAT_PRE_CHARGE || st == CHG_STAT_FAST_CHARGE)
            {
                log_d("charger plugged in, cancel shutdown");
                cancelled = 1;
                break;
            }
#endif
            rt_thread_mdelay(1000);
        }

        if (!cancelled)
        {
            into_hibernate();
        }

        lv_async_call(low_battery_countdown_dismiss, NULL);
        low_cnt = 0;
        need_recovery = 0;
    }
}
