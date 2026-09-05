#include "ir_hitachi.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "esp_check.h"
#include "esp_log.h"
#include "app_config.h"
#include "temperature_utils.h"
#include "time_utils.h"

/*
 * Hitachi 29-Byte Protocol Implementation
 * Modulated Carrier: 38kHz, 33% Duty Cycle
 */

#define HITACHI_STATE_LEN           29

#define HITACHI_HDR_MARK_US         3400
#define HITACHI_HDR_SPACE_US        1600
#define HITACHI_BIT_MARK_US         430
#define HITACHI_ONE_SPACE_US        1260
#define HITACHI_ZERO_SPACE_US       410
#define HITACHI_FINAL_SPACE_US      3000

#define HITACHI_RMT_RESOLUTION_HZ   1000000
#define HITACHI_CARRIER_HZ          38000
#define HITACHI_TX_MEM_BLOCK_SYMBOLS 64
#define HITACHI_TX_TIMEOUT_MS       5000

static const char *TAG = "ir_hitachi29";
static rmt_channel_handle_t s_tx_channel;
static rmt_encoder_handle_t s_copy_encoder;

static uint8_t s_last_non_off_mode = AC_MODE_COOL;

static uint8_t rev8(uint8_t b)
{
    b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
    b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
    b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
    return b;
}

static uint8_t map_fan_speed(uint8_t fan_speed)
{
    // Values verified from real remote captures (raw[] shifted representation)
    switch (fan_speed) {
        case 1:  return 0x20; // 1档
        case 2:  return 0x60; // 2档
        case 3:  return 0x10; // 3档
        case 4:  return 0x50; // 4档
        case 5:  return 0x30; // 5档
        case 0:
        default: return 0x40; // Auto (Verified 0x40 is sent for Auto Fan by the physical remote)
    }
}

static void hitachi_build_state(const ac_state_t *state, uint8_t raw[HITACHI_STATE_LEN], uint8_t button_code)
{
    // 1. Initialize Constant Preamble (B00 - B08)
    raw[0] = 0xC0;
    raw[1] = 0x04;
    raw[2] = 0x06;
    raw[3] = 0x01;
    raw[4] = 0x7E;
    raw[5] = 0xC0;
    raw[6] = 0x3F;
    raw[7] = 0xC4;
    raw[8] = 0x64;

    // Default remaining bytes to 0
    memset(raw + 9, 0, HITACHI_STATE_LEN - 9);

    // Set swing options in Byte 14 and Byte 15
    raw[14] = state->swing_v ? 0x70 : 0x30;
    raw[15] = state->swing_h ? 0x70 : 0x30;
    raw[24] = 0x20;
    raw[25] = 0x08;

    uint8_t active_mode = state->mode;
    if (active_mode == AC_MODE_OFF) {
        active_mode = s_last_non_off_mode;
    } else {
        s_last_non_off_mode = active_mode;
    }

    // 2. Mode (B10) & Temp (B11)
    if (active_mode == AC_MODE_COOL) {
        raw[10] = 0x10;
        uint8_t temp_val = (state->temperature_x2 > 0) ? state->temperature_x2 : 48; // default 24C (48 in x2)
        raw[11] = rev8(temp_val * 2);
    } else if (active_mode == AC_MODE_HEAT) {
        raw[10] = 0x60; // 0x60 shifted left by 1 bit is 0xC0, which reversed is 3 (Heat mode in Hitachi AC)
        uint8_t temp_val = (state->temperature_x2 > 0) ? state->temperature_x2 : 48;
        raw[11] = rev8(temp_val * 2);
    } else { // AC_MODE_FAN
        raw[10] = 0x18;
        uint8_t temp_val = (state->temperature_x2 > 0) ? state->temperature_x2 : 50; // default 25C (50 in x2)
        raw[11] = rev8(temp_val * 2);
    }

    // 3. Fan Speed (B13) - all modes use the same mapping
    ESP_LOGI(TAG, "build_state: fan_speed=%u, mapped=0x%02X", (unsigned)state->fan_speed, (unsigned)map_fan_speed(state->fan_speed));
    raw[13] = map_fan_speed(state->fan_speed);

    // 4. Power (B18) and Button Code (B09)
    if (state->mode == AC_MODE_OFF) {
        raw[18] = 0x00;
    } else {
        raw[18] = 0x80;
    }

    raw[9] = button_code;

    // 5. Checksum (B28) is initialized to 0, will be calculated on unshifted bytes in send_state
    raw[28] = 0x00;
}

static void append_symbol(rmt_symbol_word_t *symbols, size_t *index,
                           uint16_t mark_us, uint16_t space_us)
{
    symbols[*index] = (rmt_symbol_word_t) {
        .level0 = 1,
        .duration0 = mark_us,
        .level1 = 0,
        .duration1 = space_us,
    };
    (*index)++;
}

