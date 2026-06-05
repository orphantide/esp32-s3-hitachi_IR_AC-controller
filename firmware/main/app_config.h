#pragma once

#define APP_I2C_SDA_GPIO          20
#define APP_I2C_SCL_GPIO          21
#define APP_IR_RX_GPIO            15
#define APP_IR_TX_GPIO            4
#define APP_IR_CAPTURE_MODE       0

#define APP_I2C_PORT              I2C_NUM_0
#define APP_I2C_FREQ_HZ           100000
#define APP_UART_PORT             UART_NUM_0
#define APP_UART_BAUD             115200

#define APP_MAX_SCHEDULES         120
#define APP_TEMP_MIN              16
#define APP_TEMP_MAX              30
#define APP_TEMP_X2_MIN           (APP_TEMP_MIN * 2)
#define APP_TEMP_X2_MAX           (APP_TEMP_MAX * 2)
