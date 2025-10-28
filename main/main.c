#include "main.h"

#define PROJECT_LOG_LEVEL ESP_LOG_WARN

void app_main(void)
{
    esp_err_t err = ESP_OK;
    static const char *TAG = "main";

    // Set log levels
    /*esp_log_level_set("i2cdev", PROJECT_LOG_LEVEL);       // Suppress info/debug from i2cdev
    esp_log_level_set("i2c.master", PROJECT_LOG_LEVEL);   // Suppress low-level I2C driver logs
    esp_log_level_set("scd4x", PROJECT_LOG_LEVEL);        // Suppress SCD4X driver logs
    esp_log_level_set("tca9548", PROJECT_LOG_LEVEL);      // Suppress TCA9548 driver logs
    esp_log_level_set("aht", PROJECT_LOG_LEVEL);          // Suppress TCA9548 driver logs
    esp_log_level_set("esp_littlefs", PROJECT_LOG_LEVEL); // Suppress LittleFS logs
*/

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

    // Read from queue and log
    double snapshot[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS];

    /*while (1)
    {
        if (xQueuePeek(sensor_queue, &snapshot, portMAX_DELAY))
        {
            for (int bus = 0; bus < MAX_I2C_BUSES; bus++)
            {
                for (int ch = 0; ch < CHANNELS_PER_BUS; ch++)
                {
                    // if (snapshot[bus][ch][IDX_VALID])
                    //{
                    ESP_LOGI("CONSUMER",
                             "Bus %d, CH%d @ %.0f µs: Temp=%.2f °C, RH=%.2f %%, CO₂=%.0f ppm",
                             bus, ch,
                             snapshot[bus][ch][IDX_TIMESTAMP],
                             snapshot[bus][ch][IDX_TEMP],
                             snapshot[bus][ch][IDX_HUMIDITY],
                             snapshot[bus][ch][IDX_CO2]);
                    //}
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10000));
    }*/
}
