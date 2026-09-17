#include "time_service.h"

#include <time.h>
#include <ulog.h>

#include <drivers/rtc.h>

#ifdef PKG_NETUTILS_NTP
#include <ntp.h>
#endif

#define TIME_SERVICE_UTC_OFFSET_HOURS 8
#define TIME_SERVICE_NTP_STACK_SIZE  2048U
#define TIME_SERVICE_NTP_PRIORITY    RT_THREAD_PRIORITY_LOW
#define TIME_SERVICE_NTP_FIRST_DELAY_MS 3000U
#define TIME_SERVICE_NTP_RETRY_DELAY_MS 3000U
#define TIME_SERVICE_NTP_RETRY_COUNT 3U

static rt_mutex_t time_service_lock;
static rt_thread_t time_service_ntp_thread;
static bool time_service_initialized;
static bool time_service_valid;
static time_sync_source_t time_service_source = TIME_SYNC_SOURCE_NONE;

static const char *time_service_source_name(time_sync_source_t source)
{
    switch (source)
    {
    case TIME_SYNC_SOURCE_BLUETOOTH_PAN:
        return "Bluetooth PAN";
    case TIME_SYNC_SOURCE_ESP32C6:
        return "ESP32-C6";
    case TIME_SYNC_SOURCE_GPS:
        return "GPS";
    default:
        return "unknown";
    }
}

