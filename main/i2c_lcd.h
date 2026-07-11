/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#ifndef I2C_LCD_H
#define I2C_LCD_H

#include <driver/i2c_master.h>

#include "general.h"
#include "wifi_functions.h"
#include "logging.h"

/**
 * @file i2c_lcd.h
 * @brief Public LCD control interface for the I2C PCF8574 expander and HD44780 display.
 */

/**
 * @brief Add and bind the LCD expander device to the specified I2C bus.
 *
 * @param bus_handle I2C bus handle.
 * @param dev_handle Pointer to the device handle to populate.
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t i2c_lcd_init(i2c_master_bus_handle_t, i2c_master_dev_handle_t *);

/**
 * @brief Initialize the attached LCD device and configure display mode.
 *
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t i2c_lcd_init_device(void);

/**
 * @brief Write a raw byte to the PCF8574 expander.
 *
 * @param data Raw output byte.
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t lcd_dev_write_byte(uint8_t);

/**
 * @brief Pulse the LCD enable signal through the expander.
 *
 * @param data Control and backlight flags to transmit.
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t lcd_pulse_enable(uint8_t);

/**
 * @brief Write a 4-bit nibble to the LCD controller.
 *
 * @param nibble Nibble to send.
 * @param control_flags RS/RW/backlight flags.
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t lcd_write_nibble(uint8_t, uint8_t);

/**
 * @brief Clear the LCD display.
 *
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t i2c_lcd_clear(void);

/**
 * @brief Position the LCD cursor.
 *
 * @param row Zero-based row index.
 * @param col Zero-based column index.
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t i2c_lcd_set_cursor(int, int);

/**
 * @brief Write a null-terminated string to the LCD.
 *
 * @param text Text to display.
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t i2c_lcd_write_text(const char *);

/**
 * @brief Apply the current backlight state to the expander.
 *
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t lcd_write_backlight_state(void);

/**
 * @brief Enable or disable the LCD backlight.
 *
 * @param on true to enable the backlight.
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t i2c_lcd_backlight_set(bool on);

/**
 * @brief Query the current cached backlight state.
 *
 * @return true if backlight is enabled, false otherwise.
 */
bool i2c_lcd_backlight_get(void);

/**
 * @brief Format a sensor overview string for display.
 *
 * @param out Output buffer.
 * @param out_size Size of the output buffer.
 * @param bus Sensor bus index.
 * @param ch Sensor channel index.
 * @param avg Average sensor readings array.
 */
void format_sensor_overview(char *, size_t, int, int, float avg[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS]);

/**
 * @brief Build formatted valve position text for LCD output.
 *
 * @param buf Output buffer.
 * @param buf_size Output buffer size.
 */
void build_valve_positions_buffer(char *, size_t);

/**
 * @brief Build formatted statemachine actuals text for LCD output.
 *
 * @param buf Output buffer.
 * @param buf_size Output buffer size.
 */
void build_sm_actuals_buffer(char *, size_t);

#endif