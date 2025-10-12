#include "main.h"

#define PROJECT_LOG_LEVEL ESP_LOG_NONE

void app_main(void)
{
    esp_log_level_set("i2cdev", PROJECT_LOG_LEVEL);     // Suppress info/debug from i2cdev
    esp_log_level_set("i2c.master", PROJECT_LOG_LEVEL); // Suppress low-level I2C driver logs
    esp_log_level_set("scd4x", PROJECT_LOG_LEVEL);      // Suppress SCD4X driver logs
    esp_log_level_set("tca9548", PROJECT_LOG_LEVEL);    // Suppress TCA9548 driver logs
    esp_log_level_set("aht", PROJECT_LOG_LEVEL);        // Suppress TCA9548 driver logs

    // Initialize NVS

    // Start tasks
    start_task_i2c();
}
