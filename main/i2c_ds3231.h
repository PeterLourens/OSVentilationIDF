#ifndef I2C_DS3231_H
#define I2C_DS3231_H

#define DS3231_REG_TIME 0x00
#define DS3231_REG_STATUS 0x0F
#define DS3231_REG_TEMP_MSB 0x11
#define DS3231_REG_TEMP_LSB 0x12

#include <stdio.h>

#include "globals.h"

// Function declarations
esp_err_t i2c_ds3231_init(i2c_master_bus_handle_t bus_handle, i2c_master_dev_handle_t *dev_handle);
esp_err_t i2c_ds3231_read_time(i2c_master_dev_handle_t dev_handle, struct tm *timeinfo);
void init_system_time_from_ds3231(i2c_master_dev_handle_t dev_handle);
esp_err_t update_ds3231_from_system_time(void);
esp_err_t sync_system_and_ds3231_with_ntp(i2c_master_dev_handle_t dev_handle);
uint8_t bin2bcd(uint8_t val);
esp_err_t i2c_ds3231_write_time(i2c_master_dev_handle_t dev_handle, const struct tm *timeinfo);
bool wifi_has_ip(void);
void time_sync_cb(struct timeval *tv);
void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data);
void ds3231_sync_task(void *pvParameters);
esp_err_t ds3231_read_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *val);
esp_err_t ds3231_write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val);
esp_err_t ds3231_read_temperature(i2c_master_dev_handle_t dev, float *temp_c);
//esp_err_t ds3231_check_status(i2c_master_dev_handle_t dev);
esp_err_t ds3231_check_status(i2c_master_dev_handle_t, char *, size_t);
time_t tm_to_epoch_utc(const struct tm *tm);
esp_err_t ds3231_drift_check(i2c_master_dev_handle_t dev);
void ds3231_health_monitor(i2c_master_dev_handle_t dev);
void save_ds3231_diagnostics(double drift_sec, float temp_c);
void read_ds3231_diagnostics(void);

#endif