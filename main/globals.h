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
#define XLARGE_CONFIG_ITEM 128
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
extern SemaphoreHandle_t g_ping_done_sem;
extern SemaphoreHandle_t time_mutex;
extern SemaphoreHandle_t lock_valve_move_mutex;
extern SemaphoreHandle_t sensor_cfg_mutex;

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

extern bus_sensor_t bus0_sensor_settings[];
extern bus_sensor_t bus1_sensor_settings[];

extern struct tm timeinfo;

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
} Network_config;
extern Network_config network_config;

typedef struct
{
    char ntp_server[LARGE_CONFIG_ITEM];
    char timezone[LARGE_CONFIG_ITEM];
} Time_settings;
extern Time_settings time_config;

typedef struct
{
    char enable_mqtt[LARGE_CONFIG_ITEM];
    char mqtt_server[LARGE_CONFIG_ITEM];
    char mqtt_port[SMALL_CONFIG_ITEM];
    char mqtt_base_topic[XLARGE_CONFIG_ITEM];
} MQTT_settings;
extern MQTT_settings mqtt_config;

typedef struct
{
    int transmitted;
    int received;
    int loss_percent;
    int avg_time_ms;
    int duration_ms;
    bool success;
} ping_summary_t;
extern ping_summary_t g_ping_summary;

// Valvecontrol variables
extern int lock_valve_move;

#endif