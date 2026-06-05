idf_component_register(
    SRCS
        "main.c"
        "time_utils.c"
        "rtc_ds3231.c"
        "storage_nvs.c"
        "scheduler.c"
        "temperature_utils.c"
        "ir_hitachi.c"
        "ir_receiver_debug.c"
        "serial_protocol.c"
    INCLUDE_DIRS "."
    REQUIRES
        driver
        esp_timer
        esp_driver_rmt
        nvs_flash
)
