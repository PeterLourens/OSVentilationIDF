/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include "general.h"
#include "esp_log_level.h"

static const char *TAG = "General";

const char *state_nr_to_str(state_nr_t state)
{
    switch (state)
    {
    case STATE_NR_ERROR:
        return "ERROR";
    case STATE_NR_INIT:
        return "INIT";
    case STATE_NR_DAY:
        return "DAY";
    case STATE_NR_NIGHT:
        return "NIGHT";
    case STATE_NR_HIGHCO2DAY:
        return "HIGH_CO2_DAY";
    case STATE_NR_HIGHCO2NIGHT:
        return "HIGH_CO2_NIGHT";
    case STATE_NR_HIGHRHDAY:
        return "HIGH_RH_DAY";
    case STATE_NR_HIGHRHNIGHT:
        return "HIGH_RH_NIGHT";
    case STATE_NR_COOKING:
        return "COOKING";
    case STATE_NR_CYCLINGDAY:
        return "CYCLING_DAY";
    case STATE_NR_CYCLINGNIGHT:
        return "CYCLING_NIGHT";
    case STATE_NR_MANUALHS:
        return "MANUAL_HS";
    case STATE_NR_STOPPED:
        return "STOPPED";
    case STATE_NR_TEMP:
        return "TEMP";
    default:
        return "UNKNOWN_STATE";
    }
}

const char *fan_speed_to_str(fan_speed_t f)
{
    switch (f)
    {
    case FAN_SPEED_ERROR:
        return "ERROR";
    case FAN_SPEED_LOW:
        return "LOW";
    case FAN_SPEED_MEDIUM:
        return "MEDIUM";
    case FAN_SPEED_HIGH:
        return "HIGH";
    default:
        return "UNKNOWN_FAN_SPEED";
    }
}

esp_err_t init_nvs(void)
{
    esp_err_t err = ESP_OK;

    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(TAG, "NVS needs erase, retrying...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize NVS (%s)", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "NVS initialized");
    return ESP_OK;
}

esp_err_t init_littlefs(void)
{
    esp_err_t err = ESP_OK;

    const esp_vfs_littlefs_conf_t conf = {
        .base_path = "/data",
        .partition_label = "storage",
        .format_if_mount_failed = false,
        .dont_mount = false,
    };

    err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to mount LittleFS (%s)", esp_err_to_name(err));
        return err;
    }

    size_t total = 0;
    size_t used = 0;

    err = esp_littlefs_info(conf.partition_label, &total, &used);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get LittleFS partition info (%s)", esp_err_to_name(err));
        esp_vfs_littlefs_unregister(conf.partition_label);
        return err;
    }

    ESP_LOGI(TAG, "LittleFS mounted. Total: %d bytes, Used: %d bytes", total, used);

    DIR *dir = opendir("/data");
    if (!dir)
    {
        ESP_LOGE("LITTLEFS", "Failed to open /data");
        return ESP_FAIL;
    }

    struct dirent *entry;
    char full_path[270];
    struct stat st;

    while ((entry = readdir(dir)) != NULL)
    {
        snprintf(full_path, sizeof(full_path), "/data/%s", entry->d_name);

        if (stat(full_path, &st) == 0)
        {
            ESP_LOGI("LITTLEFS", "Found: %s (%ld bytes)", entry->d_name, st.st_size);
        }
        else
        {
            ESP_LOGW("LITTLEFS", "Found: %s (size unknown)", entry->d_name);
        }
    }

    closedir(dir);
    return ESP_OK;
}

esp_err_t read_file(const char *path, char *buffer, size_t bufferSize)
{
    if (!path || !buffer || bufferSize == 0)
    {
        ESP_LOGE(TAG, "Invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    FILE *file = fopen(path, "rb"); // Use binary mode for safety
    if (!file)
    {
        ESP_LOGE(TAG, "Failed to open file: %s", path);
        return ESP_FAIL;
    }

    size_t len = fread(buffer, 1, bufferSize - 1, file); // Leave space for '\0'
    buffer[len] = '\0';                                  // Null-terminate
    fclose(file);

    if (len == 0)
    {
        ESP_LOGW(TAG, "Read 0 bytes from file: %s", path);
        return ESP_ERR_INVALID_SIZE;
    }

    // ESP_LOGI(TAG, "Read %zu bytes from %s", len, path);
    return ESP_OK;
}

bool is_valid_ip_address(const char *ip_address)
{
    if (!ip_address || strlen(ip_address) == 0)
    {
        return false;
    }
    struct in_addr addr;
    return inet_pton(AF_INET, ip_address, &addr) == 1;
}

// Function to check if Wi-Fi has an IP
bool wifi_has_ip(void)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == NULL)
    {
        ESP_LOGW(TAG, "Wi-Fi STA interface not found");
        return false;
    }

    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK)
    {
        return false;
    }
    return ip_info.ip.addr != 0;
}

