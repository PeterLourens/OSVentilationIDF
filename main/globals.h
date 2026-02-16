/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#ifndef GLOBALS_H
#define GLOBALS_H

#include <driver/i2c_master.h>
#include <esp_http_server.h>
#include <esp_netif_sntp.h>
#include <led_strip.h>

// I2C General configuration
#define I2C_MASTER_FREQ_HZ 100000

// I2C Bus0 configuration
#define I2C_MASTER_BUS0_SDA_IO 18
#define I2C_MASTER_BUS0_SCL_IO 17

// I2C Bus1 configuration
#define I2C_MASTER_BUS1_SDA_IO 5
#define I2C_MASTER_BUS1_SCL_IO 4

// I2C Device Addresses
#define TCA9548_I2C_ADDR 0x70 // Default address
#define AHT20_I2C_ADDR 0x38   // Default address
#define DHT20_I2C_ADDR 0x38   // Default address
#define SCD41_I2C_ADDR 0x62   // Default address
#define BME680_I2C_ADDR 0x77  // Default address
#define DS3231_I2C_ADDR 0x68  // Default address
#define LCD_I2C_ADDR 0x27     // Default address

// SPI configuration for W5500 Ethernet
#define W5500_MISO_GPIO 13
#define W5500_MOSI_GPIO 11
#define W5500_SCLK_GPIO 12
#define W5500_CS_GPIO 10

// Interrupt pin (optional)
#define W5500_INT_GPIO 9

// Reset pin (optional, -1 if unused)
#define W5500_RST_GPIO 8

// SPI host (VSPI_HOST or HSPI_HOST)
#define W5500_SPI_HOST SPI2_HOST // or SPI3_HOST depending on your wiring

// Define I/O for 74595 chips
#define CLOCKPIN1 1
#define LATCHPIN1 2
#define DATAPIN1 3

#define CLOCKPIN2 15
#define LATCHPIN2 16
#define DATAPIN2 14

#define SMALL_CONFIG_ITEM 16
#define MEDIUM_CONFIG_ITEM 32
#define LARGE_CONFIG_ITEM 64
#define XLARGE_CONFIG_ITEM 512
#define XXLARGE_CONFIG_ITEM 1024
#define XXXLARGE_CONFIG_ITEM 2048

// I2C Sensor config including average calculation
#define MAX_I2C_BUSES 2
#define CHANNELS_PER_BUS 8
#define DATA_FIELDS 7 // 0=temp, 1=humidity, 2=CO2, 3=timestamp, 4=valid, 5=pressure, 6=gas resistance
#define IDX_TEMP 0
#define IDX_HUMIDITY 1
#define IDX_CO2 2
#define IDX_TIMESTAMP 3
#define IDX_VALID 4
#define IDX_PRESSURE 5
#define IDX_GAS_RES 6
#define MAX_FIFO_SIZE 25

// Valves config
#define MAX_VALVE_COUNT 12 // Maximum number of valves supported by the hardware
#define MAX_SENSORS 16     // Maximum number of sensors supported by the hardware

// WiFi event bits
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define AP_STARTED_BIT BIT2
#define MAINTENANCE_AP_TIMEOUT_MS (10 * 60 * 1000) // 10 minutes

// statemachine settings
#define MAX_NR_STATES 15

// Fanspeed values
#define FANSPEED_ERROR 0
#define FANSPEED_LOW 1
#define FANSPEED_MEDIUM 2
#define FANSPEED_HIGH 3

// Number of samples for moving average calculation
// #define SENSOR_AVG_SIZE 48
#define SENSOR_AVG_SIZE (MAX_I2C_BUSES * CHANNELS_PER_BUS * 5) // 5 averaged fields

extern const char *STATE_ERROR;
extern const char *STATE_INIT;
extern const char *STATE_DAY;
extern const char *STATE_NIGHT;
extern const char *STATE_HIGHCO2DAY;
extern const char *STATE_HIGHCO2NIGHT;
extern const char *STATE_HIGHRHDAY;
extern const char *STATE_HIGHRHNIGHT;
extern const char *STATE_COOKING;
extern const char *STATE_CYCLINGDAY;
extern const char *STATE_CYCLINGNIGHT;
extern const char *STATE_MANUALHS;
extern const char *STATE_STOPPED;
extern const char *STATE_TEMP;

// I2C Devices new driver style
extern i2c_master_bus_handle_t i2c_bus0_handle;
extern i2c_master_bus_handle_t i2c_bus1_handle;
extern i2c_master_dev_handle_t tca9548_bus0_handle;
extern i2c_master_dev_handle_t tca9548_bus1_handle;
extern i2c_master_dev_handle_t aht20_bus0_handle;
extern i2c_master_dev_handle_t aht20_bus1_handle;
extern i2c_master_dev_handle_t dht20_bus0_handle;
extern i2c_master_dev_handle_t dht20_bus1_handle;
extern i2c_master_dev_handle_t scd41_bus0_handle;
extern i2c_master_dev_handle_t scd41_bus1_handle;
extern i2c_master_dev_handle_t bme680_bus0_handle;
extern i2c_master_dev_handle_t bme680_bus1_handle;
extern i2c_master_dev_handle_t ds3231_bus0_handle;
extern i2c_master_dev_handle_t lcd_bus1_handle;

