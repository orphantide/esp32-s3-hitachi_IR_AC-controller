#include "ir_receiver_debug.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/rmt_rx.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "app_config.h"

#define IR_RX_RESOLUTION_HZ     1000000
#define IR_RX_SYMBOLS           256
#define IR_RX_MEM_BLOCK_SYMBOLS 192
#define IR_RX_PRINT_SYMBOLS     220
#define IR_GPIO_EDGES           900
#define IR_GPIO_GAP_US          80000

static const char *TAG = "ir_rx";
static rmt_channel_handle_t s_rx_channel;
static QueueHandle_t s_rx_queue;
static rmt_symbol_word_t s_raw_symbols[IR_RX_SYMBOLS];

#if APP_IR_CAPTURE_MODE
typedef struct {
    uint8_t level;
    uint32_t duration_us;
} gpio_pulse_t;

static gpio_pulse_t s_gpio_pulses[IR_GPIO_EDGES];
static volatile size_t s_gpio_count;
static volatile bool s_gpio_ready;
static volatile int64_t s_last_edge_us;
static volatile uint8_t s_last_level;

static void IRAM_ATTR gpio_edge_isr(void *arg)
{
    int64_t now = esp_timer_get_time();
    uint8_t level = (uint8_t)gpio_get_level(APP_IR_RX_GPIO);
    int64_t duration = now - s_last_edge_us;

    /* New edge after a long gap: do NOT store — task will flush first */
    if (duration > IR_GPIO_GAP_US) {
        if (s_gpio_count > 10) {
            s_gpio_ready = true;
        } else {
            s_gpio_count = 0;   /* stale noise, restart */
        }
    }

    if (!s_gpio_ready && s_gpio_count < IR_GPIO_EDGES) {
        s_gpio_pulses[s_gpio_count++] = (gpio_pulse_t) {
            .level = s_last_level,
            .duration_us = duration > 0 ? (uint32_t)duration : 0,
        };
        if (s_gpio_count >= IR_GPIO_EDGES) {
            s_gpio_ready = true;
        }
    }

    s_last_edge_us = now;
    s_last_level = level;
}

static void gpio_capture_task(void *arg)
{
    while (true) {
        /* Timeout detection: if pulses accumulated but no new edge for
           IR_GPIO_GAP_US, the frame is complete. This means ONE button
           press is sufficient — no need to press a second time.        */
        if (!s_gpio_ready && s_gpio_count > 10) {
            int64_t elapsed = esp_timer_get_time() - s_last_edge_us;
            if (elapsed > (int64_t)IR_GPIO_GAP_US) {
                s_gpio_ready = true;
            }
        }

        if (s_gpio_ready) {
            gpio_intr_disable(APP_IR_RX_GPIO);
            size_t count = s_gpio_count;
            if (count > IR_GPIO_EDGES) {
                count = IR_GPIO_EDGES;
            }
            printf("IRGPIO,%u", (unsigned)count);
            for (size_t i = 0; i < count; i++) {
                printf(",%u:%u", s_gpio_pulses[i].level, (unsigned)s_gpio_pulses[i].duration_us);
            }
            printf("\n");
            s_gpio_count = 0;
            s_gpio_ready = false;
            s_last_edge_us = esp_timer_get_time();
            s_last_level = (uint8_t)gpio_get_level(APP_IR_RX_GPIO);
            gpio_intr_enable(APP_IR_RX_GPIO);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
#endif


static bool on_recv_done(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *user_data)
{
    BaseType_t high_task_wakeup = pdFALSE;
    QueueHandle_t queue = (QueueHandle_t)user_data;
    xQueueSendFromISR(queue, edata, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}

static void rx_task(void *arg)
{
    rmt_receive_config_t receive_config = {
        .signal_range_min_ns = 1250,
        .signal_range_max_ns = 20000000,
    };
    ESP_ERROR_CHECK(rmt_receive(s_rx_channel, s_raw_symbols, sizeof(s_raw_symbols), &receive_config));

    rmt_rx_done_event_data_t rx_data;
    while (true) {
        if (xQueueReceive(s_rx_queue, &rx_data, portMAX_DELAY) == pdPASS) {
            printf("IRRAW,%u", (unsigned)rx_data.num_symbols);
            for (size_t i = 0; i < rx_data.num_symbols && i < IR_RX_PRINT_SYMBOLS; i++) {
                printf(",%u:%u:%u:%u",
                       rx_data.received_symbols[i].level0,
                       rx_data.received_symbols[i].duration0,
                       rx_data.received_symbols[i].level1,
                       rx_data.received_symbols[i].duration1);
            }
            printf("\n");
            ESP_ERROR_CHECK(rmt_receive(s_rx_channel, s_raw_symbols, sizeof(s_raw_symbols), &receive_config));
        }
    }
}

esp_err_t ir_receiver_debug_init(void)
{
#if APP_IR_CAPTURE_MODE
    gpio_config_t input_config = {
        .pin_bit_mask = 1ULL << APP_IR_RX_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&input_config), TAG, "gpio config failed");
    s_last_edge_us = esp_timer_get_time();
    s_last_level = (uint8_t)gpio_get_level(APP_IR_RX_GPIO);
    ESP_RETURN_ON_ERROR(gpio_install_isr_service(0), TAG, "gpio isr service failed");
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(APP_IR_RX_GPIO, gpio_edge_isr, NULL), TAG, "gpio isr add failed");
    xTaskCreate(gpio_capture_task, "ir_gpio_capture", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "gpio capture enabled on GPIO%u idle=%u", APP_IR_RX_GPIO, s_last_level);
    return ESP_OK;
#else
    return ESP_OK;
#endif
}
