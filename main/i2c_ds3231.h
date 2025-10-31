#ifndef I2C_DS3231_H
#define I2C_DS3231_H

#define DS3231_REG_TIME 0x00

#include <stdio.h>

#include "globals.h"

// Function declarations
esp_err_t i2c_ds3231_init(i2c_master_bus_handle_t bus_handle, i2c_master_dev_handle_t *dev_handle);
esp_err_t i2c_ds3231_read_time(i2c_master_dev_handle_t dev_handle, struct tm *timeinfo);
esp_err_t update_ds3231_from_system_time(void);
esp_err_t sync_system_and_ds3231_with_ntp(i2c_master_dev_handle_t dev_handle);
uint8_t bin2bcd(uint8_t val);
esp_err_t i2c_ds3231_write_time(i2c_master_dev_handle_t dev_handle, const struct tm *timeinfo);

#endif