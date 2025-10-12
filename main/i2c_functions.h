#ifndef I2C_FUNCTIONS_H
#define I2C_FUNCTIONS_H

#include <stdio.h>
#include <aht.h>
#include <tca9548.h>
#include <scd4x.h>

#include "globals.h"

// Function declarations
esp_err_t init_i2c_master0(void);
esp_err_t init_i2c_sensor_aht20(int);
esp_err_t init_i2c_sensor_scd4x(int);
esp_err_t init_i2c_multiplexer_tca9548a(void);

// Should dissapear
// void read_sensors(void);

#endif