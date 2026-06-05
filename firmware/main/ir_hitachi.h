#pragma once

#include "esp_err.h"
#include "app_types.h"

esp_err_t ir_hitachi_init(void);
esp_err_t ir_hitachi_send_state(const ac_state_t *state, const datetime_t *now);