// Function to check which network interface is active and return its handle, or NULL if none
esp_netif_t *get_active_netif(void)
{
    if (eth_is_connected())
        return eth_netif;

    if (wifi_manager_is_connected())
        return esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");

    return NULL;
}

bool network_has_ip(void)
{
    esp_netif_t *netif = get_active_netif();
    if (!netif)
        return false;

    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(netif, &ip) != ESP_OK)
        return false;

    return ip.ip.addr != 0;
}

esp_err_t create_mutexes(void)
{
    bool ok = true;

    // Mutex for valve movement locking
    lock_valve_move_mutex = xSemaphoreCreateMutex();
    if (lock_valve_move_mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create lock_valve_move_mutex");
        ok = false;
    }

    // Mutex for sensor config settings
    sensor_cfg_mutex = xSemaphoreCreateMutex();
    if (sensor_cfg_mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create sensor_cfg_mutex");
        ok = false;
    }

    // Mutex for network settings
    network_config_mutex = xSemaphoreCreateMutex();
    if (network_config_mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create network_config_mutex");
        ok = false;
    }

    // Mutex for time settings
    time_config_mutex = xSemaphoreCreateMutex();
    if (time_config_mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create time_config_mutex");
        ok = false;
    }

    mqtt_config_mutex = xSemaphoreCreateMutex();
    if (mqtt_config_mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create mqtt_config_mutex");
        ok = false;
    }

    influxdb_config_mutex = xSemaphoreCreateMutex();
    if (influxdb_config_mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create influxdb_config_mutex");
        ok = false;
    }

    fan_config_mutex = xSemaphoreCreateMutex();
    if (fan_config_mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create fan_config_mutex");
        ok = false;
    }

    i2c_config_mutex = xSemaphoreCreateMutex();
    if (i2c_config_mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create i2c_config_mutex");
        ok = false;
    }

    syslog_config_mutex = xSemaphoreCreateMutex();
    if (syslog_config_mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create syslog_config_mutex");
        ok = false;
    }

    state_config_mutex = xSemaphoreCreateMutex();
    if (state_config_mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create statemachine_config_mutex");
        ok = false;
    }

    statemachine_mutex = xSemaphoreCreateMutex();
    if (statemachine_mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create statemachine_mutex");
        ok = false;
    }

    valve_control_data_mutex = xSemaphoreCreateMutex();
    if (valve_control_data_mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create statemachine_mutex");
        ok = false;
    }

    i2c_bus_mutex = xSemaphoreCreateMutex();
    if (i2c_bus_mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create i2c_bus_mutex");
        ok = false;
    }

    if (!ok)
    {
        // cleanup any created semaphores to avoid leaks and to keep state consistent
        if (lock_valve_move_mutex)
        {
            vSemaphoreDelete(lock_valve_move_mutex);
            lock_valve_move_mutex = NULL;
        }
        if (sensor_cfg_mutex)
        {
            vSemaphoreDelete(sensor_cfg_mutex);
            sensor_cfg_mutex = NULL;
        }
        if (network_config_mutex)
        {
            vSemaphoreDelete(network_config_mutex);
            network_config_mutex = NULL;
        }
        if (time_config_mutex)
        {
            vSemaphoreDelete(time_config_mutex);
            time_config_mutex = NULL;
        }
        if (mqtt_config_mutex)
        {
            vSemaphoreDelete(mqtt_config_mutex);
            mqtt_config_mutex = NULL;
        }
        if (influxdb_config_mutex)
        {
            vSemaphoreDelete(influxdb_config_mutex);
            influxdb_config_mutex = NULL;
        }
        if (fan_config_mutex)
        {
            vSemaphoreDelete(fan_config_mutex);
            fan_config_mutex = NULL;
        }
        if (i2c_config_mutex)
        {
            vSemaphoreDelete(i2c_config_mutex);
            i2c_config_mutex = NULL;
        }
        if (syslog_config_mutex)
        {
            vSemaphoreDelete(syslog_config_mutex);
            syslog_config_mutex = NULL;
        }
        if (state_config_mutex)
        {
            vSemaphoreDelete(state_config_mutex);
            state_config_mutex = NULL;
        }
        if (statemachine_mutex)
        {
            vSemaphoreDelete(statemachine_mutex);
            statemachine_mutex = NULL;
        }
        if (valve_control_data_mutex)
        {
            vSemaphoreDelete(valve_control_data_mutex);
            valve_control_data_mutex = NULL;
        }
        if (i2c_bus_mutex)
        {
            vSemaphoreDelete(i2c_bus_mutex);
            i2c_bus_mutex = NULL;
        }

        ESP_LOGE(TAG, "One or more mutexes failed to create");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "All mutexes created");
    return ESP_OK;
}

