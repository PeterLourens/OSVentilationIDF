#ifndef I2C_DS3231_H
#define I2C_DS3231_H

#include <stdio.h>

#include "globals.h"

// Function declarations
esp_err_t i2c_ds3231_init(i2c_master_bus_handle_t bus_handle, i2c_master_dev_handle_t *dev_handle);
esp_err_t i2c_ds3231_read_time(i2c_master_dev_handle_t dev_handle, struct tm *timeinfo);

#endif