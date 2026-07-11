/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

/**
 * @file globals.h
 * @brief Shared global declarations for runtime handles, queues, and configuration.
 */

#ifndef GLOBALS_H
#define GLOBALS_H

#include <driver/i2c_master.h>
#include <esp_http_server.h>
#include <esp_netif_sntp.h>
#include <led_strip.h>

#include "hardware_caps.h"

// I2C General configuration
#define I2C_MASTER_FREQ_HZ 100000

// I2C Device Addresses
#define TCA9548_I2C_ADDR 0x70 // Default address
#define AHT20_I2C_ADDR 0x38   // Default address
#define DHT20_I2C_ADDR 0x38   // Default address
#define SCD41_I2C_ADDR 0x62   // Default address
#define BME680_I2C_ADDR 0x77  // Default address
#define DS3231_I2C_ADDR 0x68  // Default address
#define LCD_I2C_ADDR 0x27     // Default address

// SPI host (VSPI_HOST or HSPI_HOST)
#define W5500_SPI_HOST SPI2_HOST // or SPI3_HOST depending on your wiring

#define SMALL_CONFIG_ITEM 16
#define MEDIUM_CONFIG_ITEM 32
#define LARGE_CONFIG_ITEM 64
#define MLARGE_CONFIG_ITEM 256
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
#define MAX_STATE_CONFIG_FILES 10

// Fanspeed values
#define FANSPEED_ERROR 0
#define FANSPEED_LOW 1
#define FANSPEED_MEDIUM 2
#define FANSPEED_HIGH 3

// Number of samples for moving average calculation
#define SENSOR_AVG_SIZE (MAX_I2C_BUSES * CHANNELS_PER_BUS * 5) // 5 averaged fields

/**
 * @name Runtime state labels
 * @brief Global string labels representing statemachine states.
 * @{
 */
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
/** @} */

/**
 * @name Sensor queue handles
 * @brief Shared FreeRTOS queue handles used for sensor data.
 * @{
 */
// Queue handles
extern QueueHandle_t sensor_queue;
extern QueueHandle_t sensor_avg_queue;
extern size_t sensor_avg_item_size;
extern volatile bool sensors_queues_ready;
/** @} */

/**
 * @name I2C device handles
 * @brief Handles for I2C buses and attached peripheral devices.
 * @{
 */
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
/** @} */

/**
 * @name Task handles
 * @brief FreeRTOS task handles for firmware services.
 * @{
 */
// Task handles
extern TaskHandle_t task_i2c_sensors;
extern TaskHandle_t task_i2c_ds3231;
extern TaskHandle_t task_wifi;
extern TaskHandle_t task_ethernet;
extern TaskHandle_t task_i2c_lcd;
extern TaskHandle_t task_rgb_leds;
extern TaskHandle_t task_statemachine;
extern TaskHandle_t task_valvectrl;
extern TaskHandle_t task_mqtt;
extern TaskHandle_t task_influxdb;
extern TaskHandle_t network_manager_task_handle;
extern TaskHandle_t task_pushbutton;
extern TaskHandle_t task_sys_stats;
extern TaskHandle_t task_syslog;
extern TaskHandle_t task_uart_sender;
extern TaskHandle_t task_uart_tx_sender;
/** @} */

/**
 * @name Mutex handles
 * @brief Mutexes protecting shared runtime state and configuration.
 * @{
 */
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
/** @} */

/**
 * @name Event and server handles
 * @brief Global event group and HTTP server handles.
 * @{
 */
// Event group handles
extern EventGroupHandle_t s_wifi_event_group;

// HTTP server handle
extern httpd_handle_t http_server;
/** @} */

/**
 * @name Runtime configuration structures
 * @brief Global configuration and state structures loaded at runtime.
 * @{
 */

/**
 * @brief Sensor bus configuration for a single I2C-connected sensor.
 *
 * This structure stores the sensor type, the target valve mapping,
 * displayable location text, and whether humidity / CO2 measurements
 * are enabled for the sensor.
 */
