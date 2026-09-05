#pragma once

#include <stddef.h>
#include "esp_err.h"
#include "app_types.h"

esp_err_t scheduler_init(void);
esp_err_t scheduler_load(void);
esp_err_t scheduler_add_or_replace(uint16_t minute_of_day, uint8_t temperature_x2, uint8_t mode, uint8_t fan_speed, bool swing_v, bool swing_h);
esp_err_t scheduler_delete_index(size_t index);
esp_err_t scheduler_clear(void);
size_t scheduler_list(schedule_entry_t *out, size_t max_entries);
size_t scheduler_count(void);
bool scheduler_check_due(const datetime_t *now, schedule_entry_t *due);
