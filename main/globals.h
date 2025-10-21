#ifndef GLOBALS_H
#define GLOBALS_H

#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_err.h>
#include <driver/i2c_master.h>

#include <aht.h>
#include <tca9548.h>
#include <scd4x.h>
#include <bme680.h>

// I2C General configuration
#define I2C_MASTER_FREQ_HZ 100000

// I2C Bus0 configuration
// #define I2C_MASTER_NUM I2C_NUM_0
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
#define BME680_I2C_ADDR 0x76    // Default address
#define DS3231_I2C_ADDR 0x68    // Default address

#define SMALL_CONFIG_ITEM 16
#define MEDIUM_CONFIG_ITEM 32
#define LARGE_CONFIG_ITEM 64
#define XLARGE_CONFIG_ITEM 128
#define XXLARGE_CONFIG_ITEM 1024
#define XXXLARGE_CONFIG_ITEM 2048

#define BUS_MAX_SENSORS 8

// I2C Devices
extern i2c_dev_t master0;     // Master bus0 device
extern i2c_dev_t master1;     // Master bus1 device
extern aht_t aht20_bus0;      // AHT bus0 sensors
extern aht_t aht20_bus1;      // AHT bus1 sensors
extern i2c_dev_t scd41_bus0;  // SCD41 bus0 sensors
extern i2c_dev_t scd41_bus1;  // SCD41 bus1 sensors
extern bme680_t bme680_bus0;  // BME680 bus0 sensors
extern bme680_t bme680_bus1;  // BME680 bus1 sensors
extern i2c_dev_t i2c_switch0; // I2c multiplexer bus 0
extern i2c_dev_t i2c_switch1; // I2c multiplexer bus 1

// Task handles
extern TaskHandle_t task_i2c_sensors;

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

#endif