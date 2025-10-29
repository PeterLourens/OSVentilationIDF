#include "globals.h"

// I2C Devices new driver style
i2c_master_bus_handle_t i2c_bus0_handle = {0};
i2c_master_bus_handle_t i2c_bus1_handle = {0};
i2c_master_dev_handle_t tca9548_bus0_handle = {0};
i2c_master_dev_handle_t tca9548_bus1_handle = {0};
i2c_master_dev_handle_t aht20_bus0_handle = {0};
i2c_master_dev_handle_t aht20_bus1_handle = {0};
i2c_master_dev_handle_t scd41_bus0_handle = {0};
i2c_master_dev_handle_t scd41_bus1_handle = {0};

// Task handles
TaskHandle_t task_i2c_sensors = NULL;
TaskHandle_t task_i2c_ds3231 = NULL;
TaskHandle_t task_wifi = NULL;

// Sensor configuration structure
bus_sensor_t bus0_sensor_settings[CHANNELS_PER_BUS];
bus_sensor_t bus1_sensor_settings[CHANNELS_PER_BUS];

// Queue handles
QueueHandle_t sensor_queue;

struct tm timeinfo = {0};

Network_settings networksettings;
