#include "globals.h"

// I2C Devices
i2c_dev_t master0 = {0};         // Master bus0 device
i2c_dev_t master1 = {0};         // Master bus1 device
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
