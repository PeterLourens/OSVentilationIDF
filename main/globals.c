#include "globals.h"

// I2C Devices
i2c_master_bus_handle_t bus0 = {0};
i2c_master_bus_handle_t bus1 = {0};
i2c_master_dev_handle_t tca9548_bus0 = {0};
i2c_master_dev_handle_t tca9548_bus1 = {0};

// Old
// i2c_dev_t master0 = {0};         // Master bus0 device
// i2c_dev_t master1 = {0};         // Master bus1 device
aht_t aht20_bus0 = {0};          // AHT sensors
aht_t aht20_bus1 = {0};          // AHT sensors
i2c_dev_t scd41_bus0 = {0};      // SCD41 sensors
i2c_dev_t scd41_bus1 = {0};      // SCD41 sensors
i2c_dev_t i2c_switch0 = {0};     // I2c multiplexer
i2c_dev_t i2c_switch1 = {0};     // I2c multiplexer
i2c_dev_t ds3231_rtc_bus1 = {0}; // DS3231 RTC

// Task handles
TaskHandle_t task_i2c_sensors = NULL;
TaskHandle_t task_i2c_rtc = NULL;

// Sensor configuration structure
bus_sensor_t bus0_sensor_settings[BUS_MAX_SENSORS];
bus_sensor_t bus1_sensor_settings[BUS_MAX_SENSORS];

// Date and time information from RTC
struct tm rtctime =
    {
        .tm_year = 116, // since 1900 (2016 - 1900)
        .tm_mon = 9,    // 0-based
        .tm_mday = 9,
        .tm_hour = 13,
        .tm_min = 50,
        .tm_sec = 10};