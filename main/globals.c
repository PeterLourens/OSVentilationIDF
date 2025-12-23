#include "globals.h"

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
i2c_master_dev_handle_t ds3231_bus0_handle = {0};
i2c_master_dev_handle_t lcd_bus1_handle = {0};

// Task handles
TaskHandle_t task_i2c_sensors = NULL;
TaskHandle_t task_i2c_ds3231 = NULL;
TaskHandle_t task_wifi = NULL;
TaskHandle_t task_i2c_lcd = NULL;
TaskHandle_t task_rgb_leds = NULL;
TaskHandle_t task_statemachine = NULL;

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

// Event group handles
EventGroupHandle_t s_wifi_event_group = NULL;

// Sensor configuration structure
bus_sensor_t bus0_sensor_settings[CHANNELS_PER_BUS] = {0};
bus_sensor_t bus1_sensor_settings[CHANNELS_PER_BUS] = {0};

// Queue handles
QueueHandle_t sensor_queue;
QueueHandle_t sensor_avg_queue;

// RGB LED handle
led_strip_handle_t rgb_leds = NULL;

// HTTP server handle
httpd_handle_t http_server = NULL;

// time structure
struct tm timeinfo = {0};

// Structs for settings
Network_config_t network_config = {0};
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

// Valvecontrol variables
int lock_valve_move = 0;
