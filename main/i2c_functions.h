#ifndef I2C_FUNCTIONS_H
#define I2C_FUNCTIONS_H

#include <stdio.h>

#include "globals.h"

// Function declarations
esp_err_t i2c_init_master0(void);
esp_err_t i2c_init_master1(void);
esp_err_t i2c_init_tca9548(i2c_master_bus_handle_t, i2c_master_dev_handle_t *);
esp_err_t i2c_tca9548_set_channel(i2c_master_dev_handle_t, uint8_t);
esp_err_t i2c_aht20_init(i2c_master_bus_handle_t, i2c_master_dev_handle_t *);
esp_err_t aht20_read(i2c_master_dev_handle_t, float *, float *);
esp_err_t i2c_init_scd41(i2c_master_bus_handle_t, i2c_master_dev_handle_t *);
uint8_t scd41_crc8(const uint8_t *, size_t);

#endif