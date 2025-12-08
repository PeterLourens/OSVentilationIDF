#include "general.h"

esp_err_t init_nvs(void)
{
    const char *TAG = "Storage";
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
    const char *TAG = "Storage";
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
    static const char *TAG = "config";

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

    ESP_LOGI(TAG, "Read %zu bytes from %s", len, path);
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

esp_err_t create_mutexes(void)
{
    const char *TAG = "mutexes";
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

    statemachine_config_mutex = xSemaphoreCreateMutex();
    if (statemachine_config_mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create statemachine_config_mutex");
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
        if (statemachine_config_mutex)
        {
            vSemaphoreDelete(statemachine_config_mutex);
            statemachine_config_mutex = NULL;
        }

        ESP_LOGE(TAG, "One or more mutexes failed to create");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "All mutexes created");
    return ESP_OK;
}

/* ========== Ping to gateway functions ========== */
void on_ping_end(esp_ping_handle_t hdl, void *args)
{
    // const char *TAG = "network";
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

    /*ESP_LOGI(TAG, "Ping finished: tx=%d rx=%d loss=%d%% avg_rtt=%d ms duration=%d ms",
             g_ping_summary.transmitted, g_ping_summary.received,
             g_ping_summary.loss_percent, g_ping_summary.avg_time_ms,
             g_ping_summary.duration_ms);*/

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
    const char *TAG = "network";

    // Ensure station netif exists
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!netif)
    {
        ESP_LOGE(TAG, "No station netif found");
        return false;
    }

    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get IP info");
        return false;
    }

    // Log gateway using IPSTR/IP2STR to avoid incompatible-pointer warnings
    // ESP_LOGI(TAG, "Pinging gateway: " IPSTR, IP2STR(&ip_info.gw));

    // Build ip_addr_t target_addr from ip_info.gw
    ip_addr_t target_addr;
    IP_ADDR4(&target_addr, ip4_addr1(&ip_info.gw), ip4_addr2(&ip_info.gw), ip4_addr3(&ip_info.gw), ip4_addr4(&ip_info.gw));

    // ESP_LOGI(TAG, "Pinging gateway: " IPSTR, IP2STR(&ip_info.gw));

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

    snprintf(buf, bufsize, "%s %04d/%02d/%02d-%02d:%02d:%02d", day_names[temp_time.tm_wday], temp_time.tm_year, temp_time.tm_mon + 1, temp_time.tm_mday, temp_time.tm_hour, temp_time.tm_min, temp_time.tm_sec);
}

void formatted_datetime(char *buf, size_t bufsize)
{
    struct tm temp_time;
    time_t now = 0;

    // Read local time and put data in struct temp_time
    time(&now);
    localtime_r(&now, &temp_time);

    snprintf(buf, bufsize, "%04d/%02d/%02d-%02d:%02d:%02d", temp_time.tm_year, temp_time.tm_mon + 1, temp_time.tm_mday, temp_time.tm_hour, temp_time.tm_min, temp_time.tm_sec);
}

void set_log_levels(void)
{
    // Set log levels, in general only error are required unless debugging
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
    // esp_log_level_set("*", ESP_LOG_ERROR);
}

void print_html_file(const char *filepath)
{
    const char *TAG = "print";

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