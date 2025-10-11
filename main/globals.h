#ifndef GLOBALS_H
#define GLOBALS_H

#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_err.h>

#include <aht.h>
#include <tca9548.h>
#include <scd4x.h>

// I2C Bus0 configuration
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_SDA_IO 39
#define I2C_MASTER_SCL_IO 38
#define I2C_MASTER_FREQ_HZ 100000

#define TCA9548_ADDR 0x70       // Default address
#define AHT20_ADDR 0x38         // Default address
#define SCD41_ADDR 0x62         // Default address
#define AHT_TYPE AHT_TYPE_AHT20 // AHT20 also works for DHT20

// I2C Devices
extern i2c_dev_t dev;        // Master bus device
extern aht_t aht20;          // AHT sensors
extern i2c_dev_t scd41;      // SCD41 sensors
extern i2c_dev_t i2c_switch; // I2c multiplexer

// Task handles
extern TaskHandle_t task_i2c;

#endif