/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

/**
 * @file main.c
 * @brief Firmware startup and runtime initialization for the OSVentilation system.
 */

#include "main.h"
#include "cJSON.h"

static const char *TAG = "main";

/**
 * @brief Allocate cJSON heap memory from SPIRAM if available.
 *
 * @param size Number of bytes to allocate.
 * @return Pointer to allocated memory, or NULL on failure.
 */
static void *cjson_spiram_malloc(size_t size)
{
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

/**
 * @brief Free memory allocated by cjson_spiram_malloc.
 *
 * @param ptr Pointer returned from cjson_spiram_malloc.
 */
static void cjson_spiram_free(void *ptr)
{
    if (ptr)
    {
        heap_caps_free(ptr);
    }
}

/**
 * @brief Initialize cJSON allocation hooks to use SPIRAM.
 */
static void init_cjson_hooks(void)
{
    cJSON_Hooks hooks = {
        .malloc_fn = cjson_spiram_malloc,
        .free_fn = cjson_spiram_free,
    };
    cJSON_InitHooks(&hooks);
}

/**
 * @brief Start HTTP and network-related runtime tasks.
 */
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

/**
 * @brief FreeRTOS stack overflow hook implementation.
 *
 * Logs the offending task and then halts to preserve a stack trace.
 *
 * @param xTask Handle of the task that overflowed.
 * @param pcTaskName Name of the task that overflowed.
 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;

    // Avoid re-entering the logging subsystem from inside the overflow hook.
    // The hardware will abort immediately so the crash dump can be captured.
    abort();
}

/**
 * @brief Main application entry point.
 *
 * Performs hardware initialization, loads configuration, and starts runtime tasks.
 */
void app_main(void)
{
    esp_reset_reason_t reason = esp_reset_reason();
    ESP_LOGI("boot", "Reset reason: %d", reason);
    ESP_LOGI("boot", "Printing saved backtrace (if any):");
    // esp_backtrace_print(ESP_LOG_INFO);

    // dump task list and free heap
    char tbuf[1024];
    vTaskList(tbuf);
    ESP_LOGI("boot", "Task list:\n%s", tbuf);
    ESP_LOGI("boot", "Free heap: %u", esp_get_free_heap_size());

    UBaseType_t high = uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGI("boot", "Main task stack high water mark: %u", (unsigned)high);

    esp_err_t err = ESP_OK;

    // Initialize hardware capabilities based on hardware version defined in hardware_caps.c
    hw_init();

    // Reduce cJSON internal heap usage by directing cJSON allocations into SPIRAM where available.
    init_cjson_hooks();

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

    // Start UART sender task early so we can log from the start (including hw_init)
    if (strcmp(syslog_config.enable_uart, "On") == 0)
    {
        start_task_uart_sender();
    }
    else
    {
        LOGI(TAG, "UART syslog forwarding disabled by configuration");
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
    if (strcmp(syslog_config.enable_uart, "On") == 0)
    {
        start_task_uart_sender();
    }
    else
    {
        LOGI(TAG, "UART syslog forwarding disabled by configuration");
    }
    start_task_i2c_ds3231();
    start_task_i2c_sensors();
    start_task_i2c_lcd();
    start_task_rgb_leds();
    start_task_valvecontrol();
    start_task_pushbutton();
    start_task_sys_stats();
    // ws_broadcaster_init();
    // stats_worker_init();

    // Start network services in a separate task so we can retry if no network at startup
    start_network_services();

    // Initialize sensors based on current configuration
    reload_and_init_sensors();

    esp_log_set_vprintf(uart_log_vprintf);
    LOGI(TAG, "System startup, log output is now routed through uart_log_vprintf");

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
