/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include "globals.h"

const char *STATE_ERROR = "error";                // state_nr = 0
const char *STATE_INIT = "init";                  // state_nr = 1
const char *STATE_DAY = "day";                    // state_nr = 2
const char *STATE_NIGHT = "night";                // state_nr = 3
const char *STATE_HIGHCO2DAY = "highco2day";      // state_nr = 4
const char *STATE_HIGHCO2NIGHT = "highco2night";  // state_nr = 5
const char *STATE_HIGHRHDAY = "highrhday";        // state_nr = 6
const char *STATE_HIGHRHNIGHT = "highrhnight";    // state_nr = 7
const char *STATE_COOKING = "cooking";            // state_nr = 8
const char *STATE_CYCLINGDAY = "cyclingday";      // state_nr = 9
const char *STATE_CYCLINGNIGHT = "cyclingnight";  // state_nr = 10
const char *STATE_MANUALHS = "manual_high_speed"; // state_nr = 11
const char *STATE_STOPPED = "stopped";            // state_nr = 12
const char *STATE_TEMP = "temp";                  // state_nr = 13

// I2C Devices new driver style
i2c_master_bus_handle_t i2c_bus0_handle = {0};
i2c_master_bus_handle_t i2c_bus1_handle = {0};
i2c_master_dev_handle_t tca9548_bus0_handle = {0};
i2c_master_dev_handle_t tca9548_bus1_handle = {0};
i2c_master_dev_handle_t aht20_bus0_handle = {0};
i2c_master_dev_handle_t aht20_bus1_handle = {0};
i2c_master_dev_handle_t dht20_bus0_handle = {0};
i2c_master_dev_handle_t dht20_bus1_handle = {0};
i2c_master_dev_handle_t scd41_bus0_handle = {0};
i2c_master_dev_handle_t scd41_bus1_handle = {0};
i2c_master_dev_handle_t bme680_bus0_handle = {0};
i2c_master_dev_handle_t bme680_bus1_handle = {0};
i2c_master_dev_handle_t ds3231_bus0_handle = {0};
i2c_master_dev_handle_t lcd_bus1_handle = {0};

// Task handles
TaskHandle_t task_i2c_sensors = NULL;
TaskHandle_t task_i2c_ds3231 = NULL;
TaskHandle_t task_wifi = NULL;
TaskHandle_t task_ethernet = NULL;
TaskHandle_t task_i2c_lcd = NULL;
TaskHandle_t task_rgb_leds = NULL;
TaskHandle_t task_statemachine = NULL;
TaskHandle_t task_valvectrl = NULL;
TaskHandle_t task_mqtt = NULL;
TaskHandle_t task_influxdb = NULL;

// Mutex handles
SemaphoreHandle_t network_config_mutex = NULL;
SemaphoreHandle_t time_config_mutex = NULL;
SemaphoreHandle_t mqtt_config_mutex = NULL;
SemaphoreHandle_t influxdb_config_mutex = NULL;
SemaphoreHandle_t fan_config_mutex = NULL;
SemaphoreHandle_t i2c_config_mutex = NULL;
SemaphoreHandle_t syslog_config_mutex = NULL;
SemaphoreHandle_t g_ping_done_sem = NULL;
SemaphoreHandle_t time_mutex = NULL;
SemaphoreHandle_t lock_valve_move_mutex = NULL;
SemaphoreHandle_t sensor_cfg_mutex = NULL;
SemaphoreHandle_t state_config_mutex = NULL;
SemaphoreHandle_t statemachine_mutex = NULL;
SemaphoreHandle_t valve_control_data_mutex = NULL;
SemaphoreHandle_t i2c_bus_mutex = NULL;

// Event group handles
EventGroupHandle_t s_wifi_event_group = NULL;

// Sensor configuration structure
bus_sensor_t bus0_sensor_settings[CHANNELS_PER_BUS] = {0};
bus_sensor_t bus1_sensor_settings[CHANNELS_PER_BUS] = {0};

// Queue handles
QueueHandle_t sensor_queue;
QueueHandle_t sensor_avg_queue;

// HTTP server handle
httpd_handle_t http_server = NULL;

// time structure
struct tm timeinfo = {0};

// Structs for settings
Wifi_config_t wifi_config = {0};
Ethernet_config_t ethernet_config = {0};
Time_config_t time_config = {0};
MQTT_config_t mqtt_config = {0};
Influxdb_config_t influxdb_config = {0};
Fan_config_t fan_config = {0};
I2C_config_t i2c_config = {0};
Syslog_config_t syslog_config = {0};
ping_summary_t g_ping_summary = {0};

// Statemachine struct
Statemachine_gen_config_t statemachine_gen_config = {0};
Statemachine_config_t statemachine_config[MAX_NR_STATES] = {0};
Statemachine_actuals_t sm_actuals = {0};

// Valve control structs
Valve_control_data valvecontroldata = {0};

// Valvecontrol variables
int lock_valve_move = 0;
