#ifndef __RTC_TIME_H__
#define __RTC_TIME_H__

#include "rtthread.h"
#include "bf0_hal.h"
#include "drv_io.h"
#include "stdio.h"
#include "string.h"
#include "time.h"
#include <rtdevice.h>

typedef struct date_time
{
    int year;     /* eg: 2024 */
    int month;    /* eg:  1 (1-12) */
    int day;      /* eg: 31 (1~xx) */
    int hour;     /* eg:  8 (0~23) */
    int minute;   /* eg: 30 (0-59) */
    int second;   /* eg:  0 (0~59) */
} date_time_t;

/**
 * @brief Convert local time to {date_time_t}
 */
#define LOCAL_TIME_2_DATE_TIME_T(DT, TM)      \
    (DT)->year = (TM)->tm_year + 1900;        \
    (DT)->month = (TM)->tm_mon + 1;           \
    (DT)->day = (TM)->tm_mday;                \
    (DT)->hour = (TM)->tm_hour;               \
    (DT)->minute = (TM)->tm_min;              \
    (DT)->second = (TM)->tm_sec;              \



void print_time(const char *tag, date_time_t dt);
rt_err_t set_date_time(date_time_t dt);
date_time_t *get_date_time(date_time_t *dt);
void set_alarm(int hour, int min, int sec);
rt_err_t get_time(date_time_t *current_dt);

#endif