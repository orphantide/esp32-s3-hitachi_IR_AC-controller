#include "serial_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_err.h"
#include "app_config.h"
#include "ir_hitachi.h"
#include "scheduler.h"
#include "temperature_utils.h"
#include "time_utils.h"

static const char *mode_name(uint8_t mode)
{
    switch (mode) {
    case AC_MODE_COOL:
        return "cool";
    case AC_MODE_HEAT:
        return "heat";
    case AC_MODE_FAN:
        return "fan";
    case AC_MODE_OFF:
        return "off";
    default:
        return "unknown";
    }
}

static void trim_line(char *text)
{
    size_t len = strlen(text);
    while (len > 0 && (text[len - 1] == '\r' || text[len - 1] == '\n' || text[len - 1] == ' ')) {
        text[len - 1] = '\0';
        len--;
    }
}

static bool parse_u8(const char *text, uint8_t *out)
{
    if (!text || !out) {
        return false;
    }
    char *end = NULL;
    long value = strtol(text, &end, 10);
    if (!end || *end != '\0' || value < 0 || value > 255) {
        return false;
    }
    *out = (uint8_t)value;
    return true;
}

static void handle_status(const serial_protocol_context_t *ctx)
{
    size_t count = scheduler_count();
    char temp_text[8] = {0};
    temperature_format_x2(ctx->state->temperature_x2, temp_text, sizeof(temp_text));
    printf("OK,STATUS,temperature,%s,mode,%u,mode_name,%s,fan_speed,%u,power,%u,swing_v,%u,swing_h,%u,schedules,%u\n",
           temp_text,
           ctx->state->mode,
           mode_name(ctx->state->mode),
           ctx->state->fan_speed,
           ctx->state->power_on ? 1 : 0,
           ctx->state->swing_v ? 1 : 0,
           ctx->state->swing_h ? 1 : 0,
           (unsigned)count);
}

static void handle_list(void)
{
    schedule_entry_t entries[APP_MAX_SCHEDULES];
    size_t count = scheduler_list(entries, APP_MAX_SCHEDULES);
    printf("OK,LIST,%u\n", (unsigned)count);
    for (size_t i = 0; i < count; i++) {
        char temp_text[8] = {0};
        temperature_format_x2(entries[i].temperature_x2, temp_text, sizeof(temp_text));
        printf("ITEM,%u,%02u:%02u,%s,%u,%s,%u,%u,%u\n",
               (unsigned)i,
               entries[i].minute_of_day / 60,
               entries[i].minute_of_day % 60,
               temp_text,
               entries[i].mode,
               mode_name(entries[i].mode),
               entries[i].fan_speed,
               entries[i].swing_v ? 1 : 0,
               entries[i].swing_h ? 1 : 0);
    }
}