typedef struct
{
    char type[SMALL_CONFIG_ITEM];     /**< Sensor type used for driver selection. */
    char valve[MEDIUM_CONFIG_ITEM];   /**< Valve identifier associated with this sensor. */
    char location[LARGE_CONFIG_ITEM]; /**< Human-readable location label for the sensor. */
    bool rh_enabled;                  /**< Enable relative humidity measurement. */
    bool co2_enabled;                 /**< Enable CO2 measurement for this sensor. */
} bus_sensor_t;

/**
 * @brief Wi-Fi configuration persisted in runtime JSON.
 */
typedef struct
{
    char enable_wifi[SMALL_CONFIG_ITEM];    /**< "true"/"false" or equivalent string to enable Wi-Fi. */
    char enable_dhcp[SMALL_CONFIG_ITEM];    /**< DHCP enable/disable string for Wi-Fi interface. */
    char hostname[MEDIUM_CONFIG_ITEM];      /**< Device hostname used on the network. */
    char ssid[MEDIUM_CONFIG_ITEM];          /**< Wi-Fi SSID to connect to. */
    char wifi_password[LARGE_CONFIG_ITEM];  /**< WPA passphrase; supports up to 63 characters. */
    char ip_address[MEDIUM_CONFIG_ITEM];    /**< Static IP address when DHCP is disabled. */
    char subnet_mask[MEDIUM_CONFIG_ITEM];   /**< Subnet mask for the Wi-Fi network. */
    char gateway[MEDIUM_CONFIG_ITEM];       /**< Default gateway for Wi-Fi. */
    char primary_dns[MEDIUM_CONFIG_ITEM];   /**< Primary DNS server address. */
    char secondary_dns[MEDIUM_CONFIG_ITEM]; /**< Secondary DNS server address. */
} Wifi_config_t;

/**
 * @brief Ethernet configuration persisted in runtime JSON.
 */
typedef struct
{
    char enable_ethernet[SMALL_CONFIG_ITEM]; /**< Enable/disable string for Ethernet. */
    char enable_dhcp[SMALL_CONFIG_ITEM];     /**< DHCP enable/disable string for Ethernet. */
    char ip_address[MEDIUM_CONFIG_ITEM];     /**< Static IP address for Ethernet. */
    char subnet_mask[MEDIUM_CONFIG_ITEM];    /**< Subnet mask for Ethernet. */
    char gateway[MEDIUM_CONFIG_ITEM];        /**< Default gateway for Ethernet. */
    char primary_dns[MEDIUM_CONFIG_ITEM];    /**< Primary DNS server address. */
    char secondary_dns[MEDIUM_CONFIG_ITEM];  /**< Secondary DNS server address. */
} Ethernet_config_t;

/**
 * @brief NTP and timezone settings persisted in runtime JSON.
 */
typedef struct
{
    char ntp_server[LARGE_CONFIG_ITEM]; /**< NTP server hostname or IP address. */
    char timezone[LARGE_CONFIG_ITEM];   /**< Timezone string used by SNTP. */
} Time_config_t;

/**
 * @brief MQTT connection and topic configuration persisted in JSON.
 */
typedef struct
{
    char enable_mqtt[SMALL_CONFIG_ITEM];      /**< Enable/disable string for MQTT. */
    char mqtt_server[LARGE_CONFIG_ITEM];      /**< MQTT broker hostname or IP. */
    char mqtt_port[SMALL_CONFIG_ITEM];        /**< MQTT broker port as string. */
    char mqtt_base_topic[XLARGE_CONFIG_ITEM]; /**< Base topic prefix for published/subscribed messages. */
    char uri[MLARGE_CONFIG_ITEM];             /**< MQTT URI, if used instead of host/port. */
    char username[MEDIUM_CONFIG_ITEM];        /**< Username for MQTT authentication. */
    char password[XLARGE_CONFIG_ITEM];        /**< Password or token for MQTT authentication. */
    int keepalive;                            /**< MQTT keepalive interval in seconds. */
    char will_topic[XLARGE_CONFIG_ITEM];      /**< Last will topic. */
    char will_message[XLARGE_CONFIG_ITEM];    /**< Last will message payload. */
    int will_qos;                             /**< Last will QoS level. */
    int will_retain;                          /**< Last will retain flag. */
} MQTT_config_t;

