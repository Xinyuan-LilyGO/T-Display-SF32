#include "rtthread.h"
#include "rtconfig.h"
#include "bf0_hal.h"
#include "drv_io.h"
#include "stdio.h"
#include "string.h"

#include "littlevgl2rtt.h"
#include "lvgl.h"
#include "ulog.h"
#include "ui.h"
#include "drv_spi.h"
#include "drv_i2c.h"
#include "sgm41562b.h"

#if defined(PKG_USING_BHI260AP)
#include "bhi260ap.h"

#define BHI26AP_ADDR 0x28
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
float angle[3]; // pitch, roll, yaw
#endif

void HAL_MspInit(void)
{
    //__asm("B .");        /*For debugging purpose*/
    BSP_IO_Init();
}

static rt_thread_t lvgl_task = RT_NULL;
static rt_thread_t imu_task = RT_NULL;

static struct rt_i2c_bus_device *i2c_bus = NULL;

void lvgl_task_entry(void *parameter);
void imu_task_entry(void *parameter);
void imu_ui(void);

lv_timer_t *imu_data_timer;

#define IS_DISPLAY 1
/**
 * @brief  Main program
 * @param  None
 * @retval 0 if success, otherwise failure number
 */
int main(void)
{
    rt_err_t ret = RT_EOK;
    rt_thread_delay(1500);

    sgm41562b_handle_t charger;
    charger = sgm41562b_init(SGM41562B_I2C_BUS_NAME, SGM41562B_IRQ_PIN);
    if (charger == RT_NULL)
    {
        rt_kprintf("Charger init failed\n");
    }

#if defined(IS_DISPLAY)
    ret = littlevgl2rtt_init("lcd");
    if (ret != RT_EOK)
    {
        return ret;
    }

    LOG_D("T-Display-SF32!");

    imu_ui();

    lvgl_task = rt_thread_create("lvgl_task", lvgl_task_entry, RT_NULL, 8 * 1024, 25, 10);
    if (lvgl_task != RT_NULL)
    {
        rt_thread_startup(lvgl_task);
    }

#if defined(PKG_USING_BHI260AP)
    if (rt_bhi260ap_init() != RT_EOK)
    {
        LOG_E("Failed to initialize BHI260AP");
    }
    if (!bhi260ap_configure(BHY2_SENSOR_ID_ACC, BHI260AP_SAMPLE_RATE, 0))
    {
        LOG_E("Failed to configure BHI260AP");
    }
    if (!bhi260ap_configure(BHY2_SENSOR_ID_GYRO, BHI260AP_SAMPLE_RATE, 0))
    {
        LOG_E("Failed to configure BHI260AP");
    }
    if (!bhi260ap_configure(BHY2_SENSOR_ID_STC, BHI260AP_SAMPLE_RATE, 0))
    {
        LOG_E("Failed to configure BHI260AP");
    }

    imu_task = rt_thread_create("imu_task", imu_task_entry, RT_NULL, 8 * 1024, 24, 10);
    if (imu_task != RT_NULL)
    {
        rt_thread_startup(imu_task);
    }
    bhi260ap_thread_resume(); // resume the BHI260AP thread
#elif defined(PKG_USING_ICM20948)
    if (rt_icm20948_init() != RT_EOK)
    {
        LOG_E("Failed to initialize ICM20948");
    }
#endif

#else

#if defined(PKG_USING_BHI260AP)
    if (rt_bhi260ap_init() != RT_EOK)
    {
        LOG_E("Failed to initialize BHI260AP");
    }

    // for(int i = 0; i < BHY2_SENSOR_ID_MAX; i++)
    // {
    //     if(bhi260ap_is_sensor_available(i))
    //     {
    //         if(!bhi260ap_configure(i, BHI260AP_SAMPLE_RATE, 0))
    //         {
    //             LOG_E("Failed to configure BHI260AP");
    //         }
    //     }
    // }
    if (!bhi260ap_configure(BHY2_SENSOR_ID_GAMERV, BHI260AP_SAMPLE_RATE, 0))
    {
        LOG_E("Failed to configure BHI260AP");
    }
    if (!bhi260ap_configure(BHY2_SENSOR_ID_ACC, BHI260AP_SAMPLE_RATE, 0))
    {
        LOG_E("Failed to configure BHI260AP");
    }
    if (!bhi260ap_configure(BHY2_SENSOR_ID_GYRO, BHI260AP_SAMPLE_RATE, 0))
    {
        LOG_E("Failed to configure BHI260AP");
    }
    if (!bhi260ap_configure(BHY2_SENSOR_ID_STC, BHI260AP_SAMPLE_RATE, 0))
    {
        LOG_E("Failed to configure BHI260AP");
    }
#elif defined(PKG_USING_ICM20948)
    if (rt_icm20948_init() != RT_EOK)
    {
        LOG_E("Failed to initialize ICM20948");
    }

#endif
#endif

    imu_task = rt_thread_create("imu_task", imu_task_entry, RT_NULL, 4 * 1024, 26, 10);
    if (imu_task != RT_NULL)
    {
        rt_thread_startup(imu_task);
    }
}

