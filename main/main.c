#include "main.h"

#define PROJECT_LOG_LEVEL ESP_LOG_NONE

static const char *TAG = "main";

void app_main(void)
{
    esp_err_t err = ESP_OK;

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

    // Load sensor configuration for bus 0
    err = load_sensor_config("bus0_sensor_config");
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to load bus 0 sensor configuration");
        return;
    }

    // Start tasks
    // start_task_i2c();
}
