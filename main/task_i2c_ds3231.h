#ifndef TASK_I2C_DS3231_H
#define TASK_I2C_DS3231_H

#include "driver/i2c_master.h"
#include <stdio.h>

#include "globals.h"
#include "i2c_ds3231.h"
#include "logging.h"

// Function declarations
void start_task_i2c_ds3231(void);
void task_i2c_ds3231_code(void *pvParameters);

#endif