#include "main.h"

#define PROJECT_LOG_LEVEL ESP_LOG_INFO

static const char *TAG = "main";

void app_main(void)
{
    // Sensor data array to contain data from queue
    // double snapshot[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS];

    esp_err_t err = ESP_OK;

    set_log_levels();

    // Create mutexes
    err = create_mutexes();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to create mutexes");
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(1000));

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

    // Init LCD on bus1
    err = i2c_lcd_init(i2c_bus1_handle, &lcd_bus1_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize LCD on bus 1: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }
    else
    {
        ESP_LOGI(TAG, "LCD initialized on bus 1");
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

    // Load time configuration
    err = load_time_config();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to load time settings");
        return;
    }

    // Load time configuration
    err = load_mqtt_config();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to load MQTT settings");
        return;
    }

    // Start tasks
    ESP_LOGI(TAG, "Starting system tasks...");
    start_task_wifi();
    start_task_i2c_ds3231();
    start_task_i2c_sensors();
    start_task_i2c_lcd();
    start_task_rgb_leds();

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

    /* Bring up Wi‑Fi and wait for IP before sending any stored crash record */
    if (!network_wait_for_ip(30, 1000, "WIFI_STA_DEF"))
    {
        ESP_LOGE(TAG, "Network not available after retries");
    }
    else
    {
        ESP_LOGI(TAG, "Wi-Fi init done, starting webserver...");
        syslog_send_line("Boot complete, system running");
        start_webserver();
    }

    esp_log_set_vprintf(uart_log_output);
    ESP_LOGI(TAG, "System startup, logs go to UART");

    while (1)
    {
        /*if (sensor_queue != NULL)
        {
            if (xQueuePeek(sensor_queue, &snapshot, portMAX_DELAY))
            {
                for (int bus = 0; bus < MAX_I2C_BUSES; bus++)
                {
                    for (int ch = 0; ch < CHANNELS_PER_BUS; ch++)
                    {
                        if (snapshot[bus][ch][IDX_VALID])
                        {
                            ESP_LOGI(TAG, "Bus %d, CH%d: Temp=%.2f °C, RH=%.2f %%, CO₂=%.0f ppm", bus, ch, snapshot[bus][ch][IDX_TEMP], snapshot[bus][ch][IDX_HUMIDITY], snapshot[bus][ch][IDX_CO2]);
                        }
                    }
                }
            }
        }
        else
        {
            ESP_LOGE(TAG, "Sensor queue is NULL");
        }*/

        // show_system_and_task_stats();

        ESP_LOGI(TAG, "Running valvecontrol");
        valvecontrol(1, 12, 0, 13, 11, 12);
        // valvecontrol(1, 4, 0, 14, 15, 16);

        char uptime_str[64] = {};
        formatted_uptime(uptime_str, sizeof(uptime_str));
        ESP_LOGI(TAG, "System uptime: %s", uptime_str);

        // Ensure connected and have IP before pinging (check esp_netif_get_ip_info)
        if (!wifi_has_ip())
        {
            if (ping_gateway())
            {
                ESP_LOGI("watchdog", "gateway OK");
            }
            else
            {
                ESP_LOGW("watchdog", "gateway unreachable — consider reconnecting Wi‑Fi");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(120000));
    }
}
