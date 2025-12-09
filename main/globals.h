#ifndef GLOBALS_H
#define GLOBALS_H

#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "esp_rom_sys.h"
#include "esp_sntp.h"
#include "esp_ping.h"
#include "esp_netif_sntp.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_cpu.h"
#include "esp_event.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "time.h"
#include "lwip/err.h"
#include "lwip/inet.h"
#include "lwip/sys.h"
#include "lwip/ip_addr.h"
#include "ping/ping_sock.h"
#include "arpa/inet.h"
#include "sys/socket.h"
#include "wear_levelling.h" // Wear leveling support (for flash-based FATFS)
#include "nvs_flash.h"      // NVS initialization
#include "nvs.h"            // NVS read/write APIs
#include "cJSON.h"          // JSON parsing
#include "dirent.h"         // Directory operations
#include "sys/stat.h"       // File status
#include "esp_littlefs.h"   // Mounting littlefs
#include "led_strip.h"
#include "esp_http_server.h"

#ifdef CONFIG_SPIRAM_ALLOW_NESTED_RAM_ALLOC
#include "esp_psram.h"
#endif

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
#define DHT20_I2C_ADDR 0x38   // Default address
#define SCD41_I2C_ADDR 0x62   // Default address
#define DS3231_I2C_ADDR 0x68  // Default address
#define LCD_I2C_ADDR 0x27     // Default address

#define SMALL_CONFIG_ITEM 16
#define MEDIUM_CONFIG_ITEM 32
#define LARGE_CONFIG_ITEM 64
#define XLARGE_CONFIG_ITEM 512
#define XXLARGE_CONFIG_ITEM 1024
#define XXXLARGE_CONFIG_ITEM 2048

// I2C Sensor config
#define MAX_I2C_BUSES 2
#define CHANNELS_PER_BUS 8
#define DATA_FIELDS 5 // 0=temp, 1=humidity, 2=CO2, 3=timestamp, 4=valid
#define IDX_TEMP 0
#define IDX_HUMIDITY 1
#define IDX_CO2 2
#define IDX_TIMESTAMP 3
#define IDX_VALID 4

// Valves config
#define MAX_VALVE_COUNT 12

// WiFi event bits
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define AP_STARTED_BIT BIT2

// statemachine settings
#define MAX_NR_STATES 12

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
extern i2c_master_dev_handle_t ds3231_bus0_handle;
extern i2c_master_dev_handle_t lcd_bus1_handle;

// Task handles
extern TaskHandle_t task_i2c_sensors;
extern TaskHandle_t task_i2c_ds3231;
extern TaskHandle_t task_wifi;
extern TaskHandle_t task_i2c_lcd;
extern TaskHandle_t task_rgb_leds;

// Sensor readings queue
extern QueueHandle_t sensor_queue;

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
extern SemaphoreHandle_t statemachine_config_mutex;

// Event group handles
extern EventGroupHandle_t s_wifi_event_group;

// RGB LED handle
extern led_strip_handle_t rgb_leds;

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
    char enable_dhcp[SMALL_CONFIG_ITEM];
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
    char fan_control_mode[SMALL_CONFIG_ITEM];
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
    char wkend_d1[SMALL_CONFIG_ITEM]; // Weekend day 1 (0=Sunday, 6=Saturday)
    char wkend_d2[SMALL_CONFIG_ITEM]; // Weekend day 2 (0=Sunday, 6=Saturday)

} statemachine_gen_config_t;

typedef struct
{
    bool enable_state;
    int fanspeed;                        // 0 is low, 1 is medium, 2 is high
    char state_name[MEDIUM_CONFIG_ITEM]; // Max 32 characters
    int rh_low_level;                    // Lower RH threshold for this state
    int rh_high_level;                   // Upper RH threshold for this state
    int co2_low_level;                   // Lower CO2 threshold for this state
    int co2_high_level;                  // Upper CO2 threshold for this state
    int state_start_hour;                // Hour to start this state
    int state_start_minute;              // Minute to start this state
    int state_end_hour;                  // Hour to end this state
    int state_end_minute;                // Minute to end this state
    int duration_minutes;                // Minimum duration to stay in this state
    int valve_position[MAX_VALVE_COUNT]; // Valve position array for this state
} statemachine_config_t;

extern bus_sensor_t bus0_sensor_settings[CHANNELS_PER_BUS];
extern bus_sensor_t bus1_sensor_settings[CHANNELS_PER_BUS];
extern Network_config_t network_config;
extern Time_config_t time_config;
extern MQTT_config_t mqtt_config;
extern Influxdb_config_t influxdb_config;
extern Fan_config_t fan_config;
extern I2C_config_t i2c_config;
extern Syslog_config_t syslog_config;
extern ping_summary_t g_ping_summary;
extern statemachine_gen_config_t statemachine_gen_config;
extern statemachine_config_t statemachine_config[MAX_NR_STATES];

extern struct tm timeinfo;




// Valvecontrol variables
extern int lock_valve_move;

#endif