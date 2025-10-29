#ifndef TASK_I2C_DS3231_H
#define TASK_I2C_DS3231_H

#include <stdio.h>

#include "globals.h"
#include "i2c_ds3231.h"

// Function declarations
void start_task_i2c_sensors(void);
void task_i2c_sensors_code(void *pvParameters);

#endif