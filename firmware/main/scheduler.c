#include "scheduler.h"

#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "app_config.h"
#include "storage_nvs.h"
#include "time_utils.h"

static schedule_entry_t s_entries[APP_MAX_SCHEDULES];
static SemaphoreHandle_t s_lock;
static int s_last_day = -1;
static int s_last_minute = -1;

static int compare_entries(const void *a, const void *b)
{
    const schedule_entry_t *ea = (const schedule_entry_t *)a;
    const schedule_entry_t *eb = (const schedule_entry_t *)b;
    if (!ea->enabled && eb->enabled) {
        return 1;
    }
    if (ea->enabled && !eb->enabled) {
        return -1;
    }
    return (int)ea->minute_of_day - (int)eb->minute_of_day;
}

static void lock(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
}

static void unlock(void)
{
    xSemaphoreGive(s_lock);
}

esp_err_t scheduler_init(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

esp_err_t scheduler_load(void)
{
    esp_err_t err = storage_load_schedule(s_entries);
    if (err != ESP_OK) {
        memset(s_entries, 0, sizeof(s_entries));
    }
    return err == ESP_ERR_INVALID_RESPONSE ? ESP_OK : err;
}

esp_err_t scheduler_add_or_replace(uint16_t minute_of_day, uint8_t temperature_x2, uint8_t mode, uint8_t fan_speed, bool swing_v, bool swing_h)
{
    if (minute_of_day >= 1440 || temperature_x2 < APP_TEMP_X2_MIN || temperature_x2 > APP_TEMP_X2_MAX || mode > AC_MODE_OFF || fan_speed > 5) {
        return ESP_ERR_INVALID_ARG;
    }

    lock();
    int free_index = -1;
    for (size_t i = 0; i < APP_MAX_SCHEDULES; i++) {
        if (s_entries[i].enabled && s_entries[i].minute_of_day == minute_of_day) {
            s_entries[i].temperature_x2 = temperature_x2;
            s_entries[i].mode = mode;
            s_entries[i].fan_speed = fan_speed;
            s_entries[i].swing_v = swing_v;
            s_entries[i].swing_h = swing_h;
            esp_err_t err = storage_save_schedule(s_entries);
            unlock();
            return err;
        }
        if (!s_entries[i].enabled && free_index < 0) {
            free_index = (int)i;
        }
    }

    if (free_index < 0) {
        unlock();
        return ESP_ERR_NO_MEM;
    }
    s_entries[free_index] = (schedule_entry_t) {
        .minute_of_day = minute_of_day,
        .temperature_x2 = temperature_x2,
        .mode = mode,
        .fan_speed = fan_speed,
        .swing_v = swing_v,
        .swing_h = swing_h,
        .enabled = true,
    };
    qsort(s_entries, APP_MAX_SCHEDULES, sizeof(schedule_entry_t), compare_entries);
    esp_err_t err = storage_save_schedule(s_entries);
    unlock();
    return err;
}

esp_err_t scheduler_delete_index(size_t index)
{
    lock();
    size_t enabled_index = 0;
    for (size_t i = 0; i < APP_MAX_SCHEDULES; i++) {
        if (!s_entries[i].enabled) {
            continue;
        }
        if (enabled_index == index) {
            memset(&s_entries[i], 0, sizeof(s_entries[i]));
            qsort(s_entries, APP_MAX_SCHEDULES, sizeof(schedule_entry_t), compare_entries);
            esp_err_t err = storage_save_schedule(s_entries);
            unlock();
            return err;
        }
        enabled_index++;
    }
    unlock();
    return ESP_ERR_NOT_FOUND;
}

esp_err_t scheduler_clear(void)
{
    lock();
    memset(s_entries, 0, sizeof(s_entries));
    esp_err_t err = storage_save_schedule(s_entries);
    unlock();
    return err;
}

size_t scheduler_list(schedule_entry_t *out, size_t max_entries)
{
    lock();
    size_t count = 0;
    for (size_t i = 0; i < APP_MAX_SCHEDULES; i++) {
        if (!s_entries[i].enabled) {
            continue;
        }
        if (out && count < max_entries) {
            out[count] = s_entries[i];
        }
        count++;
    }
    unlock();
    return count;
}

size_t scheduler_count(void)
{
    return scheduler_list(NULL, 0);
}

bool scheduler_check_due(const datetime_t *now, schedule_entry_t *due)
{
    if (!now || !due) {
        return false;
    }

    int day_key = now->year * 10000 + now->month * 100 + now->day;
    int minute = time_utils_minute_of_day(now);
    if (s_last_day == day_key && s_last_minute == minute) {
        return false;
    }
    s_last_day = day_key;
    s_last_minute = minute;

    lock();
    for (size_t i = 0; i < APP_MAX_SCHEDULES; i++) {
        if (s_entries[i].enabled && s_entries[i].minute_of_day == minute) {
            *due = s_entries[i];
            unlock();
            return true;
        }
    }
    unlock();
    return false;
}