// Task handles
extern TaskHandle_t task_i2c_sensors;
extern TaskHandle_t task_i2c_ds3231;
extern TaskHandle_t task_wifi;
extern TaskHandle_t task_i2c_lcd;
extern TaskHandle_t task_rgb_leds;
extern TaskHandle_t task_statemachine;
extern TaskHandle_t task_valvectrl;
extern TaskHandle_t task_mqtt;
extern TaskHandle_t task_influxdb;

// Sensor readings queue
extern QueueHandle_t sensor_queue;
extern QueueHandle_t sensor_avg_queue;

// Mutex handles
extern SemaphoreHandle_t network_config_mutex;
extern SemaphoreHandle_t time_config_mutex;
extern SemaphoreHandle_t mqtt_config_mutex;
extern SemaphoreHandle_t influxdb_config_mutex;
extern SemaphoreHandle_t fan_config_mutex;
extern SemaphoreHandle_t i2c_config_mutex;
extern SemaphoreHandle_t syslog_config_mutex;
extern SemaphoreHandle_t g_ping_done_sem;
extern SemaphoreHandle_t time_mutex;
extern SemaphoreHandle_t lock_valve_move_mutex;
extern SemaphoreHandle_t sensor_cfg_mutex;
extern SemaphoreHandle_t state_config_mutex;
extern SemaphoreHandle_t statemachine_mutex;
extern SemaphoreHandle_t valve_control_data_mutex;
extern SemaphoreHandle_t i2c_bus_mutex;

// Event group handles
extern EventGroupHandle_t s_wifi_event_group;

// HTTP server handle
extern httpd_handle_t http_server;

// Sensor configuration structure
typedef struct
{
    char type[SMALL_CONFIG_ITEM];
    char valve[MEDIUM_CONFIG_ITEM];
    char location[LARGE_CONFIG_ITEM];
    bool rh_enabled;
    bool co2_enabled;
} bus_sensor_t;

typedef struct
{
    char enable_wifi[SMALL_CONFIG_ITEM];
    char enable_dhcp[SMALL_CONFIG_ITEM];
    char hostname[MEDIUM_CONFIG_ITEM];
    char ssid[MEDIUM_CONFIG_ITEM];
    char wifi_password[LARGE_CONFIG_ITEM]; // WPA3 has max 63 characters
    char ip_address[MEDIUM_CONFIG_ITEM];
    char subnet_mask[MEDIUM_CONFIG_ITEM];
    char gateway[MEDIUM_CONFIG_ITEM];
    char primary_dns[MEDIUM_CONFIG_ITEM];
    char secondary_dns[MEDIUM_CONFIG_ITEM];
} Network_config_t;

typedef struct
{
    char enable_ethernet[SMALL_CONFIG_ITEM];
    char enable_dhcp[SMALL_CONFIG_ITEM];
    char ip_address[MEDIUM_CONFIG_ITEM];
    char subnet_mask[MEDIUM_CONFIG_ITEM];
    char gateway[MEDIUM_CONFIG_ITEM];
    char primary_dns[MEDIUM_CONFIG_ITEM];
    char secondary_dns[MEDIUM_CONFIG_ITEM];
} Ethernet_config_t;

typedef struct
{
    char ntp_server[LARGE_CONFIG_ITEM];
    char timezone[LARGE_CONFIG_ITEM];
} Time_config_t;

typedef struct
{
    char enable_mqtt[SMALL_CONFIG_ITEM];
    char mqtt_server[LARGE_CONFIG_ITEM];
    char mqtt_port[SMALL_CONFIG_ITEM];
    char mqtt_base_topic[XLARGE_CONFIG_ITEM];
} MQTT_config_t;

typedef struct
{
    char enable_influxdb[SMALL_CONFIG_ITEM];
    char influxdb_url[XXLARGE_CONFIG_ITEM];
    char influxdb_org[LARGE_CONFIG_ITEM];
    char influxdb_bucket[LARGE_CONFIG_ITEM];
    char influxdb_token[XLARGE_CONFIG_ITEM];
} Influxdb_config_t;

typedef struct
{
    char fan_control_mode[MEDIUM_CONFIG_ITEM];
    char fan_control_mqtt_server[LARGE_CONFIG_ITEM];
    char fan_control_mqtt_port[SMALL_CONFIG_ITEM];
    char fan_control_mqtt_topic[XLARGE_CONFIG_ITEM];
    char fan_control_url_hs[XLARGE_CONFIG_ITEM];
    char fan_control_url_ms[XLARGE_CONFIG_ITEM];
    char fan_control_url_ls[XLARGE_CONFIG_ITEM];
} Fan_config_t;

typedef struct
{
    char bus0_multiplexer_address[SMALL_CONFIG_ITEM];
    char bus1_multiplexer_address[SMALL_CONFIG_ITEM];
    char enable_lcd[SMALL_CONFIG_ITEM];
    // char lcd_i2c_address[SMALL_CONFIG_ITEM];
} I2C_config_t;