void init_timezone(void)
{
    char ntp_server[64];
    char timezone[64];

    // --- Read config under mutex ---
    if (xSemaphoreTake(time_config_mutex, pdMS_TO_TICKS(20)) != pdTRUE)
    {
        ESP_LOGE(TAG, "Failed to acquire time_config mutex");
        return;
    }

    strncpy(ntp_server, time_config.ntp_server, sizeof(ntp_server));
    ntp_server[sizeof(ntp_server) - 1] = '\0';
    strncpy(timezone, time_config.timezone, sizeof(timezone));
    timezone[sizeof(timezone) - 1] = '\0';
    xSemaphoreGive(time_config_mutex);

    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
}

/* ========== Ping to gateway functions ========== */
void on_ping_end(esp_ping_handle_t hdl, void *args)
{
    uint32_t transmitted = 0, received = 0, duration = 0, avg_time = 0;

    // Profiles available in ESP‑IDF 5.5.1
    esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &transmitted, sizeof(transmitted));
    esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &received, sizeof(received));
    esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &duration, sizeof(duration));
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &avg_time, sizeof(avg_time));

    int loss_percent = 0;
    if (transmitted > 0)
    {
        loss_percent = (int)((transmitted - received) * 100 / transmitted);
    }

    g_ping_summary.transmitted = (int)transmitted;
    g_ping_summary.received = (int)received;
    g_ping_summary.loss_percent = loss_percent;
    g_ping_summary.avg_time_ms = (int)avg_time;
    g_ping_summary.duration_ms = (int)duration;
    g_ping_summary.success = (received > 0);

    ESP_LOGD(TAG, "Ping finished: tx=%d rx=%d loss=%d%% avg_rtt=%d ms duration=%d ms",
             g_ping_summary.transmitted, g_ping_summary.received,
             g_ping_summary.loss_percent, g_ping_summary.avg_time_ms,
             g_ping_summary.duration_ms);

    // Clean up session
    esp_ping_delete_session(hdl);

    // Signal completion
    if (g_ping_done_sem)
    {
        xSemaphoreGive(g_ping_done_sem);
    }
}

bool ping_gateway(void)
{
    // Ensure station netif exists
    // esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    // if (!netif)
    //{
    //    ESP_LOGE(TAG, "No station netif found");
    //    return false;
    //}

    esp_netif_t *netif = get_active_netif();
    if (!netif)
    {
        // Optional: one generic log, not “Wi‑Fi STA not found”
        LOGW(TAG, "No active network interface");
        return false; // or keep waiting
    }

    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get IP info");
        return false;
    }

    // Log gateway using IPSTR/IP2STR to avoid incompatible-pointer warnings
    ESP_LOGD(TAG, "Pinging gateway: " IPSTR, IP2STR(&ip_info.gw));

    // Build ip_addr_t target_addr from ip_info.gw
    ip_addr_t target_addr;
    IP_ADDR4(&target_addr, ip4_addr1(&ip_info.gw), ip4_addr2(&ip_info.gw), ip4_addr3(&ip_info.gw), ip4_addr4(&ip_info.gw));

    ESP_LOGD(TAG, "Pinging gateway: " IPSTR, IP2STR(&ip_info.gw));

    // Reset summary
    memset(&g_ping_summary, 0, sizeof(g_ping_summary));

    // Create (or reuse) semaphore
    if (g_ping_done_sem == NULL)
    {
        g_ping_done_sem = xSemaphoreCreateBinary();
        if (g_ping_done_sem == NULL)
        {
            ESP_LOGE(TAG, "Failed to create semaphore");
            return false;
        }
    }
    else
    {
        // Drain any previous signal
        xSemaphoreTake(g_ping_done_sem, 0);
    }

    // Configure ping session
    esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
    config.count = 5;                 // number of probes
    config.interval_ms = 1000;        // 1s between pings
    config.timeout_ms = 1000;         // per‑ping timeout
    config.target_addr = target_addr; // gateway

    esp_ping_callbacks_t cbs = {
        .on_ping_end = on_ping_end,
        // You can add per‑reply logging if you want:
        // .on_ping_success = your_success_cb,
        // .on_ping_timeout = your_timeout_cb,
    };

    // Start session
    esp_ping_handle_t ping = NULL;
    esp_err_t err = esp_ping_new_session(&config, &cbs, &ping);
    if (err != ESP_OK || ping == NULL)
    {
        ESP_LOGE(TAG, "esp_ping_new_session failed: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_ping_start(ping);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_ping_start failed: %s", esp_err_to_name(err));
        esp_ping_delete_session(ping);
        return false;
    }

    // Wait for completion (5 pings + buffer)
    // Duration profile will be ~count * interval + timeouts; use a safe upper bound
    const TickType_t wait_ticks = pdMS_TO_TICKS(10000);
    if (xSemaphoreTake(g_ping_done_sem, wait_ticks) != pdTRUE)
    {
        ESP_LOGW(TAG, "Ping session timeout waiting for completion");
        // If still running, stop and cleanup
        esp_ping_stop(ping);
        esp_ping_delete_session(ping);
        return false;
    }

    // Success = at least one reply
    return g_ping_summary.success;
}

