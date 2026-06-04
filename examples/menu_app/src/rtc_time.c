
#include "rtc_time.h"
#include <ntp.h>
date_time_t current_dt;

void print_time(const char *tag, date_time_t dt)
{
    printf("%s %04d %02d %02d %02d:%02d:%02d\n", tag, dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
}

/**
 * @brief  Set system date and time.
 * @param  dt date and time, eg: 2024.1.1 08:30:00
 *
 * @retval If success, return RT_EOK.
 */
rt_err_t set_date_time(date_time_t dt)
{
    time_t now;
    rt_device_t device;
    struct tm *p_tm;
    struct tm tm_new = {0};

    /* Get current time (calendar time). */
    now = time(RT_NULL);

    /* Lock scheduler */
    rt_enter_critical();
    /* Convert calendar time to local time. */
    p_tm = localtime(&now);
    /* Copy time */
    memcpy(&tm_new, p_tm, sizeof(struct tm));
    /* Unlock scheduler */
    rt_exit_critical();

    /* Update system time. */
    tm_new.tm_year = dt.year - 1900;
    tm_new.tm_mon = dt.month - 1;
    tm_new.tm_mday = dt.day;
    tm_new.tm_hour = dt.hour;
    tm_new.tm_min = dt.minute;
    tm_new.tm_sec = dt.second;
    /* Convert local time to calendar time. */
    now = mktime(&tm_new);

    /* Find RTC device. */
    device = rt_device_find("rtc");
    if (device == RT_NULL)
    {
        rt_kprintf("app_set_system_time not find device\n");
        return -RT_ERROR;
    }
    /* Update time to RTC device */
    rt_device_control(device, RT_DEVICE_CTRL_RTC_SET_TIME, &now);

    return RT_EOK;
}

/**
 * @brief  Get system date and time.
 * @param dt pointer to variable used to storing current system time.
 * @retval date_time_t current system time.
 */
date_time_t *get_date_time(date_time_t *dt)
{
    /* Get current time(calendar time) */
    time_t ts = time(NULL);
    /* Convert calendar time to local time */
    struct tm *p_tm = localtime(&ts);

    /* Convert local time to date_time_t  */
    LOCAL_TIME_2_DATE_TIME_T(dt, p_tm);

    return dt;
}

#if defined(RT_USING_RTC) && defined(RT_USING_ALARM)
/**
 * @brief  This function handles alarm.
 * @retval none
 */
static void alarm_callback(rt_alarm_t alarm, time_t timestamp)
{
    /* Convert calendar time to local time */
    struct tm *tm_expired = localtime(&timestamp);
    /* Convert local time to date_time_t  */
    date_time_t dt_expired = {0};

    /* Convert local time to date_time_t  */
    LOCAL_TIME_2_DATE_TIME_T(&dt_expired, tm_expired);
    print_time("Alarm triggered at ", dt_expired);
}

/**
 * @brief  Set alarm.
 * @retval none
 */
void set_alarm(int hour, int min, int sec)
{
    rt_alarm_t g_alarm;
    static struct rt_alarm_setup g_alarm_setup;
    static struct tm tm_now;
    time_t now, ts_new;

    memset(&g_alarm_setup, 0, sizeof(g_alarm_setup));

    /* Get current system time (calendar time) */
    now = time(NULL);
    /* Convert calendar time to UTC time */
    gmtime_r(&now, &g_alarm_setup.wktime);
    g_alarm_setup.wktime.tm_hour = hour;
    g_alarm_setup.wktime.tm_min = min;
    g_alarm_setup.wktime.tm_sec = sec;
    /* Alarm is oneshot alarm.
     * Alarm flags see RT_ALARM_XXX(RT_ALARM_DAILY/RT_ALARM_WEELY...) in alarm.h.
     */
    g_alarm_setup.flag = RT_ALARM_ONESHOT;
    /* Create alarm */
    g_alarm = rt_alarm_create(alarm_callback, &g_alarm_setup);
    /* Start alarm */
    rt_alarm_start(g_alarm);

    rt_kprintf("SET ONESHOT ALARM : [%02d:%02d:%02d] \n", g_alarm_setup.wktime.tm_hour, g_alarm_setup.wktime.tm_min, g_alarm_setup.wktime.tm_sec);
}
#endif

rt_err_t convert_timestamp_to_datetime(time_t timestamp, date_time_t *dt, rt_bool_t is_utc)
{
    struct tm tm_time;
    time_t adjusted_time = timestamp;

    /* 时区补偿（假设系统使用UTC+8时区） */
    const int timezone_offset = 8 * 3600;
    if (!is_utc)
    {
        adjusted_time += timezone_offset;
    }
    /* 时间有效性校验 */
    if (timestamp < 1577808000)
    { // 2020-01-01之后的时间视为有效
        return -RT_ERROR;
    }
    /* 转换为分解时间 */
    if (gmtime_r(&adjusted_time, &tm_time) == RT_NULL)
    {
        return -RT_ERROR;
    }
    /* 使用预定义宏转换 */
    LOCAL_TIME_2_DATE_TIME_T(dt, &tm_time);

/* 附加闰秒补偿（可选） */
#ifdef RTC_LEAP_SECOND_SUPPORT
    dt->second += get_leap_second_offset(timestamp);
#endif
    return RT_EOK;
}

rt_err_t get_time(date_time_t *current_dt)
{
    time_t cur_time = ntp_sync_to_rtc(NULL);
    if (cur_time)
    {
        // rt_kprintf("NTP Server Time: %s", ctime((const time_t *)&cur_time));
        if (convert_timestamp_to_datetime(cur_time, current_dt, true) == RT_EOK)
        {
            rt_kprintf("[System Time] %04d/%02d/%02d %02d:%02d:%02d\n",
                       current_dt->year, current_dt->month, current_dt->day,
                       current_dt->hour, current_dt->minute, current_dt->second);
            set_date_time(*current_dt);
        }
        else
        {
            rt_kprintf("Time conversion failed!\n");
        }
    }
    else
    {
        rt_kprintf("NTP Server Time Error!\n");
    }

    return cur_time > 0 ? RT_EOK : -RT_ERROR;
}