/**
 * @brief InfluxDB connection settings persisted in JSON.
 */
typedef struct
{
    char enable_influxdb[SMALL_CONFIG_ITEM]; /**< Enable/disable string for InfluxDB reporting. */
    char influxdb_url[XXLARGE_CONFIG_ITEM];  /**< InfluxDB endpoint URL. */
    char influxdb_org[LARGE_CONFIG_ITEM];    /**< InfluxDB organization name. */
    char influxdb_bucket[LARGE_CONFIG_ITEM]; /**< InfluxDB bucket name. */
    char influxdb_token[XLARGE_CONFIG_ITEM]; /**< Authentication token for InfluxDB. */
} Influxdb_config_t;

/**
 * @brief Fan control integration settings persisted in JSON.
 */
typedef struct
{
    char fan_control_mode[MEDIUM_CONFIG_ITEM];       /**< Fan control algorithm or mode name. */
    char fan_control_mqtt_server[LARGE_CONFIG_ITEM]; /**< MQTT broker for fan control commands. */
    char fan_control_mqtt_port[SMALL_CONFIG_ITEM];   /**< MQTT port used for fan control messages. */
    char fan_control_mqtt_topic[XLARGE_CONFIG_ITEM]; /**< MQTT topic for fan control commands. */
    char fan_control_url_hs[XLARGE_CONFIG_ITEM];     /**< HTTP endpoint for high-speed fan control. */
    char fan_control_url_ms[XLARGE_CONFIG_ITEM];     /**< HTTP endpoint for medium-speed fan control. */
    char fan_control_url_ls[XLARGE_CONFIG_ITEM];     /**< HTTP endpoint for low-speed fan control. */
} Fan_config_t;

/**
 * @brief I2C bus and peripheral enable/disable settings persisted in JSON.
 */
typedef struct
{
    char bus0_multiplexer_address[SMALL_CONFIG_ITEM]; /**< I2C address of the first TCA9548 multiplexer. */
    char bus1_multiplexer_address[SMALL_CONFIG_ITEM]; /**< I2C address of the second TCA9548 multiplexer. */
    char enable_lcd[SMALL_CONFIG_ITEM];               /**< Enable/disable string for the I2C LCD display. */
    // char lcd_i2c_address[SMALL_CONFIG_ITEM];
} I2C_config_t;

/**
 * @brief Syslog target settings persisted in JSON.
 */
typedef struct
{
    char enable_syslog[SMALL_CONFIG_ITEM];     /**< Enable/disable string for syslog forwarding. */
    char enable_uart[SMALL_CONFIG_ITEM];       /**< Enable/disable string for UART syslog forwarding. */
    char syslog_ip[MEDIUM_CONFIG_ITEM];        /**< Remote syslog server IP address or hostname. */
    char syslog_port[SMALL_CONFIG_ITEM];       /**< Remote syslog server port. */
    char syslog_hostname[MEDIUM_CONFIG_ITEM];  /**< Hostname reported in syslog messages. */
    char syslog_appname[MEDIUM_CONFIG_ITEM];   /**< Application name reported in syslog messages. */
    char syslog_device_id[MEDIUM_CONFIG_ITEM]; /**< Device identifier used in syslog payloads. */
} Syslog_config_t;

/**
 * @brief Summary statistics for ICMP ping health checks.
 */
typedef struct
{
    int transmitted;  /**< Number of ping packets transmitted. */
    int received;     /**< Number of ping replies received. */
    int loss_percent; /**< Packet loss percentage. */
    int avg_time_ms;  /**< Average round-trip time in milliseconds. */
    int duration_ms;  /**< Total duration of the ping operation in milliseconds. */
    bool success;     /**< True if ping sequence completed successfully. */
} ping_summary_t;

