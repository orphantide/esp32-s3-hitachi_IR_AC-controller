#pragma once

#include "esp_err.h"
#include "app_types.h"

esp_err_t rtc_ds3231_init(void);
esp_err_t rtc_ds3231_get_time(datetime_t *dt);
esp_err_t rtc_ds3231_set_time(const datetime_t *dt);
esp_err_t rtc_ds3231_time_is_valid(bool *valid);
