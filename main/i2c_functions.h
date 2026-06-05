/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

/**
 * @file i2c_functions.h
 * @brief Public declarations for I2C master bus initialization.
 */

#ifndef I2C_FUNCTIONS_H
#define I2C_FUNCTIONS_H

#include <driver/i2c_master.h>
#include <esp_log.h>

#include "logging.h"

/**
 * @brief Initialize I2C master bus 0 using the configured SDA/SCL pins.
 *
 * @return ESP_OK on success, or an esp_err_t error code on failure.
 */
esp_err_t i2c_init_master0(void);

/**
 * @brief Initialize I2C master bus 1 using the configured SDA/SCL pins.
 *
 * @return ESP_OK on success, or an esp_err_t error code on failure.
 */
esp_err_t i2c_init_master1(void);

#endif