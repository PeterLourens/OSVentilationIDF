#ifndef TASK_I2C_H
#define TASK_I2C_H

#include "globals.h"
#include "i2c_functions.h"

// Function declarations
void start_task_i2c_sensors(void);
void task_i2c_sensors_code(void *);
void read_sensors(void);

#endif