#ifndef I2C_FUNCTIONS_H
#define I2C_FUNCTIONS_H

#include <stdio.h>

#include "globals.h"

typedef struct
{
    float temperature;
    float humidity;
    float pressure;
    float gas_resistance;
    esp_err_t status;
} bme680_data_t;

// Function declarations
esp_err_t i2c_init_master0(void);
esp_err_t i2c_init_master1(void);
esp_err_t i2c_init_tca9548(void);
esp_err_t i2c_tca9548_set_channel(i2c_master_dev_handle_t, uint8_t);
esp_err_t i2c_aht20_init(void);
esp_err_t aht20_read(float *, float *);

// esp_err_t i2c_init_multiplexer_tca9548a(uint8_t); // Initialize TCA9548A multiplexer with bus number
// esp_err_t i2c_init_sensor_aht20(uint8_t, int);    // Initialize AHT20 sensor with bus number and channel
// esp_err_t i2c_init_sensor_scd4x(uint8_t, int);    // Initialize SCD4x sensor with bus number and channel
// esp_err_t i2c_init_rtc(void);

#endif