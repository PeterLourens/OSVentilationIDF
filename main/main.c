#include "main.h"

#define PROJECT_LOG_LEVEL ESP_LOG_ERROR

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
    esp_log_level_set("esp_littlefs", PROJECT_LOG_LEVEL); // Suppress LittleFS logs*/
    // esp_log_level_set("esp_netif_lwip", PROJECT_LOG_LEVEL); // Suppress LittleFS logs

    esp_log_level_set("*", PROJECT_LOG_LEVEL); // Suppress LittleFS logs

    // Create mutex for WiFi configuration, must be before loading config and wifi task start
    network_config_mutex = xSemaphoreCreateMutex();
    if (network_config_mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create WiFi config mutex");
        // return ESP_ERR_NO_MEM;
    }

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

    // Load sensor configuration for bus 1
    err = load_sensor_config("bus1_sensor_config");
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to load bus 1 sensor configuration");
        return;
    }

    // Load network (wifi) configuration
    err = load_network_config();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to load network settings");
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(200)); // Small delay to ensure config is settled

    // Start tasks
    start_task_wifi();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start Wifi task: %s", esp_err_to_name(err));
    }

    // Start tasks
    err = start_task_i2c_sensors();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start I2C sensors task: %s", esp_err_to_name(err));
    }

    // start_task_ds3231();

    // Print network settings
    ESP_LOGI(TAG, "\nNetwork Settings:\n"
                  "┌──────────────┬────────────────────────────────┐\n"
                  "│ Setting      │ Value                          │\n"
                  "├──────────────┼────────────────────────────────┤\n"
                  "│ DHCP         │ %-30s │\n"
                  "│ SSID         │ %-30s │\n"
                  "| Wifi Pass.   | %-30s │\n"
                  "│ IP Address   │ %-30s │\n"
                  "│ Subnet Mask  │ %-30s │\n"
                  "│ Gateway      │ %-30s │\n"
                  "│ Primary DNS  │ %-30s │\n"
                  "│ Secondary DNS│ %-30s │\n"
                  "└──────────────┴────────────────────────────────┘",
             network_config.enable_dhcp, network_config.ssid, network_config.wifi_password, network_config.ip_address,
             network_config.subnet_mask, network_config.gateway, network_config.primary_dns, network_config.secondary_dns);

    // Read from queue and log
    double snapshot[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS];

    while (1)
    {
        if (sensor_queue != NULL)
        {
            if (xQueuePeek(sensor_queue, &snapshot, portMAX_DELAY))
            {
                for (int bus = 0; bus < MAX_I2C_BUSES; bus++)
                {
                    for (int ch = 0; ch < CHANNELS_PER_BUS; ch++)
                    {
                        if (snapshot[bus][ch][IDX_VALID])
                        {
                            ESP_LOGI("CONSUMER",
                                     "Bus %d, CH%d @ %.0f µs: Temp=%.2f °C, RH=%.2f %%, CO₂=%.0f ppm",
                                     bus, ch,
                                     snapshot[bus][ch][IDX_TIMESTAMP],
                                     snapshot[bus][ch][IDX_TEMP],
                                     snapshot[bus][ch][IDX_HUMIDITY],
                                     snapshot[bus][ch][IDX_CO2]);
                        }
                    }
                }
            }
        }
        else
        {
            ESP_LOGE(TAG, "Sensor queue is NULL");
        }

        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}
