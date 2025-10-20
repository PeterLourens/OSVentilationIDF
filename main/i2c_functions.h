#ifndef I2C_FUNCTIONS_H
#define I2C_FUNCTIONS_H

#include <stdio.h>
#include <aht.h>
#include <tca9548.h>
#include <scd4x.h>

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
esp_err_t i2c_init_sensor_aht20(int);
esp_err_t i2c_init_sensor_scd4x(int);
esp_err_t i2c_init_multiplexer_tca9548a(uint8_t);
esp_err_t i2c_init_sensor_bme680(int);
bme680_data_t i2c_sensor_read_bme680(int);

#endif