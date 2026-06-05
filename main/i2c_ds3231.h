/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

/**
 * @file i2c_ds3231.h
 * @brief DS3231 RTC I2C support interfaces and helpers.
 */

#ifndef I2C_DS3231_H
#define I2C_DS3231_H

#define DS3231_REG_TIME 0x00
#define DS3231_REG_STATUS 0x0F
#define DS3231_REG_TEMP_MSB 0x11
#define DS3231_REG_TEMP_LSB 0x12

#include <driver/i2c_master.h>
#include <esp_log.h>
#include <esp_sntp.h>
#include <esp_wifi.h>
#include <nvs_flash.h>

#include "general.h"
#include "logging.h"

/**
 * @brief Initialize DS3231 device on the provided I2C bus.
 */
esp_err_t i2c_ds3231_init(i2c_master_bus_handle_t, i2c_master_dev_handle_t *);

/**
 * @brief Initialize system time from the DS3231 RTC.
 */
void init_system_time_from_ds3231(i2c_master_dev_handle_t);

/**
 * @brief Update DS3231 from current system time.
 */
esp_err_t update_ds3231_from_system_time(void);

/**
 * @brief Synchronize system clock and DS3231 with NTP and optionally update the RTC.
 */
esp_err_t sync_system_and_ds3231_with_ntp(i2c_master_dev_handle_t);

/**
 * @brief Convert a binary value to BCD.
 */
uint8_t bin2bcd(uint8_t);

/**
 * @brief Set the DS3231 time from a struct tm.
 */
esp_err_t ds3231_set_time(i2c_master_dev_handle_t, const struct tm *);

/**
 * @brief Read the DS3231 time into a struct tm.
 */
esp_err_t ds3231_get_time(i2c_master_dev_handle_t, struct tm *);

/**
 * @brief Read a single DS3231 register.
 */
esp_err_t ds3231_read_reg(i2c_master_dev_handle_t, uint8_t, uint8_t *);

/**
 * @brief Write a single DS3231 register.
 */
esp_err_t ds3231_write_reg(i2c_master_dev_handle_t, uint8_t, uint8_t);

/**
 * @brief Read temperature in degrees Celsius from the DS3231.
 */
esp_err_t ds3231_read_temperature(i2c_master_dev_handle_t, float *);

/**
 * @brief Read the DS3231 status register and format diagnostics.
 */
esp_err_t ds3231_check_status(i2c_master_dev_handle_t, char *, size_t);

/**
 * @brief Perform periodic DS3231 health monitoring.
 */
void ds3231_health_monitor(i2c_master_dev_handle_t);

/**
 * @brief Save DS3231 diagnostics to NVS.
 */
void save_ds3231_diagnostics(float);

/**
 * @brief Read DS3231 diagnostics from NVS.
 */
void read_ds3231_diagnostics(void);

#endif