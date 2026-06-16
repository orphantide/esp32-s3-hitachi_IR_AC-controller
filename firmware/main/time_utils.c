#include "time_utils.h"

#include <stdio.h>
#include <time.h>
#include <sys/time.h>

static bool is_leap(int year)
{
    return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

static int days_in_month(int year, int month)
{
    static const int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month < 1 || month > 12) {
        return 0;
    }
    if (month == 2 && is_leap(year)) {
        return 29;
    }
    return days[month - 1];
}

bool time_utils_valid_datetime(const datetime_t *dt)
{
    if (!dt) {
        return false;
    }
    if (dt->year < 2024 || dt->year > 2099) {
        return false;
    }
    if (dt->month < 1 || dt->month > 12) {
        return false;
    }
    if (dt->day < 1 || dt->day > days_in_month(dt->year, dt->month)) {
        return false;
    }
    return dt->hour >= 0 && dt->hour <= 23 &&
           dt->minute >= 0 && dt->minute <= 59 &&
           dt->second >= 0 && dt->second <= 59;
}

bool time_utils_parse_datetime(const char *text, datetime_t *out)
{
    if (!text || !out) {
        return false;
    }
    datetime_t dt = {0};
    if (sscanf(text, "%d-%d-%d %d:%d:%d",
               &dt.year, &dt.month, &dt.day,
               &dt.hour, &dt.minute, &dt.second) != 6) {
        return false;
    }
    if (!time_utils_valid_datetime(&dt)) {
        return false;
    }
    *out = dt;
    return true;
}

bool time_utils_parse_hhmm(const char *text, uint16_t *minute_of_day)
{
    if (!text || !minute_of_day) {
        return false;
    }
    int hour = 0;
    int minute = 0;
    if (sscanf(text, "%d:%d", &hour, &minute) != 2) {
        return false;
    }
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        return false;
    }
    *minute_of_day = (uint16_t)(hour * 60 + minute);
    return true;
}

void time_utils_format_datetime(const datetime_t *dt, char *out, size_t out_size)
{
    if (!dt || !out || out_size == 0) {
        return;
    }
    snprintf(out, out_size, "%04d-%02d-%02d %02d:%02d:%02d",
             dt->year, dt->month, dt->day, dt->hour, dt->minute, dt->second);
}

uint16_t time_utils_minute_of_day(const datetime_t *dt)
{
    return (uint16_t)(dt->hour * 60 + dt->minute);
}

void time_utils_set_system_time(const datetime_t *dt)
{
    if (!dt) {
        return;
    }
    struct tm tm_info = {0};
    tm_info.tm_year = dt->year - 1900;
    tm_info.tm_mon = dt->month - 1;
    tm_info.tm_mday = dt->day;
    tm_info.tm_hour = dt->hour;
    tm_info.tm_min = dt->minute;
    tm_info.tm_sec = dt->second;
    tm_info.tm_isdst = -1;

    time_t t = mktime(&tm_info);
    if (t != (time_t)-1) {
        struct timeval tv = {
            .tv_sec = t,
            .tv_usec = 0
        };
        settimeofday(&tv, NULL);
    }
}

void time_utils_get_system_time(datetime_t *dt)
{
    if (!dt) {
        return;
    }
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm tm_info;
    localtime_r(&tv.tv_sec, &tm_info);

    dt->year = tm_info.tm_year + 1900;
    dt->month = tm_info.tm_mon + 1;
    dt->day = tm_info.tm_mday;
    dt->hour = tm_info.tm_hour;
    dt->minute = tm_info.tm_min;
    dt->second = tm_info.tm_sec;
}
