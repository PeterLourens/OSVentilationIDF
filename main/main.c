#include "main.h"

#define PROJECT_LOG_LEVEL ESP_LOG_INFO

static const char *TAG = "main";

void app_main(void)
{
    esp_err_t err = ESP_OK;

    // Set log levels
    // sp_log_level_set("i2cdev", PROJECT_LOG_LEVEL);
    // esp_log_level_set("i2c.master", PROJECT_LOG_LEVEL);
    esp_log_level_set("scd4x", ESP_LOG_ERROR);
    esp_log_level_set("tca9548", ESP_LOG_ERROR);
    esp_log_level_set("aht", ESP_LOG_ERROR);
    esp_log_level_set("esp_littlefs", ESP_LOG_ERROR);
    esp_log_level_set("nvs", ESP_LOG_ERROR);
    esp_log_level_set("wifi", ESP_LOG_ERROR);
    esp_log_level_set("esp_netif_lwip", ESP_LOG_ERROR);
    esp_log_level_set("event", ESP_LOG_ERROR);
    esp_log_level_set("wifi_init_default", ESP_LOG_ERROR);
    esp_log_level_set("esp_adapter", ESP_LOG_ERROR);
    // esp_log_level_set("*", ESP_LOG_ERROR);

    // Create mutexes
    err = create_mutexes();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to create mutexes");
        return;
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
        return;
    }
    err = i2c_init_master1();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "I2C master bus1 initialization failed: %s", esp_err_to_name(err));
        return;
    }

    // Init DS3231 and sync system time after that
    err = i2c_ds3231_init(i2c_bus0_handle, &ds3231_bus0_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize DS3231 on bus 0: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }
    else
    {
        ESP_LOGI(TAG, "DS3231 initialized on bus 0");
    }
    init_system_time_from_ds3231(ds3231_bus0_handle);

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
    // Load time configuration
    err = load_time_config();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to load time settings");
        return;
    }

    // Start tasks
    start_task_wifi();
    start_task_i2c_ds3231();
    start_task_i2c_sensors();

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
                  "| Time Server  │ %-30s │\n"
                  "| Timezone     │ %-30s │\n"
                  "└──────────────┴────────────────────────────────┘",
             network_config.enable_dhcp, network_config.ssid, network_config.wifi_password, network_config.ip_address,
             network_config.subnet_mask, network_config.gateway, network_config.primary_dns, network_config.secondary_dns,
             time_config.ntp_server, time_config.timezone);

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
                            ESP_LOGI(TAG, "Bus %d, CH%d @ %.0f µs: Temp=%.2f °C, RH=%.2f %%, CO₂=%.0f ppm", bus, ch,
                                     snapshot[bus][ch][IDX_TIMESTAMP], snapshot[bus][ch][IDX_TEMP], snapshot[bus][ch][IDX_HUMIDITY], snapshot[bus][ch][IDX_CO2]);
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