void formatted_uptime(char *buf, size_t bufsize)
{
    uint64_t uptime = esp_timer_get_time() / 1000000; // uptime in seconds

    int uptime_day = uptime / (60 * 60 * 24);
    int uptime_hour = (uptime / (60 * 60)) % 24;
    int uptime_minute = (uptime / 60) % 60;
    int uptime_second = uptime % 60;

    snprintf(buf, bufsize, "%dd %dh %02dm %02ds", uptime_day, uptime_hour, uptime_minute, uptime_second);
}

void formatted_daydatetime(char *buf, size_t bufsize)
{
    struct tm temp_time;
    time_t now;

    static const char *day_names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

    // Read current time and put data in struct temp_time
    time(&now);
    localtime_r(&now, &temp_time);

    snprintf(buf, bufsize, "%s %04d/%02d/%02d-%02d:%02d:%02d", day_names[temp_time.tm_wday], temp_time.tm_year + 1900, temp_time.tm_mon + 1, temp_time.tm_mday, temp_time.tm_hour, temp_time.tm_min, temp_time.tm_sec);
}

void formatted_datetime(char *buf, size_t bufsize)
{
    struct tm temp_time;
    time_t now = 0;

    // Read local time and put data in struct temp_time
    time(&now);
    localtime_r(&now, &temp_time);

    snprintf(buf, bufsize, "%04d/%02d/%02d-%02d:%02d:%02d", temp_time.tm_year + 1900, temp_time.tm_mon + 1, temp_time.tm_mday, temp_time.tm_hour, temp_time.tm_min, temp_time.tm_sec);
}

