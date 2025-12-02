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

// Mutex handles
SemaphoreHandle_t network_config_mutex = NULL;
SemaphoreHandle_t time_config_mutex = NULL;
SemaphoreHandle_t mqtt_config_mutex = NULL;
SemaphoreHandle_t g_ping_done_sem = NULL;
SemaphoreHandle_t time_mutex = NULL;
SemaphoreHandle_t lock_valve_move_mutex = NULL;
SemaphoreHandle_t sensor_cfg_mutex = NULL;

// Event group handles
EventGroupHandle_t s_wifi_event_group = NULL;

// Sensor configuration structure
bus_sensor_t bus0_sensor_settings[CHANNELS_PER_BUS];
bus_sensor_t bus1_sensor_settings[CHANNELS_PER_BUS];

// Queue handles
QueueHandle_t sensor_queue;

// RGB LED handle
led_strip_handle_t rgb_leds = NULL;

// HTTP server handle
httpd_handle_t http_server = NULL;

// time structure
struct tm timeinfo = {0};

// Structs for settings
Network_config network_config = {0};
Time_settings time_config = {0};
MQTT_settings mqtt_config = {0};
ping_summary_t g_ping_summary = {0};

// Valvecontrol variables
int lock_valve_move = 0;
