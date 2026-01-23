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

// Function declarations
esp_err_t i2c_ds3231_init(i2c_master_bus_handle_t, i2c_master_dev_handle_t *);
esp_err_t update_ds3231_from_system_time(void);
esp_err_t sync_system_and_ds3231_with_ntp(i2c_master_dev_handle_t);
uint8_t bin2bcd(uint8_t);
esp_err_t i2c_ds3231_write_time(i2c_master_dev_handle_t, const struct tm *);
esp_err_t ds3231_read_reg(i2c_master_dev_handle_t, uint8_t, uint8_t *);
esp_err_t ds3231_write_reg(i2c_master_dev_handle_t, uint8_t, uint8_t);
esp_err_t ds3231_read_temperature(i2c_master_dev_handle_t, float *);
esp_err_t ds3231_check_status(i2c_master_dev_handle_t, char *, size_t);
esp_err_t ds3231_set_time(i2c_master_dev_handle_t, const struct tm *);
esp_err_t ds3231_get_time(i2c_master_dev_handle_t, struct tm *);

void init_system_time_from_ds3231(i2c_master_dev_handle_t);
void time_sync_cb(struct timeval *);
void ds3231_health_monitor(i2c_master_dev_handle_t);
void save_ds3231_diagnostics(float);
void read_ds3231_diagnostics(void);

#endif