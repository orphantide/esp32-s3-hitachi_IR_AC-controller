#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    AC_MODE_COOL = 0,
    AC_MODE_HEAT = 1,
    AC_MODE_FAN = 2,
    AC_MODE_OFF = 3,
} ac_mode_t;

typedef struct {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
} datetime_t;

typedef struct {
    uint8_t temperature_x2;
    uint8_t mode;
    uint8_t fan_speed;
    bool power_on;
    bool swing_v;
    bool swing_h;
} ac_state_t;

typedef struct {
    uint16_t minute_of_day;
    uint8_t temperature_x2;
    uint8_t mode;
    uint8_t fan_speed;
    bool enabled;
    bool swing_v;
    bool swing_h;
} schedule_entry_t;