/**
 * @brief General statemachine parameters loaded from configuration.
 *
 * This struct contains schedule masks for weekday and weekend day/night
 * boundaries, weekend day labels, and global RH/CO2 threshold levels used
 * by the statemachine decision logic.
 */
typedef struct
{
    int wday_d_h_start;               /**< Weekday day-start hour bitmask. */
    int wday_d_m_start;               /**< Weekday day-start minute bitmask. */
    int wday_n_h_start;               /**< Weekday night-start hour bitmask. */
    int wday_n_m_start;               /**< Weekday night-start minute bitmask. */
    int wkend_d_h_start;              /**< Weekend day-start hour bitmask. */
    int wkend_d_m_start;              /**< Weekend day-start minute bitmask. */
    int wkend_n_h_start;              /**< Weekend night-start hour bitmask. */
    int wkend_n_m_start;              /**< Weekend night-start minute bitmask. */
    char wkend_d1[SMALL_CONFIG_ITEM]; /**< First weekend day label. */
    char wkend_d2[SMALL_CONFIG_ITEM]; /**< Second weekend day label. */
    int rh_low_level;                 /**< Lower relative humidity threshold. */
    int rh_high_level;                /**< Upper relative humidity threshold. */
    int co2_low_level;                /**< Lower CO2 threshold for control decisions. */
    int co2_high_level;               /**< Upper CO2 threshold for control decisions. */

} Statemachine_gen_config_t;

/**
 * @brief Individual statemachine state configuration.
 */
typedef struct
{
    bool enable_state;                   /**< Whether this state is active in the runtime schedule. */
    int fanspeed;                        /**< Fan speed for this state: 0=low, 1=medium, 2=high. */
    char state_name[MEDIUM_CONFIG_ITEM]; /**< Name of the state, used in UI and logs. */
    int state_start_hour;                /**< State start hour (0-23). */
    int state_start_minute;              /**< State start minute (0-59). */
    int state_end_hour;                  /**< State end hour (0-23). */
    int state_end_minute;                /**< State end minute (0-59). */
    int duration_minutes;                /**< Minimum duration to remain in this state. */
    int cycling_interval_min;            /**< Valve cycling interval in minutes for cycling states. */
    int valve_position[MAX_VALVE_COUNT]; /**< Valve positions for each supported valve. */
} Statemachine_config_t;

/**
 * @brief Runtime actuals and telemetry tracked by the statemachine.
 *
 * This struct collects the current fanspeed, active state, sensor alert counts,
 * network connectivity status, system diagnostics, and per-task stack usage.
 */
