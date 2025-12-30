#ifndef I2C_FUNCTIONS_H
#define I2C_FUNCTIONS_H

#include <driver/i2c_master.h>
#include <esp_log.h>

#include "globals.h"

// Function declarations
esp_err_t i2c_init_master0(void);
esp_err_t i2c_init_master1(void);

#endif