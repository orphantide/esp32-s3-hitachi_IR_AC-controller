#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool temperature_parse_x2(const char *text, uint8_t *temperature_x2);
void temperature_format_x2(uint8_t temperature_x2, char *out, size_t out_size);
uint8_t temperature_round_to_whole(uint8_t temperature_x2);