typedef struct
{
    int fanspeed;         /**< Current fan speed value. */
    int state;            /**< Current statemachine state index. */
    int co2_sensors_high; /**< Number of CO2 sensors reporting high values. */
    int rh_sensors_high;  /**< Number of RH sensors reporting high values. */

    bool valve_lock; /**< True when valve movement is locked. */

    bool wifi_error;     /**< True if Wi-Fi has reported an error. */
    bool wifi_connected; /**< True when Wi-Fi is connected. */
    bool wifi_ap_mode;   /**< True when device is in AP mode. */

    int wifi_rssi;                       /**< Wi-Fi RSSI in dBm. */
    int wifi_channel;                    /**< Wi-Fi channel in use. */
    int wifi_authmode;                   /**< Wi-Fi authentication mode. */
    int wifi_phy;                        /**< Wi-Fi PHY mode. */
    int wifi_bandwidth;                  /**< Wi-Fi bandwidth. */
    char wifi_bssid[MEDIUM_CONFIG_ITEM]; /**< Wi-Fi BSSID in MAC text form. */
    char wifi_ssid[LARGE_CONFIG_ITEM];   /**< Connected SSID. */
    char hostname[MEDIUM_CONFIG_ITEM];   /**< Local device hostname. */

    char wifi_ip_addr[SMALL_CONFIG_ITEM]; /**< Wi-Fi IP address. */
    char wifi_netmask[SMALL_CONFIG_ITEM]; /**< Wi-Fi netmask. */
    char wifi_gateway[SMALL_CONFIG_ITEM]; /**< Wi-Fi gateway. */
    char wifi_dns1[SMALL_CONFIG_ITEM];    /**< Wi-Fi primary DNS. */
    char wifi_dns2[SMALL_CONFIG_ITEM];    /**< Wi-Fi secondary DNS. */
    bool wifi_dhcp_enabled;               /**< True when Wi-Fi uses DHCP. */
    bool wifi_enabled;                    /**< True when Wi-Fi subsystem is enabled. */

    char eth_ip_addr[SMALL_CONFIG_ITEM]; /**< Ethernet IP address. */
    char eth_netmask[SMALL_CONFIG_ITEM]; /**< Ethernet netmask. */
    char eth_gateway[SMALL_CONFIG_ITEM]; /**< Ethernet gateway. */
    char eth_dns1[SMALL_CONFIG_ITEM];    /**< Ethernet primary DNS. */
    char eth_dns2[SMALL_CONFIG_ITEM];    /**< Ethernet secondary DNS. */
    bool eth_dhcp_enabled;               /**< True when Ethernet uses DHCP. */
    bool ethernet_enabled;               /**< True when Ethernet subsystem is enabled. */
    bool eth_connected;                  /**< True when Ethernet link is established. */
    bool eth_link_up;                    /**< True when Ethernet PHY link is up. */

    int cpu_temp; /**< CPU temperature reading. */

    size_t heap_free;     /**< Current free heap in bytes. */
    size_t heap_min_ever; /**< Minimum heap ever available. */

    uint32_t stack_i2c_sensors;     /**< Stack high-water mark for i2c_sensors task. */
    uint32_t stack_i2c_ds3231;      /**< Stack high-water mark for ds3231 task. */
    uint32_t stack_i2c_lcd;         /**< Stack high-water mark for lcd task. */
    uint32_t stack_rgb_leds;        /**< Stack high-water mark for RGB LED task. */
    uint32_t stack_statemachine;    /**< Stack high-water mark for statemachine task. */
    uint32_t stack_valvectrl;       /**< Stack high-water mark for valve control task. */
    uint32_t stack_mqtt;            /**< Stack high-water mark for MQTT task. */
    uint32_t stack_influxdb;        /**< Stack high-water mark for InfluxDB task. */
    uint32_t stack_network_manager; /**< Stack high-water mark for network manager task. */
    uint32_t stack_pushbutton;      /**< Stack high-water mark for pushbutton task. */
    uint32_t stack_sys_stats;       /**< Stack high-water mark for system stats task. */
    uint32_t stack_syslog;          /**< Stack high-water mark for syslog task. */
    uint32_t stack_uart_sender;     /**< Stack high-water mark for UART sender task. */

} Statemachine_actuals_t;

/**
 * @brief Shared valve control data passed between web handlers and the statemachine.
 */
typedef struct
{
    int valve_number[MAX_VALVE_COUNT];          /**< Requested valve numbers from the web UI. */
    int position_change[MAX_VALVE_COUNT];       /**< Requested position changes for each valve. */
    int direction[MAX_VALVE_COUNT];             /**< Direction of valve movement for each valve. */
    int actual_valve_position[MAX_VALVE_COUNT]; /**< Current physical valve position values. */
    bool check_position;                        /**< Request to verify valve positions. */
    bool write_new_position;                    /**< Request to write new valve positions. */
    bool valve_move_request_from_post;          /**< True when a POST request initiated valve movement. */
} Valve_control_data;

extern bus_sensor_t bus0_sensor_settings[CHANNELS_PER_BUS];
extern bus_sensor_t bus1_sensor_settings[CHANNELS_PER_BUS];
extern Wifi_config_t wifi_config;
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

/** @} */

#endif