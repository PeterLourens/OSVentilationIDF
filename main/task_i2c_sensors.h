/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

/**
 * @file task_i2c_sensors.h
 * @brief Public task declarations for I2C sensor sampling and averaging.
 */

#ifndef TASK_I2C_SENSORS_H
#define TASK_I2C_SENSORS_H

#include <esp_log.h>
#include <esp_attr.h>

#include "globals.h"
#include "i2c_sensors.h"
#include "driver/i2c_master.h"
#include "logging.h"

// Function declarations
void start_task_i2c_sensors(void);
void task_i2c_sensors_code(void *);

#endif