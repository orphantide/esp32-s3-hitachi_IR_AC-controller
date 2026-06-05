#pragma once

#include "esp_err.h"
#include "app_types.h"
#include "app_config.h"

esp_err_t storage_nvs_init(void);
esp_err_t storage_load_state(ac_state_t *state);
esp_err_t storage_save_state(const ac_state_t *state);
esp_err_t storage_load_schedule(schedule_entry_t entries[APP_MAX_SCHEDULES]);
esp_err_t storage_save_schedule(const schedule_entry_t entries[APP_MAX_SCHEDULES]);
