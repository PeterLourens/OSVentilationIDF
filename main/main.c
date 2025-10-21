#include "main.h"

#define PROJECT_LOG_LEVEL ESP_LOG_NONE

void app_main(void)
{
    esp_err_t err = ESP_OK;
    static const char *TAG = "main";

    // Set log levels
    esp_log_level_set("i2cdev", PROJECT_LOG_LEVEL);       // Suppress info/debug from i2cdev
    esp_log_level_set("i2c.master", PROJECT_LOG_LEVEL);   // Suppress low-level I2C driver logs
    esp_log_level_set("scd4x", PROJECT_LOG_LEVEL);        // Suppress SCD4X driver logs
    esp_log_level_set("tca9548", PROJECT_LOG_LEVEL);      // Suppress TCA9548 driver logs
    esp_log_level_set("aht", PROJECT_LOG_LEVEL);          // Suppress TCA9548 driver logs
    esp_log_level_set("esp_littlefs", PROJECT_LOG_LEVEL); // Suppress LittleFS logs

    // Initialize NVS
    err = init_nvs();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize NVS");
        return;
    }

    // Initialize LittleFS
    err = init_littlefs();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize LittleFS");
        return;
    }

    // init I2C bus0 and bus1
    err = i2c_init_master0();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "I2C master bus0 initialization failed: %s", esp_err_to_name(err));
    }
    err = i2c_init_master1();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "I2C master bus1 initialization failed: %s", esp_err_to_name(err));
    }

    // Load sensor configuration for bus 0
    err = load_sensor_config("bus0_sensor_config");
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to load bus 0 sensor configuration");
        return;
    }

    err = load_sensor_config("bus1_sensor_config");
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to load bus 1 sensor configuration");
        return;
    }

    // Start tasks
    start_task_i2c_sensors();
}