typedef struct
{
    char enable_syslog[SMALL_CONFIG_ITEM];
    char syslog_ip[MEDIUM_CONFIG_ITEM];
    char syslog_port[SMALL_CONFIG_ITEM];
    char syslog_hostname[MEDIUM_CONFIG_ITEM];
    char syslog_appname[MEDIUM_CONFIG_ITEM];
    char syslog_device_id[MEDIUM_CONFIG_ITEM];
} Syslog_config_t;

typedef struct
{
    int transmitted;
    int received;
    int loss_percent;
    int avg_time_ms;
    int duration_ms;
    bool success;
} ping_summary_t;

typedef struct
{
    int wday_d_h_start;               // Bitmask for weekday/day/hour start
    int wday_d_m_start;               // Bitmask for weekday/day/hour end
    int wday_n_h_start;               // Bitmask for weekday/day/hour start
    int wday_n_m_start;               // Bitmask for weekday/day/hour end
    int wkend_d_h_start;              // Bitmask for weekday/day/hour start
    int wkend_d_m_start;              // Bitmask for weekday/day/hour end
    int wkend_n_h_start;              // Bitmask for weekday/day/hour start
    int wkend_n_m_start;              // Bitmask for weekday/day/hour end
    char wkend_d1[SMALL_CONFIG_ITEM]; // Weekend day 1
    char wkend_d2[SMALL_CONFIG_ITEM]; // Weekend day 2
    int rh_low_level;                 // General lower RH threshold
    int rh_high_level;                // General upper RH threshold
    int co2_low_level;                // General lower CO2 threshold
    int co2_high_level;               // General upper CO2 threshold

} Statemachine_gen_config_t;

typedef struct
{
    bool enable_state;
    int fanspeed;                        // 0 is low, 1 is medium, 2 is high
    char state_name[MEDIUM_CONFIG_ITEM]; // Max 32 characters
    int state_start_hour;                // Hour to start this state
    int state_start_minute;              // Minute to start this state
    int state_end_hour;                  // Hour to end this state
    int state_end_minute;                // Minute to end this state
    int duration_minutes;                // Minimum duration to stay in this state
    int cycling_interval_min;            // Interval for cycling valves in minutes
    int valve_position[MAX_VALVE_COUNT]; // Valve position array for this state
} Statemachine_config_t;

typedef struct
{
    int fanspeed;
    int state;
    int co2_sensors_high;
    int rh_sensors_high;
    bool valve_lock;
    bool wifi_error;
    bool wifi_connected;
    bool wifi_ap_mode;

    // System stats
    int wifi_rssi;
    int wifi_channel;
    int wifi_authmode;
    int wifi_phy;
    int wifi_bandwidth;
    char wifi_bssid[MEDIUM_CONFIG_ITEM]; // "AA:BB:CC:DD:EE:FF"
    char wifi_ssid[LARGE_CONFIG_ITEM];
    char hostname[MEDIUM_CONFIG_ITEM];

    char ip_addr[SMALL_CONFIG_ITEM];
    char netmask[SMALL_CONFIG_ITEM];
    char gateway[SMALL_CONFIG_ITEM];
    char dns1[SMALL_CONFIG_ITEM];
    char dns2[SMALL_CONFIG_ITEM];
    bool dhcp_enabled;

    int cpu_temp;

    size_t heap_free;
    size_t heap_min_ever;

    uint32_t stack_i2c_sensors;
    uint32_t stack_i2c_ds3231;
    uint32_t stack_wifi;
    uint32_t stack_i2c_lcd;
    uint32_t stack_rgb_leds;
    uint32_t stack_statemachine;
    uint32_t stack_valvectrl;
    uint32_t stack_mqtt;
    uint32_t stack_influxdb;

} Statemachine_actuals_t;

// To store valve control data from web page and from statemachine and actual valvel data
typedef struct
{
    int valve_number[MAX_VALVE_COUNT];
    int position_change[MAX_VALVE_COUNT];
    int direction[MAX_VALVE_COUNT];
    int actual_valve_position[MAX_VALVE_COUNT];
    bool check_position;
    bool write_new_position;
    bool valve_move_request_from_post; /* POST handler requests movement */
} Valve_control_data;

extern bus_sensor_t bus0_sensor_settings[CHANNELS_PER_BUS];
extern bus_sensor_t bus1_sensor_settings[CHANNELS_PER_BUS];
extern Network_config_t network_config;
extern Ethernet_config_t ethernet_config;
extern Time_config_t time_config;
extern MQTT_config_t mqtt_config;
extern Influxdb_config_t influxdb_config;
extern Fan_config_t fan_config;
extern I2C_config_t i2c_config;
extern Syslog_config_t syslog_config;
extern ping_summary_t g_ping_summary;
extern Statemachine_gen_config_t statemachine_gen_config;
extern Statemachine_config_t statemachine_config[MAX_NR_STATES];
extern Statemachine_actuals_t sm_actuals;
extern Valve_control_data valvecontroldata;

extern struct tm timeinfo;

// Valvecontrol variables
extern int lock_valve_move;

#endif