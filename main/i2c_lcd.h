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

// Function definitions
esp_err_t i2c_lcd_init(i2c_master_bus_handle_t, i2c_master_dev_handle_t *);
esp_err_t i2c_lcd_init_device(void);
esp_err_t lcd_dev_write_byte(uint8_t);
esp_err_t lcd_pulse_enable(uint8_t);
esp_err_t lcd_write_nibble(uint8_t, uint8_t);
esp_err_t i2c_lcd_clear(void);
esp_err_t i2c_lcd_set_cursor(int, int);
esp_err_t i2c_lcd_write_text(const char *);
esp_err_t lcd_write_backlight_state(void);
esp_err_t i2c_lcd_backlight_set(bool on);
bool i2c_lcd_backlight_get(void);
void format_sensor_overview(char *, size_t, int, int, float avg[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS]);
void build_valve_positions_buffer(char *, size_t);
void build_sm_actuals_buffer(char *, size_t);

#endif