static void append_byte_msb(rmt_symbol_word_t *symbols, size_t *index, uint8_t value)
{
    for (int bit = 7; bit >= 0; bit--) {
        bool one = (value >> bit) & 0x01;
        append_symbol(symbols, index, HITACHI_BIT_MARK_US,
                       one ? HITACHI_ONE_SPACE_US : HITACHI_ZERO_SPACE_US);
    }
}

static void append_section(rmt_symbol_word_t *symbols, size_t *index,
                           const uint8_t *data, size_t len,
                           bool with_header, uint16_t footer_space_us)
{
    if (with_header) {
        append_symbol(symbols, index, HITACHI_HDR_MARK_US, HITACHI_HDR_SPACE_US);
    }
    for (size_t i = 0; i < len; i++) {
        append_byte_msb(symbols, index, data[i]);
    }
    // Append the final trailing stop bit (footer mark)
    append_symbol(symbols, index, HITACHI_BIT_MARK_US, footer_space_us);
}

static esp_err_t transmit_section(const uint8_t *data, size_t len,
                                  bool with_header, uint16_t footer_space_us,
                                  size_t *sent_symbols)
{
    // 29 bytes * 8 bits/byte + 2 symbols = 234 symbols max.
    // Use size 300 to be completely safe from stack overflow.
    rmt_symbol_word_t symbols[300] = {0};
    size_t count = 0;
    append_section(symbols, &count, data, len, with_header, footer_space_us);

    rmt_transmit_config_t transmit_config = {
        .loop_count = 0,
    };
    esp_err_t err = rmt_transmit(s_tx_channel, s_copy_encoder, symbols,
                                 count * sizeof(rmt_symbol_word_t), &transmit_config);
    if (err == ESP_OK) {
        err = rmt_tx_wait_all_done(s_tx_channel, pdMS_TO_TICKS(HITACHI_TX_TIMEOUT_MS));
    }
    if (err == ESP_OK && sent_symbols) {
        *sent_symbols += count;
    }
    return err;
}

static void shift_left_1bit(const uint8_t *in, uint8_t *out, size_t len)
{
    uint8_t carry = 0;
    for (int i = (int)len - 1; i >= 0; i--) {
        uint8_t next_carry = (in[i] & 0x80) ? 1 : 0;
        out[i] = (in[i] << 1) | carry;
        carry = next_carry;
    }
}

esp_err_t ir_hitachi_init(void)
{
    rmt_tx_channel_config_t tx_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = APP_IR_TX_GPIO,
        .mem_block_symbols = HITACHI_TX_MEM_BLOCK_SYMBOLS,
        .resolution_hz = HITACHI_RMT_RESOLUTION_HZ,
        .trans_queue_depth = 4,
    };
    esp_err_t err = rmt_new_tx_channel(&tx_config, &s_tx_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "tx channel init failed: %s", esp_err_to_name(err));
        return err;
    }

    rmt_carrier_config_t carrier_config = {
        .frequency_hz = HITACHI_CARRIER_HZ,
        .duty_cycle = 0.33,
    };
    ESP_RETURN_ON_ERROR(rmt_apply_carrier(s_tx_channel, &carrier_config), TAG, "carrier failed");

    rmt_copy_encoder_config_t encoder_config = {};
    ESP_RETURN_ON_ERROR(rmt_new_copy_encoder(&encoder_config, &s_copy_encoder), TAG, "copy encoder failed");
    return rmt_enable(s_tx_channel);
}

esp_err_t ir_hitachi_send_state(const ac_state_t *state, const datetime_t *now)
{
    if (!state) {
        return ESP_ERR_INVALID_ARG;
    }

    static ac_state_t s_last_state = {0};
    static bool s_last_state_valid = false;

    bool previous_power_on = s_last_state_valid && s_last_state.mode != AC_MODE_OFF;
    bool power_changed = (state->mode != AC_MODE_OFF) != previous_power_on;

    esp_err_t err = ESP_OK;
    size_t count = 0;

    uint8_t button_code = 0x18; // Default: temp/mode/swing change (0x18 in raw_shifted = 0x0C decoded)
    if (power_changed) {
        button_code = 0x60; // Power toggle (0x60 in raw_shifted = 0x03 decoded)
    }
    uint8_t raw_shifted[HITACHI_STATE_LEN] = {0};
    hitachi_build_state(state, raw_shifted, button_code);

    uint8_t raw[HITACHI_STATE_LEN] = {0};
    shift_left_1bit(raw_shifted, raw, HITACHI_STATE_LEN);

    uint8_t sum = 62;
    for (int i = 0; i < 28; i++) {
        sum -= rev8(raw[i]);
    }
    raw[28] = rev8(sum);

    err = transmit_section(raw, HITACHI_STATE_LEN, true, HITACHI_FINAL_SPACE_US, &count);

    if (err == ESP_OK) {
        s_last_state = *state; // Update last state on success
        ESP_LOGI(TAG, "sent hitachi29 state button_code=0x%02X temp=%u mode=%u symbols=%u",
                 button_code, state->temperature_x2, state->mode, (unsigned)count);
    } else {
        ESP_LOGW(TAG, "hitachi29 transmit failed: %s", esp_err_to_name(err));
    }

    return err;
}
