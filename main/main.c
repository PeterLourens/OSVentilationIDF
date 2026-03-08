/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include "main.h"

static const char *TAG = "main";

static void start_network_services(void)
{
    while (http_server_start() != ESP_OK)
    {
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    start_task_statemachine();
    start_task_mqtt();
    start_task_influxdb();
    // Start MQTT, InfluxDB, etc. AFTER HTTP server

    syslog_send_line("Boot complete, system running");
}

void app_main(void)
{
    esp_err_t err = ESP_OK;

    // Set log levels for each TAG
    set_log_levels();

    // Install GPIO ISR service (required for W5500 interrupt pin, even if not used)
    ESP_ERROR_CHECK(gpio_install_isr_service(ESP_INTR_FLAG_IRAM));

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

    // Load wifi configuration
    err = load_wifi_config();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to load wifi configuration");
        return;
    }

    // Load ethernet configuration
    err = load_ethernet_config();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to load ethernet configuration");
        return;
    }

    // Load time configuration
    err = load_time_config();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to load time configuration");
        return;
    }

    // set timezone after reading time config
    init_timezone();

    // Load MQTT configuration
    err = load_mqtt_config();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to load MQTT configuration");
        return;
    }

    // Load InfluxDB configuration
    err = load_influxdb_config();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to load InfluxDB configuration");
        return;
    }

    // Load fan control configuration
    err = load_fan_config();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to load fan control configuration");
        return;
    }

    // Load I2C control configuration
    err = load_i2c_config();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to load I2C configuration");
        return;
    }

    // Load syslog control configuration
    err = load_syslog_config();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to load syslog configuration");
        return;
    }

    // Load statemachine general configuration
    err = load_statemachine_general_config();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to load statemachine general configuration");
        return;
    }

    // Load state configuration
    err = load_state_config();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to load states configuration");
        return;
    }

    // Read valve positions from NVS into valvecontrol struct
    read_valve_pos_in_valvecontrol();

    // Iinit influxdb sender task and queue
    if (!influxdb_init_sender())
    {
        ESP_LOGW("main", "InfluxDB sender init failed; Influx disabled for this boot");
    }

    // TCP/IP stack and default event loop (do once)
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Start tasks
    ESP_LOGI(TAG, "Starting system tasks...");
    // start_task_wifi();
    // start_task_eth();
    network_manager_start(); // starts network manager task which starts Wi‑Fi and Ethernet as needed
    start_task_i2c_ds3231();
    start_task_i2c_sensors();
    start_task_i2c_lcd();
    start_task_rgb_leds();
    start_task_valvecontrol();

    // Start network services in a separate task so we can retry if no network at startup
    start_network_services();

    // Initialize sensors based on current configuration
    reload_and_init_sensors();

    esp_log_set_vprintf(uart_log_output);
    ESP_LOGI(TAG, "System startup, logs go to UART");

    while (1)
    {
        char uptime_str[64] = {};

        formatted_uptime(uptime_str, sizeof(uptime_str));
        ESP_LOGI(TAG, "System uptime: %s", uptime_str);

        // sys_stats_update_actuals();
        update_hostname();
        ESP_LOGI(TAG, "Updated system stats");

        // Ensure connected and have IP before pinging (check esp_netif_get_ip_info)
        if (!wifi_has_ip())
        {
            if (ping_gateway())
            {
                ESP_LOGI(TAG, "gateway OK");
            }
            else
            {
                ESP_LOGW(TAG, "gateway unreachable — consider reconnecting Wi‑Fi");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(600000));
    }
}
