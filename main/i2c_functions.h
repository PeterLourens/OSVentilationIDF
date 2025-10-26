#ifndef I2C_FUNCTIONS_H
#define I2C_FUNCTIONS_H

#include <stdio.h>

#include "globals.h"

// Function declarations
esp_err_t i2c_init_master0(void);
esp_err_t i2c_init_master1(void);

// TCA9548A functions
esp_err_t i2c_init_tca9548(i2c_master_bus_handle_t, i2c_master_dev_handle_t *);
esp_err_t i2c_tca9548_set_channel(i2c_master_dev_handle_t, uint8_t);

// AHT20/DHT20 functions
esp_err_t i2c_aht20_init(i2c_master_bus_handle_t, i2c_master_dev_handle_t *);
esp_err_t aht20_read(i2c_master_dev_handle_t, float *, float *);

// SCD41 functions
esp_err_t i2c_init_scd41(i2c_master_bus_handle_t, i2c_master_dev_handle_t *);
uint8_t scd41_crc8(const uint8_t *, size_t);
esp_err_t scd41_data_ready(i2c_master_dev_handle_t, bool *);
esp_err_t scd41_read(i2c_master_dev_handle_t, uint16_t *, float *, float *);

// Functions to init and read sensors on mux channels based on config struct
bool is_scd4x(const char *);
bool is_aht20_family(const char *);
esp_err_t init_sensors_on_mux_channels(void);
esp_err_t read_sensors_on_mux_channels(void);

#endif