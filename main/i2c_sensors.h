/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#ifndef I2C_SENSORS_H
#define I2C_SENSORS_H

#include <driver/i2c_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <string.h>
#include <esp_rom_sys.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>
#include <esp_attr.h>

#include "globals.h"
#include "logging.h"

// BME680 driver
#include "bme68x.h"
#include "bme68x_defs.h"

// BSEC includes
// #include "bsec_adapter.h" /* only if you need BSEC types here */
// #include "bme680_driver.h"
//  #include "bme680_test.h"

/**
 * @file i2c_sensors.h
 * @brief Public sensor management interface for the OSVentilation I2C sensor subsystem.
 */

// Function declarations

/**
 * @brief Initialize the TCA9548A multiplexer on the specified I2C bus.
 *
 * @param[in] bus_handle Handle to the I2C bus.
 * @param[out] dev_handle Pointer to receive the created device handle.
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t i2c_init_tca9548(i2c_master_bus_handle_t, i2c_master_dev_handle_t *);

/**
 * @brief Select a channel on the TCA9548A multiplexer.
 *
 * @param[in] dev_handle TCA9548A device handle.
 * @param[in] channel Channel index (0-7).
 * @return ESP_OK on success, or ESP_ERR_INVALID_ARG if arguments are invalid.
 */
esp_err_t i2c_tca9548_set_channel(i2c_master_dev_handle_t, uint8_t);

/**
 * @brief Initialize an AHT20 sensor device on the I2C bus.
 *
 * @param[in] bus_handle I2C bus handle.
 * @param[out] dev_handle Device handle pointer to populate.
 * @return ESP_OK on success, or an esp_err_t on failure.
 */
esp_err_t i2c_aht20_init(i2c_master_bus_handle_t, i2c_master_dev_handle_t *);

/**
 * @brief Read temperature and humidity from an AHT20 sensor.
 *
 * @param[in] dev_handle AHT20 device handle.
 * @param[out] temperature Measured temperature in degrees Celsius.
 * @param[out] humidity Measured relative humidity in percent.
 * @return ESP_OK on success, or an esp_err_t on failure.
 */
esp_err_t aht20_read(i2c_master_dev_handle_t, float *, float *);

/**
 * @brief Initialize a DHT20 sensor device on the I2C bus.
 *
 * @param[in] bus_handle I2C bus handle.
 * @param[out] dev_handle Device handle pointer to populate.
 * @return ESP_OK on success, or an esp_err_t on failure.
 */
esp_err_t i2c_dht20_init(i2c_master_bus_handle_t, i2c_master_dev_handle_t *);

/**
 * @brief Read temperature and humidity from a DHT20 sensor.
 *
 * @param[in] dev_handle DHT20 device handle.
 * @param[out] temperature Measured temperature in degrees Celsius.
 * @param[out] humidity Measured relative humidity in percent.
 * @return ESP_OK on success, or an esp_err_t on failure.
 */
esp_err_t dht20_read(i2c_master_dev_handle_t, float *, float *);

/**
 * @brief Initialize an SCD41 sensor device on the I2C bus.
 *
 * @param[in] bus_handle I2C bus handle.
 * @param[out] dev_handle Device handle pointer to populate.
 * @return ESP_OK on success, or an esp_err_t on failure.
 */
esp_err_t i2c_init_scd41(i2c_master_bus_handle_t, i2c_master_dev_handle_t *);

/**
 * @brief Compute SCD41 CRC8 for a buffer.
 *
 * @param[in] data Input data buffer.
 * @param[in] len Number of bytes to compute.
 * @return Computed CRC8 value.
 */
uint8_t scd41_crc8(const uint8_t *, size_t);

/**
 * @brief Check whether SCD41 sensor data is ready.
 *
 * @param[in] dev_handle SCD41 device handle.
 * @param[out] data_ready True if data is available.
 * @return ESP_OK on success, or an esp_err_t on failure.
 */
esp_err_t scd41_data_ready(i2c_master_dev_handle_t, bool *);

/**
 * @brief Read CO2, temperature, and humidity from an SCD41 sensor.
 *
 * @param[in] dev_handle SCD41 device handle.
 * @param[out] co2 Measured CO2 concentration in ppm.
 * @param[out] temp Measured temperature in degrees Celsius.
 * @param[out] rh Measured relative humidity in percent.
 * @return ESP_OK on success, or an esp_err_t on failure.
 */
esp_err_t scd41_read(i2c_master_dev_handle_t, uint16_t *, float *, float *);

/**
 * @brief Determine whether a sensor type string refers to an SCD4x sensor.
 *
 * @param[in] type Sensor type string.
 * @return true if the type matches an SCD4x sensor.
 */
bool is_scd4x(const char *);

/**
 * @brief Determine whether a sensor type string refers to an AHT20 sensor.
 *
 * @param[in] type Sensor type string.
 * @return true if the type matches AHT20.
 */
bool is_aht20(const char *);

/**
 * @brief Determine whether a sensor type string refers to a DHT20 sensor.
 *
 * @param[in] type Sensor type string.
 * @return true if the type matches DHT20.
 */
bool is_dht20(const char *);

/**
 * @brief Initialize all configured sensors on their respective I2C multiplexer channels.
 *
 * @return ESP_OK if all configured sensors initialize successfully, or an error code otherwise.
 */
esp_err_t init_sensors_on_mux_channels(void);

/**
 * @brief Check whether a sensor is configured for the specified bus and slot.
 *
 * @param[in] bus Bus index.
 * @param[in] slot Channel slot index.
 * @return true if a sensor is configured and initialized.
 */
bool configured_sensor(int, int);

/**
 * @brief Check whether the configured sensor supports CO2 measurement.
 *
 * @param[in] bus Bus index.
 * @param[in] slot Channel slot index.
 * @return true if the sensor supports CO2.
 */
bool sensor_supports_co2(int, int);

/**
 * @brief Read all configured sensors and publish the latest values into the sensor queue.
 *
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t read_sensors_into_queue(void);

/**
 * @brief Reload sensor configuration and initialize sensors based on the new settings.
 */
void reload_and_init_sensors(void);

/**
 * @brief Preload sensor type information from the configuration into local driver state.
 */
void preload_sensor_types_from_config(void);

/**
 * @brief Compute moving averages for sensor data and publish average results into the average queue.
 */
void sensor_data_average(void);

/**
 * @brief Push a new sensor measurement into the moving average FIFO.
 *
 * @param[in] bus I2C bus index.
 * @param[in] slot Sensor slot index.
 * @param[in] measurement Measurement index.
 * @param[in] value Measurement value.
 */
void fifoPush(int, int, int, float);

#endif