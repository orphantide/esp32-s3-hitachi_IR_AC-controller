#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "app_types.h"

bool time_utils_valid_datetime(const datetime_t *dt);
bool time_utils_parse_datetime(const char *text, datetime_t *out);
bool time_utils_parse_hhmm(const char *text, uint16_t *minute_of_day);
void time_utils_format_datetime(const datetime_t *dt, char *out, size_t out_size);
uint16_t time_utils_minute_of_day(const datetime_t *dt);
