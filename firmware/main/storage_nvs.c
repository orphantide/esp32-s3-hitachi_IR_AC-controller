#include "storage_nvs.h"

#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#define STORAGE_NAMESPACE       "hitachi"
#define STORAGE_STATE_KEY       "state"
#define STORAGE_SCHEDULE_KEY    "schedule"
#define STORAGE_STATE_MAGIC     0x48414353U
#define STORAGE_SCHEDULE_MAGIC  0x48414350U
#define STORAGE_VERSION         2

static const char *TAG = "storage";

typedef struct {
    uint32_t magic;
    uint16_t version;
    ac_state_t state;
} stored_state_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t count;
    schedule_entry_t entries[APP_MAX_SCHEDULES];
} stored_schedule_t;

static esp_err_t open_storage(nvs_open_mode_t mode, nvs_handle_t *handle)
{
    return nvs_open(STORAGE_NAMESPACE, mode, handle);
}

esp_err_t storage_nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs init failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t storage_load_state(ac_state_t *state)
{
    if (!state) {
        return ESP_ERR_INVALID_ARG;
    }

    *state = (ac_state_t) {
        .temperature_x2 = 52,
        .mode = AC_MODE_COOL,
        .fan_speed = 0,
        .power_on = true,
        .swing_v = false,
        .swing_h = false,
    };

    nvs_handle_t handle;
    esp_err_t err = open_storage(NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    stored_state_t stored = {0};
    size_t len = sizeof(stored);
    err = nvs_get_blob(handle, STORAGE_STATE_KEY, &stored, &len);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    if (len != sizeof(stored) || stored.magic != STORAGE_STATE_MAGIC || stored.version != STORAGE_VERSION) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    *state = stored.state;
    return ESP_OK;
}

esp_err_t storage_save_state(const ac_state_t *state)
{
    if (!state) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle;
    esp_err_t err = open_storage(NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    stored_state_t stored = {
        .magic = STORAGE_STATE_MAGIC,
        .version = STORAGE_VERSION,
        .state = *state,
    };
    err = nvs_set_blob(handle, STORAGE_STATE_KEY, &stored, sizeof(stored));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t storage_load_schedule(schedule_entry_t entries[APP_MAX_SCHEDULES])
{
    if (!entries) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(entries, 0, sizeof(schedule_entry_t) * APP_MAX_SCHEDULES);

    nvs_handle_t handle;
    esp_err_t err = open_storage(NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    stored_schedule_t *stored = calloc(1, sizeof(stored_schedule_t));
    if (!stored) {
        nvs_close(handle);
        return ESP_ERR_NO_MEM;
    }

    size_t len = sizeof(*stored);
    err = nvs_get_blob(handle, STORAGE_SCHEDULE_KEY, stored, &len);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        free(stored);
        return ESP_OK;
    }
    if (err != ESP_OK) {
        free(stored);
        return err;
    }
    if (len != sizeof(*stored) || stored->magic != STORAGE_SCHEDULE_MAGIC ||
        stored->version != STORAGE_VERSION || stored->count != APP_MAX_SCHEDULES) {
        free(stored);
        return ESP_ERR_INVALID_RESPONSE;
    }
    memcpy(entries, stored->entries, sizeof(stored->entries));
    free(stored);
    return ESP_OK;
}

esp_err_t storage_save_schedule(const schedule_entry_t entries[APP_MAX_SCHEDULES])
{
    if (!entries) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t handle;
    esp_err_t err = open_storage(NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    stored_schedule_t *stored = calloc(1, sizeof(stored_schedule_t));
    if (!stored) {
        nvs_close(handle);
        return ESP_ERR_NO_MEM;
    }

    stored->magic = STORAGE_SCHEDULE_MAGIC;
    stored->version = STORAGE_VERSION;
    stored->count = APP_MAX_SCHEDULES;
    memcpy(stored->entries, entries, sizeof(stored->entries));
    err = nvs_set_blob(handle, STORAGE_SCHEDULE_KEY, stored, sizeof(*stored));
    free(stored);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}
