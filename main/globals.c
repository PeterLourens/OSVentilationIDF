#include "globals.h"

// I2C Devices new driver style
i2c_master_bus_handle_t i2c_bus0_handle = {0};
i2c_master_bus_handle_t i2c_bus1_handle = {0};
i2c_master_dev_handle_t tca9548_bus0_handle = {0};
i2c_master_dev_handle_t tca9548_bus1_handle = {0};
i2c_master_dev_handle_t aht20_bus0_handle = {0};
i2c_master_dev_handle_t aht20_bus1_handle = {0};
i2c_master_dev_handle_t scd41_bus0_handle = {0};
i2c_master_dev_handle_t scd41_bus1_handle = {0};

// Task handles
TaskHandle_t task_i2c_sensors = NULL;
TaskHandle_t task_i2c_rtc = NULL;

// Sensor configuration structure
bus_sensor_t bus0_sensor_settings[BUS_MAX_SENSORS];
bus_sensor_t bus1_sensor_settings[BUS_MAX_SENSORS];

// Date and time information from RTC
/*struct tm rtctime =
    {
        .tm_year = 116, // since 1900 (2016 - 1900)
        .tm_mon = 9,    // 0-based
        .tm_mday = 9,
        .tm_hour = 13,
        .tm_min = 50,
        .tm_sec = 10};*/