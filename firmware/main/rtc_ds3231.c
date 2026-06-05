#include "rtc_ds3231.h"

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "app_config.h"
#include "time_utils.h"

#define DS3231_ADDR             0x68
#define DS3231_REG_SECONDS      0x00
#define DS3231_REG_STATUS       0x0F
#define DS3231_STATUS_OSF       0x80
#define I2C_TIMEOUT_MS          1000

static const char *TAG = "rtc_ds3231";
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static SemaphoreHandle_t s_i2c_mutex;

static uint8_t bcd_to_bin(uint8_t value)
{
    return (uint8_t)(((value >> 4) * 10) + (value & 0x0F));
}

static uint8_t bin_to_bcd(uint8_t value)
{
    return (uint8_t)(((value / 10) << 4) | (value % 10));
}

static esp_err_t reg_read(uint8_t reg, uint8_t *data, size_t len)
{
    if (!s_i2c_mutex || xSemaphoreTake(s_i2c_mutex, pdMS_TO_TICKS(I2C_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = i2c_master_transmit_receive(s_dev, &reg, 1, data, len, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    xSemaphoreGive(s_i2c_mutex);
    return err;
}

static esp_err_t reg_write(uint8_t reg, const uint8_t *data, size_t len)
{
    uint8_t buf[16] = {0};
    if (len + 1 > sizeof(buf)) {
        return ESP_ERR_INVALID_SIZE;
    }
    buf[0] = reg;
    for (size_t i = 0; i < len; i++) {
        buf[i + 1] = data[i];
    }
    if (!s_i2c_mutex || xSemaphoreTake(s_i2c_mutex, pdMS_TO_TICKS(I2C_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = i2c_master_transmit(s_dev, buf, len + 1, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    xSemaphoreGive(s_i2c_mutex);
    return err;
}

esp_err_t rtc_ds3231_init(void)
{
    if (!s_i2c_mutex) {
        s_i2c_mutex = xSemaphoreCreateMutex();
        if (!s_i2c_mutex) {
            return ESP_ERR_NO_MEM;
        }
    }

    i2c_master_bus_config_t bus_config = {
        .i2c_port = APP_I2C_PORT,
        .sda_io_num = APP_I2C_SDA_GPIO,
        .scl_io_num = APP_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_config, &s_bus);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "i2c bus init failed: %s", esp_err_to_name(err));
        return err;
    }

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = DS3231_ADDR,
        .scl_speed_hz = APP_I2C_FREQ_HZ,
    };
    err = i2c_master_bus_add_device(s_bus, &dev_config, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ds3231 add device failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t rtc_ds3231_get_time(datetime_t *dt)
{
    if (!dt) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t data[7] = {0};
    esp_err_t err = reg_read(DS3231_REG_SECONDS, data, sizeof(data));
    if (err != ESP_OK) {
        return err;
    }

    datetime_t tmp = {
        .second = bcd_to_bin(data[0] & 0x7F),
        .minute = bcd_to_bin(data[1] & 0x7F),
        .hour = bcd_to_bin(data[2] & 0x3F),
        .day = bcd_to_bin(data[4] & 0x3F),
        .month = bcd_to_bin(data[5] & 0x1F),
        .year = 2000 + bcd_to_bin(data[6]),
    };
    if (!time_utils_valid_datetime(&tmp)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    *dt = tmp;
    return ESP_OK;
}

esp_err_t rtc_ds3231_set_time(const datetime_t *dt)
{
    if (!time_utils_valid_datetime(dt)) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t data[7] = {
        bin_to_bcd((uint8_t)dt->second),
        bin_to_bcd((uint8_t)dt->minute),
        bin_to_bcd((uint8_t)dt->hour),
        1,
        bin_to_bcd((uint8_t)dt->day),
        bin_to_bcd((uint8_t)dt->month),
        bin_to_bcd((uint8_t)(dt->year - 2000)),
    };
    esp_err_t err = reg_write(DS3231_REG_SECONDS, data, sizeof(data));
    if (err != ESP_OK) {
        return err;
    }

    uint8_t status = 0;
    err = reg_read(DS3231_REG_STATUS, &status, 1);
    if (err != ESP_OK) {
        return err;
    }
    status &= (uint8_t)~DS3231_STATUS_OSF;
    return reg_write(DS3231_REG_STATUS, &status, 1);
}

esp_err_t rtc_ds3231_time_is_valid(bool *valid)
{
    if (!valid) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t status = 0;
    esp_err_t err = reg_read(DS3231_REG_STATUS, &status, 1);
    if (err != ESP_OK) {
        return err;
    }
    *valid = (status & DS3231_STATUS_OSF) == 0;
    return ESP_OK;
}
