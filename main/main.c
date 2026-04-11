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

    syslog_send_line("Boot complete, system running");
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    // Log the offending task name and handle before aborting
    ESP_LOGE(TAG, "Stack overflow in task '%s' (handle=%p). Halting for postmortem.", pcTaskName ? pcTaskName : "<unknown>", (void *)xTask);

    // Optionally yield a short time to flush logs
    vTaskDelay(pdMS_TO_TICKS(50));

    // Abort to produce the panic/backtrace (so you can decode addresses)
    abort();
}

void app_main(void)
{
    esp_err_t err = ESP_OK;

    // Initialize hardware capabilities based on hardware version defined in hardware_caps.c
    hw_init();

    // Initialize lifecycle mutex for Ethernet operations
    eth_lifecycle_init_once();

    // Set log levels for each TAG
    set_log_levels();

    // Install GPIO ISR service (required for W5500 interrupt pin, even if not used)
    ESP_ERROR_CHECK(gpio_install_isr_service(ESP_INTR_FLAG_IRAM));

    // Create mutexes
    err = create_mutexes();
    if (err != ESP_OK)
    {
        LOGE(TAG, "Failed to create mutexes");
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(1000));

    // Initialize NVS
    err = init_nvs();
    if (err != ESP_OK)
    {
        LOGE(TAG, "Failed to initialize NVS");
        return;
    }

    // Initialize LittleFS
    err = init_littlefs();
    if (err != ESP_OK)
    {
        LOGE(TAG, "Failed to initialize LittleFS");
        return;
    }

    // init I2C bus0 and bus1
    err = i2c_init_master0();
    if (err != ESP_OK)
    {
        LOGE(TAG, "I2C master bus0 initialization failed: %s", esp_err_to_name(err));
        return;
    }
    err = i2c_init_master1();
    if (err != ESP_OK)
    {
        LOGE(TAG, "I2C master bus1 initialization failed: %s", esp_err_to_name(err));
        return;
    }

    // Init LCD on bus1
    err = i2c_lcd_init(i2c_bus1_handle, &lcd_bus1_handle);
    if (err != ESP_OK)
    {
        LOGE(TAG, "Failed to initialize LCD on bus 1: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }
    else
    {
        LOGI(TAG, "LCD initialized on bus 1");
    }

    // Load sensor configuration for bus 0
    err = load_sensor_config("bus0_sensor_config");
    if (err != ESP_OK)
    {
        LOGE(TAG, "Failed to load bus 0 sensor configuration");
        return;
    }

    // Load sensor configuration for bus 1
    err = load_sensor_config("bus1_sensor_config");
    if (err != ESP_OK)
    {
        LOGE(TAG, "Failed to load bus 1 sensor configuration");
        return;
    }

    // Load wifi configuration
    err = load_wifi_config();
    if (err != ESP_OK)
    {
        LOGE(TAG, "Failed to load wifi configuration");
        return;
    }

    // Load ethernet configuration
    err = load_ethernet_config();
    if (err != ESP_OK)
    {
        LOGE(TAG, "Failed to load ethernet configuration");
        return;
    }

    // Load time configuration
    err = load_time_config();
    if (err != ESP_OK)
    {
        LOGE(TAG, "Failed to load time configuration");
        return;
    }

    // set timezone after reading time config
    init_timezone();

    // Load MQTT configuration
    err = load_mqtt_config();
    if (err != ESP_OK)
    {
        LOGE(TAG, "Failed to load MQTT configuration");
        return;
    }

    // Load InfluxDB configuration
    err = load_influxdb_config();
    if (err != ESP_OK)
    {
        LOGE(TAG, "Failed to load InfluxDB configuration");
        return;
    }

    // Load fan control configuration
    err = load_fan_config();
    if (err != ESP_OK)
    {
        LOGE(TAG, "Failed to load fan control configuration");
        return;
    }

    // Load I2C control configuration
    err = load_i2c_config();
    if (err != ESP_OK)
    {
        LOGE(TAG, "Failed to load I2C configuration");
        return;
    }

    // Load syslog control configuration
    err = load_syslog_config();
    if (err != ESP_OK)
    {
        LOGE(TAG, "Failed to load syslog configuration");
        return;
    }

    // Load statemachine general configuration
    err = load_statemachine_general_config();
    if (err != ESP_OK)
    {
        LOGE(TAG, "Failed to load statemachine general configuration");
        return;
    }

    // Load state configuration
    err = load_state_config();
    if (err != ESP_OK)
    {
        LOGE(TAG, "Failed to load states configuration");
        return;
    }

    // Read valve positions from NVS into valvecontrol struct
    read_valve_pos_in_valvecontrol();

    // Iinit influxdb sender task and queue
    if (!influxdb_init_sender())
    {
        LOGW("main", "InfluxDB sender init failed; Influx disabled for this boot");
    }

    // TCP/IP stack and default event loop (do once)
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Start tasks
    LOGI(TAG, "Starting system tasks...");
    syslog_queue_init();     // must be before network services to capture logs from startup
    network_manager_start(); // starts network manager task which starts Wi‑Fi and Ethernet as needed
    start_task_i2c_ds3231();
    start_task_i2c_sensors();
    start_task_i2c_lcd();
    start_task_rgb_leds();
    start_task_valvecontrol();
    start_task_pushbutton();

    // Start network services in a separate task so we can retry if no network at startup
    start_network_services();

    // Initialize sensors based on current configuration
    reload_and_init_sensors();

    esp_log_set_vprintf(uart_log_output);
    LOGI(TAG, "System startup, logs go to UART");

    while (1)
    {
        char uptime_str[64] = {};

        formatted_uptime(uptime_str, sizeof(uptime_str));
        LOGI(TAG, "System uptime: %s", uptime_str);

        // sys_stats_update_actuals();
        update_hostname();
        LOGI(TAG, "Updated system stats");

        // Ensure connected and have IP before pinging (check esp_netif_get_ip_info)
        if (!wifi_has_ip())
        {
            if (ping_gateway())
            {
                LOGI(TAG, "gateway OK");
            }
            else
            {
                LOGW(TAG, "gateway unreachable — consider reconnecting Wi‑Fi");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(600000));
    }
}