void serial_protocol_handle_line(const char *line, const serial_protocol_context_t *ctx)
{
    if (!line || !ctx) {
        return;
    }

    char buf[128] = {0};
    strncpy(buf, line, sizeof(buf) - 1);
    trim_line(buf);
    if (buf[0] == '\0') {
        return;
    }

    char *cmd = strtok(buf, ",");
    if (!cmd) {
        printf("ERR,empty_command\n");
        return;
    }

    if (strcmp(cmd, "STATUS") == 0) {
        handle_status(ctx);
        return;
    }

    if (strcmp(cmd, "LIST") == 0) {
        handle_list();
        return;
    }

    if (strcmp(cmd, "CLEAR") == 0) {
        esp_err_t err = scheduler_clear();
        printf(err == ESP_OK ? "OK,CLEAR\n" : "ERR,CLEAR,%s\n", esp_err_to_name(err));
        return;
    }

    if (strcmp(cmd, "SET") == 0) {
        uint8_t temperature_x2 = 0;
        uint8_t mode = 0;
        uint8_t fan_speed = 0;
        bool swing_v = ctx->state->swing_v;
        bool swing_h = ctx->state->swing_h;
        if (!temperature_parse_x2(strtok(NULL, ","), &temperature_x2) ||
            !parse_u8(strtok(NULL, ","), &mode) ||
            mode > AC_MODE_OFF) {
            printf("ERR,bad_set\n");
            return;
        }
        char *fan_speed_str = strtok(NULL, ",");
        if (fan_speed_str) {
            if (!parse_u8(fan_speed_str, &fan_speed) || fan_speed > 5) {
                printf("ERR,bad_set_fan_speed\n");
                return;
            }
        }
        char *swing_v_str = strtok(NULL, ",");
        if (swing_v_str) {
            uint8_t val = 0;
            if (!parse_u8(swing_v_str, &val) || val > 1) {
                printf("ERR,bad_set_swing_v\n");
                return;
            }
            swing_v = val == 1;
        }
        char *swing_h_str = strtok(NULL, ",");
        if (swing_h_str) {
            uint8_t val = 0;
            if (!parse_u8(swing_h_str, &val) || val > 1) {
                printf("ERR,bad_set_swing_h\n");
                return;
            }
            swing_h = val == 1;
        }
        esp_err_t err = ctx->set_cb(temperature_x2, mode, fan_speed, swing_v, swing_h, "manual");
        printf(err == ESP_OK ? "OK,SET\n" : "ERR,SET,%s\n", esp_err_to_name(err));
        return;
    }

    if (strcmp(cmd, "SCHEDULE") == 0) {
        char *time_text = strtok(NULL, ",");
        uint8_t temperature_x2 = 0;
        uint8_t mode = 0;
        uint8_t fan_speed = 0;
        bool swing_v = false;
        bool swing_h = false;
        uint16_t minute_of_day = 0;
        if (!time_utils_parse_hhmm(time_text, &minute_of_day) ||
            !temperature_parse_x2(strtok(NULL, ","), &temperature_x2) ||
            !parse_u8(strtok(NULL, ","), &mode) ||
            mode > AC_MODE_OFF) {
            printf("ERR,bad_schedule\n");
            return;
        }
        char *fan_speed_str = strtok(NULL, ",");
        if (fan_speed_str) {
            if (!parse_u8(fan_speed_str, &fan_speed) || fan_speed > 5) {
                printf("ERR,bad_schedule_fan_speed\n");
                return;
            }
        }
        char *swing_v_str = strtok(NULL, ",");
        if (swing_v_str) {
            uint8_t val = 0;
            if (!parse_u8(swing_v_str, &val) || val > 1) {
                printf("ERR,bad_schedule_swing_v\n");
                return;
            }
            swing_v = val == 1;
        }
        char *swing_h_str = strtok(NULL, ",");
        if (swing_h_str) {
            uint8_t val = 0;
            if (!parse_u8(swing_h_str, &val) || val > 1) {
                printf("ERR,bad_schedule_swing_h\n");
                return;
            }
            swing_h = val == 1;
        }
        esp_err_t err = scheduler_add_or_replace(minute_of_day, temperature_x2, mode, fan_speed, swing_v, swing_h);
        printf(err == ESP_OK ? "OK,SCHEDULE\n" : "ERR,SCHEDULE,%s\n", esp_err_to_name(err));
        return;
    }

    if (strcmp(cmd, "DELETE") == 0) {
        char *index_text = strtok(NULL, ",");
        if (!index_text) {
            printf("ERR,bad_delete\n");
            return;
        }
        char *end = NULL;
        long index = strtol(index_text, &end, 10);
        if (!end || *end != '\0' || index < 0) {
            printf("ERR,bad_delete\n");
            return;
        }
        esp_err_t err = scheduler_delete_index((size_t)index);
        printf(err == ESP_OK ? "OK,DELETE\n" : "ERR,DELETE,%s\n", esp_err_to_name(err));
        return;
    }

    if (strcmp(cmd, "TIME") == 0) {
        char *dt_text = strtok(NULL, "");
        datetime_t dt = {0};
        if (!time_utils_parse_datetime(dt_text, &dt)) {
            printf("ERR,bad_time\n");
            return;
        }
        esp_err_t err = ctx->time_cb(&dt);
        printf(err == ESP_OK ? "OK,TIME\n" : "ERR,TIME,%s\n", esp_err_to_name(err));
        return;
    }

    printf("ERR,unknown_command,%s\n", cmd);
}
