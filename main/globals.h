#ifndef GLOBALS_H
#define GLOBALS_H

#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_err.h>
#include <driver/i2c_master.h>

// #include <aht.h>
// #include <tca9548.h>
// #include <scd4x.h>
// #include <bme680.h>
// #include <ds3231.h>

// I2C General configuration
#define I2C_MASTER_FREQ_HZ 100000

// I2C Bus0 configuration
#define I2C_MASTER_BUS0_SDA_IO 39
#define I2C_MASTER_BUS0_SCL_IO 38

// I2C Bus1 configuration
#define I2C_MASTER_BUS1_SDA_IO 41
#define I2C_MASTER_BUS1_SCL_IO 40

// I2C Device Addresses
#define TCA9548_I2C_ADDR 0x70   // Default address
#define AHT20_I2C_ADDR 0x38     // Default address
#define AHT_TYPE AHT_TYPE_AHT20 // AHT20 also works for DHT20
#define SCD41_I2C_ADDR 0x62     // Default address
#define DS3231_I2C_ADDR 0x68    // Default address

#define SMALL_CONFIG_ITEM 16
#define MEDIUM_CONFIG_ITEM 32
#define LARGE_CONFIG_ITEM 64
#define XLARGE_CONFIG_ITEM 128
#define XXLARGE_CONFIG_ITEM 1024
#define XXXLARGE_CONFIG_ITEM 2048

#define BUS_MAX_SENSORS 8

// I2C Devices new driver style
extern i2c_master_bus_handle_t i2c_bus0_handle;
extern i2c_master_bus_handle_t i2c_bus1_handle;
extern i2c_master_dev_handle_t tca9548_bus0_handle;
extern i2c_master_dev_handle_t tca9548_bus1_handle;

// Task handles
extern TaskHandle_t task_i2c_sensors;
extern TaskHandle_t task_i2c_rtc;

// Sensor configuration structure
typedef struct
{
    char type[SMALL_CONFIG_ITEM];
    char valve[MEDIUM_CONFIG_ITEM];
    char location[LARGE_CONFIG_ITEM];
    bool rh_enabled;
    bool co2_enabled;
} bus_sensor_t;

extern bus_sensor_t bus0_sensor_settings[];
extern bus_sensor_t bus1_sensor_settings[];

// extern struct tm rtctime;

#endif