void lvgl_task_entry(void *parameter)
{
    rt_uint32_t ms;
    rt_uint32_t time;
    while (1)
    {
        ms = lv_task_handler();
        rt_thread_mdelay(ms);
    }
}

void imu_task_entry(void *parameter)
{
    while (1)
    {
#if defined(PKG_USING_BHI260AP)
        accel_data = bhi260ap_get_acc_sensor_data(BHY2_SENSOR_ID_ACC);
        gyro_data = bhi260ap_get_gyro_sensor_data(BHY2_SENSOR_ID_GYRO);
        step_counter = bhi260ap_get_step_counter_sensor_data(BHY2_SENSOR_ID_STC);
        struct bhy2_data_orientation_float orient = bhi260ap_get_orient_sensor_data();
        struct bhy2_data_quaternion_float quaternion = bhi260ap_get_quaternion_sensor_data();

        LOG_D("Accel: %.2f, %.2f, %.2f", accel_data.x, accel_data.y, accel_data.z);
        LOG_D("Gyro: %.2f, %.2f, %.2f", gyro_data.x, gyro_data.y, gyro_data.z);
        LOG_D("Step Counter: %d", step_counter);
        log_d("orient: %f, %f, %f", orient.heading, orient.pitch, orient.roll);
        log_d("quaternion: %f, %f, %f, %f", quaternion.x, quaternion.y, quaternion.z,quaternion.w);
        rt_thread_mdelay(500);
#elif defined(PKG_USING_ICM20948)
        icm20948_real_data(&accel_data, &gyro_data);
        icm20948_get_temp_data(&temp_data);
        ak09916_real_data(&mag_data);

        IMU_GetYawPitchRoll(angle);
        LOG_D("Yaw: %.2f, Pitch: %.2f, Roll: %.2f", angle[0], angle[1], angle[2]);

        LOG_D("Accel: %.2f, %.2f, %.2f", accel_data.x, accel_data.y, accel_data.z);
        LOG_D("Gyro: %.2f, %.2f, %.2f", gyro_data.x, gyro_data.y, gyro_data.z);
        LOG_D("Mag: %.2f, %.2f, %.2f", mag_data.x, mag_data.y, mag_data.z);
        LOG_D("Temp: %.2f", temp_data);
        rt_thread_mdelay(100);
#endif
    }
}

void imu_data_update(lv_timer_t *timer)
{
#if defined(PKG_USING_BHI260AP)
    lv_label_set_text_fmt(acc_label_data, "X:%.2f  Y:%.2f  Z:%.2f", accel_data.x, accel_data.y, accel_data.z);
    lv_label_set_text_fmt(gyro_label_data, "X:%.2f  Y:%.2f  Z:%.2f", gyro_data.x, gyro_data.y, gyro_data.z);
    lv_label_set_text_fmt(step_label_data, "%d", step_counter);
#elif defined(PKG_USING_ICM20948)
    lv_label_set_text_fmt(acc_label_data, "X:%.2f  Y:%.2f  Z:%.2f", accel_data.x, accel_data.y, accel_data.z);
    lv_label_set_text_fmt(gyro_label_data, "X:%.2f  Y:%.2f  Z:%.2f", gyro_data.x, gyro_data.y, gyro_data.z);
    lv_label_set_text_fmt(mag_label_data, "X:%.2f  Y:%.2f  Z:%.2f", mag_data.x, mag_data.y, mag_data.z);
    lv_label_set_text_fmt(temp_label_data, "%.2f", temp_data);
#endif
}

