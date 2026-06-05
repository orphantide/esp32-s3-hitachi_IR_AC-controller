#pragma once

#include "esp_err.h"
#include "app_types.h"

typedef esp_err_t (*serial_set_cb_t)(uint8_t temperature_x2, uint8_t mode, uint8_t fan_speed, const char *type);
typedef esp_err_t (*serial_time_cb_t)(const datetime_t *dt);

typedef struct {
    serial_set_cb_t set_cb;
    serial_time_cb_t time_cb;
    ac_state_t *state;
} serial_protocol_context_t;

void serial_protocol_handle_line(const char *line, const serial_protocol_context_t *ctx);
