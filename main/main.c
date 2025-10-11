#include "main.h"

void app_main(void)
{
    esp_log_level_set("i2cdev", ESP_LOG_WARN);     // Suppress info/debug from i2cdev
    esp_log_level_set("i2c.master", ESP_LOG_WARN); // Suppress low-level I2C driver logs
    esp_log_level_set("scd4x", ESP_LOG_WARN);      // Suppress SCD4X driver logs
    esp_log_level_set("tca9548", ESP_LOG_WARN);    // Suppress SCD4X driver logs

    start_task_i2c();
}