void imu_ui(void)
{
    lv_obj_t *src = lv_screen_active();

    static lv_style_t style;
    lv_style_init(&style);
    lv_style_set_bg_color(&style, lv_color_black());
    lv_style_set_pad_all(&style, 0);
    lv_style_set_border_width(&style, 0);
    lv_obj_add_style(src, &style, 0);
    lv_obj_set_scrollbar_mode(src, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *label = lv_label_create(src);
    lv_label_set_text(label, "IMU");
    lv_obj_set_style_text_font(label, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x00FF00), 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t *accel_label = lv_label_create(src);
    lv_label_set_text(accel_label, "Accelerometer:");
    lv_obj_set_size(accel_label, 200, 35);
    lv_obj_set_style_text_font(accel_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(accel_label, lv_color_white(), 0);
    lv_obj_align(accel_label, LV_ALIGN_TOP_LEFT, 10, 100);
    lv_obj_set_style_text_align(accel_label, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *gyro_label = lv_label_create(src);
    lv_obj_set_size(gyro_label, 200, 35);
    lv_label_set_text(gyro_label, "Gyroscope:");
    lv_obj_set_style_text_color(gyro_label, lv_color_white(), 0);
    lv_obj_align(gyro_label, LV_ALIGN_TOP_LEFT, 10, 200);
    lv_obj_set_style_text_font(gyro_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(gyro_label, LV_TEXT_ALIGN_CENTER, 0);

    acc_label_data = lv_label_create(src);
    lv_obj_set_size(acc_label_data, 250, 40);
    lv_obj_set_style_text_color(acc_label_data, lv_color_white(), 0);
    lv_label_set_text(acc_label_data, "X:0.00  Y:-0.00  Z:-0.00");
    lv_obj_set_style_text_font(acc_label_data, &lv_font_montserrat_20, 0);
    lv_obj_align(acc_label_data, LV_ALIGN_TOP_LEFT, 210, 100);
    lv_obj_set_style_text_align(acc_label_data, LV_TEXT_ALIGN_CENTER, 0);

    gyro_label_data = lv_label_create(src);
    lv_obj_set_size(gyro_label_data, 250, 40);
    lv_obj_set_style_text_color(gyro_label_data, lv_color_white(), 0);
    lv_label_set_text(gyro_label_data, "X:0.00  Y:-0.00  Z:-0.00");
    lv_obj_set_style_text_font(gyro_label_data, &lv_font_montserrat_20, 0);
    lv_obj_align(gyro_label_data, LV_ALIGN_TOP_LEFT, 210, 200);
    lv_obj_set_style_text_align(gyro_label_data, LV_TEXT_ALIGN_CENTER, 0);

#if defined(PKG_USING_BHI260AP)
    lv_obj_t *step_label = lv_label_create(src);
    lv_obj_set_size(step_label, 200, 35);
    lv_label_set_text(step_label, "Step:");
    lv_obj_set_style_text_color(step_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(step_label, &lv_font_montserrat_24, 0);
    lv_obj_align(step_label, LV_ALIGN_TOP_LEFT, 10, 300);
    lv_obj_set_style_text_align(step_label, LV_TEXT_ALIGN_CENTER, 0);

    step_label_data = lv_label_create(src);
    lv_obj_set_size(step_label_data, 250, 40);
    lv_obj_set_style_text_color(step_label_data, lv_color_white(), 0);
    lv_label_set_text(step_label_data, "0");
    lv_obj_set_style_text_font(step_label_data, &lv_font_montserrat_20, 0);
    lv_obj_align(step_label_data, LV_ALIGN_TOP_LEFT, 210, 300);
    lv_obj_set_style_text_align(step_label_data, LV_TEXT_ALIGN_CENTER, 0);

#elif defined(PKG_USING_ICM20948)
    lv_obj_t *mag_label = lv_label_create(src);
    lv_obj_set_size(mag_label, 200, 35);
    lv_label_set_text(mag_label, "Mag:");
    lv_obj_set_style_text_color(mag_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(mag_label, &lv_font_montserrat_24, 0);
    lv_obj_align(mag_label, LV_ALIGN_TOP_LEFT, 10, 300);
    lv_obj_set_style_text_align(mag_label, LV_TEXT_ALIGN_CENTER, 0);

    mag_label_data = lv_label_create(src);
    lv_obj_set_size(mag_label_data, 250, 40);
    lv_obj_set_style_text_color(mag_label_data, lv_color_white(), 0);
    lv_label_set_text(mag_label_data, "");
    lv_obj_set_style_text_font(mag_label_data, &lv_font_montserrat_20, 0);
    lv_obj_align(mag_label_data, LV_ALIGN_TOP_LEFT, 210, 300);
    lv_obj_set_style_text_align(mag_label_data, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t *temp_label = lv_label_create(src);
    lv_obj_set_size(temp_label, 200, 35);
    lv_label_set_text(temp_label, "Temp:");
    lv_obj_set_style_text_color(temp_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(temp_label, &lv_font_montserrat_24, 0);
    lv_obj_align(temp_label, LV_ALIGN_TOP_LEFT, 10, 400);
    lv_obj_set_style_text_align(temp_label, LV_TEXT_ALIGN_CENTER, 0);

    temp_label_data = lv_label_create(src);
    lv_obj_set_size(temp_label_data, 250, 40);
    lv_obj_set_style_text_color(temp_label_data, lv_color_white(), 0);
    lv_label_set_text(temp_label_data, "0");
    lv_obj_set_style_text_font(temp_label_data, &lv_font_montserrat_20, 0);
    lv_obj_align(temp_label_data, LV_ALIGN_TOP_LEFT, 210, 400);
    lv_obj_set_style_text_align(temp_label_data, LV_TEXT_ALIGN_CENTER, 0);

#endif

    imu_data_timer = lv_timer_create(imu_data_update, 100, NULL);
    lv_timer_ready(imu_data_timer);
}