void formatted_day(char *buf, size_t bufsize)
{
    int dayofweek = 0;
    static const char *day_names[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    struct tm temp_time;
    time_t now = 0;

    time(&now);
    localtime_r(&now, &temp_time);

    // Get day of week from local time struct
    dayofweek = temp_time.tm_wday;

    // Clamp dayofweek to valid range just in case
    if (dayofweek < 0 || dayofweek > 6)
    {
        dayofweek = 0;
    }

    snprintf(buf, bufsize, "%s", day_names[dayofweek]);
}

void set_log_levels(void)
{
    // Set log levels, in general only error are required unless debugging
    // esp_log_level_set("*", ESP_LOG_INFO);

    esp_log_level_set("bus_lock", ESP_LOG_NONE);
    esp_log_level_set("spi", ESP_LOG_NONE);
    esp_log_level_set("spi_master", ESP_LOG_NONE);
    esp_log_level_set("scd4x", ESP_LOG_ERROR);
    esp_log_level_set("tca9548", ESP_LOG_ERROR);
    esp_log_level_set("aht", ESP_LOG_ERROR);
    esp_log_level_set("esp_littlefs", ESP_LOG_ERROR);
    esp_log_level_set("nvs", ESP_LOG_ERROR);
    esp_log_level_set("wifi", ESP_LOG_ERROR);
    esp_log_level_set("esp_netif_lwip", ESP_LOG_ERROR);
    esp_log_level_set("esp_netif_handlers", ESP_LOG_ERROR);
    esp_log_level_set("event", ESP_LOG_ERROR);
    esp_log_level_set("wifi_init_default", ESP_LOG_ERROR);
    esp_log_level_set("esp_adapter", ESP_LOG_ERROR);
    esp_log_level_set("DS3231", ESP_LOG_INFO);
    esp_log_level_set("gpio", ESP_LOG_ERROR);
    esp_log_level_set("httpd", ESP_LOG_ERROR);
    esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
    esp_log_level_set("httpd_ws", ESP_LOG_ERROR);
    esp_log_level_set("httpd_parse", ESP_LOG_ERROR);
    esp_log_level_set("httpd_sess", ESP_LOG_ERROR);
    esp_log_level_set("httpd_uri", ESP_LOG_ERROR);
    esp_log_level_set("mqtt_client", ESP_LOG_ERROR);
    esp_log_level_set("transport_base", ESP_LOG_ERROR);
    esp_log_level_set("outbox", ESP_LOG_ERROR);
    esp_log_level_set("esp-tls", ESP_LOG_ERROR);
    esp_log_level_set("HTTP_CLIENT", ESP_LOG_ERROR);
    esp_log_level_set("w5500.mac", ESP_LOG_NONE);
    esp_log_level_set("esp_eth", ESP_LOG_NONE);
    esp_log_level_set("gdma", ESP_LOG_NONE);

    // Custom TAG sttings
    esp_log_level_set("Config", ESP_LOG_INFO);
    esp_log_level_set("Eth_func", ESP_LOG_INFO);
    esp_log_level_set("Fan_control", ESP_LOG_INFO);
    esp_log_level_set("General", ESP_LOG_INFO);
    esp_log_level_set("HTTP_server", ESP_LOG_INFO);
    // esp_log_level_set("HTTP_POST_functions", ESP_LOG_INFO);      // Nothing is logged now
    esp_log_level_set("HTTP_JSON_ws", ESP_LOG_INFO);
    esp_log_level_set("I2C_DS3231", ESP_LOG_INFO);
    esp_log_level_set("i2c_functions", ESP_LOG_DEBUG);
    esp_log_level_set("i2c_LCD", ESP_LOG_INFO);
    esp_log_level_set("i2c_sensors", ESP_LOG_DEBUG);
    esp_log_level_set("Influxdb", ESP_LOG_INFO);
    esp_log_level_set("main", ESP_LOG_INFO);
    esp_log_level_set("MQTT", ESP_LOG_INFO);
    esp_log_level_set("RGB_LED", ESP_LOG_INFO);
    esp_log_level_set("SM_helpers", ESP_LOG_INFO);
    esp_log_level_set("SM", ESP_LOG_INFO);
    esp_log_level_set("Sys_stats", ESP_LOG_INFO);
    esp_log_level_set("Syslog", ESP_LOG_INFO);
    esp_log_level_set("task_DS3231_sync", ESP_LOG_INFO);
    esp_log_level_set("task_eth", ESP_LOG_INFO);
    esp_log_level_set("task_i2c_LCD", ESP_LOG_INFO);
    esp_log_level_set("task_i2c_sensors", ESP_LOG_DEBUG);
    esp_log_level_set("task_influxdb", ESP_LOG_INFO);
    esp_log_level_set("task_MQTT", ESP_LOG_INFO);
    esp_log_level_set("task_rgb_leds", ESP_LOG_INFO);
    esp_log_level_set("task_SM", ESP_LOG_INFO);
    esp_log_level_set("task_wifi", ESP_LOG_INFO);
    esp_log_level_set("valve_control", ESP_LOG_INFO);
    esp_log_level_set("wifi_functions", ESP_LOG_INFO);
    esp_log_level_set("syslog", ESP_LOG_WARN);
    esp_log_level_set("control_valves", ESP_LOG_INFO);
    esp_log_level_set("Eth_func", ESP_LOG_INFO);
    esp_log_level_set("task_eth", ESP_LOG_INFO);

    // esp_log_level_set("*", ESP_LOG_INFO);       // Set all log levels
}

void print_html_file(const char *filepath)
{
    FILE *f = fopen(filepath, "r");

    if (!f)
    {
        ESP_LOGE(TAG, "Failed to open %s", filepath);
        return;
    }

    char buf[256];
    while (fgets(buf, sizeof(buf), f))
    {
        // Print each line to console
        printf("%s", buf);
    }

    fclose(f);
    ESP_LOGI(TAG, "Finished printing %s", filepath);
}