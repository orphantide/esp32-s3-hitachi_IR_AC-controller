#include "temperature_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_config.h"

bool temperature_parse_x2(const char *text, uint8_t *temperature_x2)
{
    if (!text || !temperature_x2 || text[0] == '\0') {
        return false;
    }

    char buf[12] = {0};
    strncpy(buf, text, sizeof(buf) - 1);

    char *dot = strchr(buf, '.');
    int half = 0;
    if (dot) {
        *dot = '\0';
        const char *frac = dot + 1;
        if (strcmp(frac, "0") == 0 || strcmp(frac, "00") == 0) {
            half = 0;
        } else if (strcmp(frac, "5") == 0 || strcmp(frac, "50") == 0) {
            half = 1;
        } else {
            return false;
        }
    }

    char *end = NULL;
    long whole = strtol(buf, &end, 10);
    if (!end || *end != '\0') {
        return false;
    }

    long x2 = whole * 2 + half;
    if (x2 < APP_TEMP_X2_MIN || x2 > APP_TEMP_X2_MAX) {
        return false;
    }
    *temperature_x2 = (uint8_t)x2;
    return true;
}

void temperature_format_x2(uint8_t temperature_x2, char *out, size_t out_size)
{
    if (!out || out_size == 0) {
        return;
    }
    if (temperature_x2 % 2 == 0) {
        snprintf(out, out_size, "%u", temperature_x2 / 2);
    } else {
        snprintf(out, out_size, "%u.5", temperature_x2 / 2);
    }
}

uint8_t temperature_round_to_whole(uint8_t temperature_x2)
{
    return (uint8_t)((temperature_x2 + 1) / 2);
}
