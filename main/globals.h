#ifndef GLOBALS_H
#define GLOBALS_H

#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <freertos/queue.h>
#include <esp_log.h>
#include <esp_err.h>
#include <esp_timer.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_mac.h>
#include <driver/i2c_master.h>
#include <time.h>
#include <lwip/err.h>
#include <lwip/sys.h>

// I2C General configuration
#define I2C_MASTER_FREQ_HZ 100000

// I2C Bus0 configuration
#define I2C_MASTER_BUS0_SDA_IO 39
#define I2C_MASTER_BUS0_SCL_IO 38

// I2C Bus1 configuration
#define I2C_MASTER_BUS1_SDA_IO 41
#define I2C_MASTER_BUS1_SCL_IO 40

// I2C Device Addresses
#define TCA9548_I2C_ADDR 0x70 // Default address
#define AHT20_I2C_ADDR 0x38   // Default address
#define SCD41_I2C_ADDR 0x62   // Default address
#define DS3231_I2C_ADDR 0x68  // Default address

#define SMALL_CONFIG_ITEM 16
#define MEDIUM_CONFIG_ITEM 32
#define LARGE_CONFIG_ITEM 64
#define XLARGE_CONFIG_ITEM 128
#define XXLARGE_CONFIG_ITEM 1024
#define XXXLARGE_CONFIG_ITEM 2048

#define MAX_I2C_BUSES 2
#define CHANNELS_PER_BUS 8
#define DATA_FIELDS 5 // 0=temp, 1=humidity, 2=CO2, 3=timestamp, 4=valid
#define IDX_TEMP 0
#define IDX_HUMIDITY 1
#define IDX_CO2 2
#define IDX_TIMESTAMP 3
#define IDX_VALID 4

// I2C Devices new driver style
extern i2c_master_bus_handle_t i2c_bus0_handle;
extern i2c_master_bus_handle_t i2c_bus1_handle;
extern i2c_master_dev_handle_t tca9548_bus0_handle;
extern i2c_master_dev_handle_t tca9548_bus1_handle;
extern i2c_master_dev_handle_t aht20_bus0_handle;
extern i2c_master_dev_handle_t aht20_bus1_handle;
extern i2c_master_dev_handle_t scd41_bus0_handle;
extern i2c_master_dev_handle_t scd41_bus1_handle;
extern i2c_master_dev_handle_t ds3231_bus1_handle;

// Task handles
extern TaskHandle_t task_i2c_sensors;
extern TaskHandle_t task_i2c_ds3231;
extern TaskHandle_t task_wifi;

// Sensor readings queue
extern QueueHandle_t sensor_queue;

// Sensor configuration structure
typedef struct
{
    char type[SMALL_CONFIG_ITEM];
    char valve[MEDIUM_CONFIG_ITEM];
    char location[LARGE_CONFIG_ITEM];
    bool rh_enabled;
    bool co2_enabled;
} bus_sensor_t;

extern bus_sensor_t bus0_sensor_settings[];
extern bus_sensor_t bus1_sensor_settings[];

extern struct tm timeinfo;

typedef struct
{
    char enable_dhcp[SMALL_CONFIG_ITEM];
    char ssid[SMALL_CONFIG_ITEM];
    char wifi_password[XLARGE_CONFIG_ITEM]; // WPA3 has max 63 characters
    char ip_address[LARGE_CONFIG_ITEM];
    char subnet_mask[LARGE_CONFIG_ITEM];
    char gateway[LARGE_CONFIG_ITEM];
    char primary_dns[LARGE_CONFIG_ITEM];
    char secondary_dns[LARGE_CONFIG_ITEM];
} Network_settings;

extern Network_settings networksettings;

#endif