#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_log.h"
#include "app_config.h"
#include "app_types.h"
#include "ir_hitachi.h"
#include "ir_receiver_debug.h"
#include "rtc_ds3231.h"
#include "scheduler.h"
#include "serial_protocol.h"
#include "storage_nvs.h"
#include "temperature_utils.h"
#include "time_utils.h"

static const char *TAG = "app";
static ac_state_t s_state;

static void print_log_line(const datetime_t *now, const char *type, uint8_t temperature_x2, uint8_t mode, uint8_t fan_speed, bool swing_v, bool swing_h)
{
    char text[32] = {0};
    char temp_text[8] = {0};
    time_utils_format_datetime(now, text, sizeof(text));
    temperature_format_x2(temperature_x2, temp_text, sizeof(temp_text));
    printf("LOG,%s,%s,temperature,%s,mode,%u,fan_speed,%u,swing_v,%u,swing_h,%u\n",
           text, type, temp_text, mode, fan_speed, swing_v ? 1 : 0, swing_h ? 1 : 0);
}

static esp_err_t apply_control(uint8_t temperature_x2, uint8_t mode, uint8_t fan_speed, bool swing_v, bool swing_h, const char *type)
{
    datetime_t now = {0};
    time_utils_get_system_time(&now);

    s_state.temperature_x2 = temperature_x2;
    s_state.mode = mode;
    s_state.fan_speed = fan_speed;
    s_state.swing_v = swing_v;
    s_state.swing_h = swing_h;
    s_state.power_on = mode != AC_MODE_OFF;

    esp_err_t err = ir_hitachi_send_state(&s_state, &now);
    if (err == ESP_OK) {
        storage_save_state(&s_state);
        print_log_line(&now, type, temperature_x2, mode, fan_speed, swing_v, swing_h);
    }
    return err;
}

static esp_err_t sync_time(const datetime_t *dt)
{
    time_utils_set_system_time(dt);
    esp_err_t err = rtc_ds3231_set_time(dt);
    if (err == ESP_OK) {
        print_log_line(dt, "sync", s_state.temperature_x2, s_state.mode, s_state.fan_speed, s_state.swing_v, s_state.swing_h);
    }
    return err;
}

static void serial_task(void *arg)
{
    serial_protocol_context_t ctx = {
        .set_cb = apply_control,
        .time_cb = sync_time,
        .state = &s_state,
    };

    char line[128] = {0};
    size_t pos = 0;
    uint8_t ch = 0;
    while (true) {
        int len = uart_read_bytes(APP_UART_PORT, &ch, 1, pdMS_TO_TICKS(100));
        if (len <= 0) {
            continue;
        }
        if (ch == '\n' || ch == '\r') {
            if (pos > 0) {
                line[pos] = '\0';
                serial_protocol_handle_line(line, &ctx);
                pos = 0;
            }
        } else if (pos < sizeof(line) - 1) {
            line[pos++] = (char)ch;
        } else {
            pos = 0;
            printf("ERR,line_too_long\n");
        }
    }
}

static void schedule_task(void *arg)
{
    while (true) {
        datetime_t now = {0};
        schedule_entry_t due = {0};
        time_utils_get_system_time(&now);
        if (scheduler_check_due(&now, &due)) {
            apply_control(due.temperature_x2, due.mode, due.fan_speed, due.swing_v, due.swing_h, "scheduled");
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void uart_init(void)
{
    uart_config_t uart_config = {
        .baud_rate = APP_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(APP_UART_PORT, 4096, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(APP_UART_PORT, &uart_config));
}

void app_main(void)
{
    uart_init();

#if APP_IR_CAPTURE_MODE
    ESP_ERROR_CHECK(ir_receiver_debug_init());
    printf("READY,ir_capture,gpio,%u\n", APP_IR_RX_GPIO);
    return;
#endif

    ESP_ERROR_CHECK(storage_nvs_init());
    esp_err_t state_err = storage_load_state(&s_state);
    if (state_err != ESP_OK) {
        ESP_LOGW(TAG, "state load fallback: %s", esp_err_to_name(state_err));
        s_state = (ac_state_t) {
            .temperature_x2 = 52,
            .mode = AC_MODE_COOL,
            .fan_speed = 0,
            .power_on = true,
        };
    }

    ESP_ERROR_CHECK(scheduler_init());
    ESP_ERROR_CHECK(scheduler_load());
    ESP_ERROR_CHECK(rtc_ds3231_init());
    ESP_ERROR_CHECK(ir_hitachi_init());
    esp_err_t ir_rx_err = ir_receiver_debug_init();
    if (ir_rx_err != ESP_OK) {
        ESP_LOGW(TAG, "IR receiver debug disabled: %s", esp_err_to_name(ir_rx_err));
    }

    bool rtc_valid = false;
    datetime_t rtc_now = {0};
    if (rtc_ds3231_time_is_valid(&rtc_valid) == ESP_OK && rtc_valid) {
        if (rtc_ds3231_get_time(&rtc_now) == ESP_OK) {
            time_utils_set_system_time(&rtc_now);
            char formatted[32] = {0};
            time_utils_format_datetime(&rtc_now, formatted, sizeof(formatted));
            ESP_LOGI(TAG, "System time synced from DS3231: %s", formatted);
        }
    } else {
        ESP_LOGW(TAG, "DS3231 oscillator stop flag is set; sync time from GUI");
    }

    printf("READY,hitachi_ac_controller, schedules,%u\n", (unsigned)scheduler_count());
    xTaskCreate(serial_task, "serial_task", 4096, NULL, 8, NULL);
    xTaskCreate(schedule_task, "schedule_task", 4096, NULL, 6, NULL);
}
