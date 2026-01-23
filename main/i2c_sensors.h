#ifndef I2C_SENSORS_H
#define I2C_SENSORS_H

#include <driver/i2c_master.h>
#include <esp_log.h>

#include "globals.h"
#include "logging.h"

// Function declarations

// TCA9548A functions
esp_err_t i2c_init_tca9548(i2c_master_bus_handle_t, i2c_master_dev_handle_t *);
esp_err_t i2c_tca9548_set_channel(i2c_master_dev_handle_t, uint8_t);

// AHT20 functions
esp_err_t i2c_aht20_init(i2c_master_bus_handle_t, i2c_master_dev_handle_t *);
esp_err_t aht20_read(i2c_master_dev_handle_t, float *, float *);

// DHT20 functions
esp_err_t i2c_dht20_init(i2c_master_bus_handle_t, i2c_master_dev_handle_t *);
esp_err_t dht20_read(i2c_master_dev_handle_t, float *, float *);

// SCD41 functions
esp_err_t i2c_init_scd41(i2c_master_bus_handle_t, i2c_master_dev_handle_t *);
uint8_t scd41_crc8(const uint8_t *, size_t);
esp_err_t scd41_data_ready(i2c_master_dev_handle_t, bool *);
esp_err_t scd41_read(i2c_master_dev_handle_t, uint16_t *, float *, float *);

// sensor_type_t sensor_type_from_str(const char *type_str);

// Functions to init and read sensors on mux channels based on config struct
bool is_scd4x(const char *);
bool is_aht20(const char *);
bool is_dht20(const char *);
esp_err_t init_sensors_on_mux_channels(void);
esp_err_t read_sensors_into_queue(void);
void reload_and_init_sensors(void);

void preload_sensor_types_from_config(void);
void sensor_data_average(void);
void fifoPush(int, int, int, float, int);

#endif