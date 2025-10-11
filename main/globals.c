#include "globals.h"

// I2C Devices
i2c_dev_t dev = {0};        // Master bus device
aht_t aht20 = {0};          // AHT sensors
i2c_dev_t scd41 = {0};      // SCD41 sensors
i2c_dev_t i2c_switch = {0}; // I2c multiplexer

// Task handles
TaskHandle_t task_i2c = NULL;