static bool time_service_is_leap_year(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static int time_service_days_in_month(int year, int month)
{
    static const uint8_t days[] = {
        31U, 28U, 31U, 30U, 31U, 30U,
        31U, 31U, 30U, 31U, 30U, 31U,
    };

    if (month < 1 || month > 12)
        return 0;
    if (month == 2 && time_service_is_leap_year(year))
        return 29;
    return days[month - 1];
}

static bool time_service_calendar_valid(int year, int month, int day,
                                        int hour, int minute, int second)
{
    int days;

    if (year < 2020 || year > 2099 || month < 1 || month > 12 ||
            hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
            second < 0 || second > 59)
    {
        return false;
    }
    days = time_service_days_in_month(year, month);
    return day >= 1 && day <= days;
}

static void time_service_increment_day(int *year, int *month, int *day)
{
    (*day)++;
    if (*day <= time_service_days_in_month(*year, *month))
        return;
    *day = 1;
    (*month)++;
    if (*month <= 12)
        return;
    *month = 1;
    (*year)++;
}

rt_err_t time_service_init(void)
{
    if (time_service_initialized)
        return RT_EOK;

    time_service_lock = rt_mutex_create("time_srv", RT_IPC_FLAG_PRIO);
    if (time_service_lock == RT_NULL)
        return -RT_ENOMEM;
    time_service_initialized = true;
    return RT_EOK;
}

bool time_service_is_valid(void)
{
    bool valid = false;

    if (!time_service_initialized || time_service_lock == RT_NULL)
        return false;
    if (rt_mutex_take(time_service_lock, RT_WAITING_FOREVER) == RT_EOK)
    {
        valid = time_service_valid;
        rt_mutex_release(time_service_lock);
    }
    return valid;
}

rt_err_t time_service_set_local_calendar(time_sync_source_t source,
                                         int year, int month, int day,
                                         int hour, int minute, int second)
{
    rt_err_t result;

    if (!time_service_initialized || time_service_lock == RT_NULL)
        return -RT_ERROR;
    if (source <= TIME_SYNC_SOURCE_NONE || source > TIME_SYNC_SOURCE_GPS ||
            !time_service_calendar_valid(year, month, day,
                                         hour, minute, second))
    {
        return -RT_EINVAL;
    }

    result = rt_mutex_take(time_service_lock, RT_WAITING_FOREVER);
    if (result != RT_EOK)
        return result;
    result = set_date((rt_uint32_t)year, (rt_uint32_t)month,
                      (rt_uint32_t)day);
    if (result == RT_EOK)
    {
        result = set_time((rt_uint32_t)hour, (rt_uint32_t)minute,
                          (rt_uint32_t)second);
    }
    if (result != RT_EOK)
    {
        rt_mutex_release(time_service_lock);
        LOG_E("Set RTC from %s failed: %d",
              time_service_source_name(source), result);
        return result;
    }

    time_service_valid = true;
    time_service_source = source;
    rt_mutex_release(time_service_lock);
    LOG_I("RTC synchronized by %s: %04d-%02d-%02d %02d:%02d:%02d UTC+8",
          time_service_source_name(source), year, month, day,
          hour, minute, second);
    return RT_EOK;
}

rt_err_t time_service_set_utc_calendar(time_sync_source_t source,
                                       int year, int month, int day,
                                       int hour, int minute, int second)
{
    if (!time_service_calendar_valid(year, month, day,
                                     hour, minute, second))
    {
        return -RT_EINVAL;
    }

    hour += TIME_SERVICE_UTC_OFFSET_HOURS;
    if (hour >= 24)
    {
        hour -= 24;
        time_service_increment_day(&year, &month, &day);
    }
    return time_service_set_local_calendar(source, year, month, day,
                                           hour, minute, second);
}

#ifdef PKG_NETUTILS_NTP
static void time_service_ntp_entry(void *parameter)
{
    time_sync_source_t source =
        (time_sync_source_t)(uintptr_t)parameter;
    uint8_t attempt;

    rt_thread_mdelay(TIME_SERVICE_NTP_FIRST_DELAY_MS);
    for (attempt = 0U; attempt < TIME_SERVICE_NTP_RETRY_COUNT; attempt++)
    {
        time_t utc_time = ntp_get_time(RT_NULL);

        if (utc_time > 0)
        {
            struct tm local_calendar;
            time_t local_time = utc_time +
                (TIME_SERVICE_UTC_OFFSET_HOURS * 60 * 60);

            if (gmtime_r(&local_time, &local_calendar) != RT_NULL &&
                    time_service_set_local_calendar(
                        source, local_calendar.tm_year + 1900,
                        local_calendar.tm_mon + 1,
                        local_calendar.tm_mday, local_calendar.tm_hour,
                        local_calendar.tm_min,
                        local_calendar.tm_sec) == RT_EOK)
            {
                break;
            }
        }
        if (attempt + 1U < TIME_SERVICE_NTP_RETRY_COUNT)
            rt_thread_mdelay(TIME_SERVICE_NTP_RETRY_DELAY_MS);
    }
    if (attempt >= TIME_SERVICE_NTP_RETRY_COUNT)
        LOG_W("NTP synchronization over %s failed",
              time_service_source_name(source));

    rt_mutex_take(time_service_lock, RT_WAITING_FOREVER);
    time_service_ntp_thread = RT_NULL;
    rt_mutex_release(time_service_lock);
}
#endif

rt_err_t time_service_request_ntp(time_sync_source_t source)
{
#ifdef PKG_NETUTILS_NTP
    rt_thread_t thread;
    rt_err_t result;

    if (!time_service_initialized || time_service_lock == RT_NULL)
        return -RT_ERROR;
    if (source != TIME_SYNC_SOURCE_BLUETOOTH_PAN)
        return -RT_EINVAL;

    result = rt_mutex_take(time_service_lock, RT_WAITING_FOREVER);
    if (result != RT_EOK)
        return result;
    if (time_service_ntp_thread != RT_NULL)
    {
        rt_mutex_release(time_service_lock);
        return -RT_EBUSY;
    }
    thread = rt_thread_create("time_ntp", time_service_ntp_entry,
                              (void *)(uintptr_t)source,
                              TIME_SERVICE_NTP_STACK_SIZE,
                              TIME_SERVICE_NTP_PRIORITY, 10U);
    if (thread == RT_NULL)
    {
        rt_mutex_release(time_service_lock);
        return -RT_ENOMEM;
    }
    time_service_ntp_thread = thread;
    rt_mutex_release(time_service_lock);

    result = rt_thread_startup(thread);
    if (result != RT_EOK)
    {
        rt_mutex_take(time_service_lock, RT_WAITING_FOREVER);
        time_service_ntp_thread = RT_NULL;
        rt_mutex_release(time_service_lock);
        rt_thread_delete(thread);
    }
    return result;
#else
    (void)source;
    return -RT_ENOSYS;
#endif
}
