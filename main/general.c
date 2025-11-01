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
    static const char *TAG = "settings";

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

esp_err_t load_sensor_config(const char *config_name)
{
    const char *TAG = "Config";
    char path[128];
    size_t bufferSize = XXXLARGE_CONFIG_ITEM;

    if (!config_name || strlen(config_name) > 48)
    {
        ESP_LOGE(TAG, "Invalid config name");
        return ESP_ERR_INVALID_ARG;
    }

    snprintf(path, sizeof(path), "/data/%s.json", config_name);
    ESP_LOGI(TAG, "Attempting to read config: %s", path);

    char *buffer = malloc(bufferSize);
    if (!buffer)
    {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = read_file(path, buffer, bufferSize);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read config file: %s", path);
        free(buffer);
        return err;
    }

    cJSON *json = cJSON_Parse(buffer);
    if (!json)
    {
        ESP_LOGE(TAG, "Invalid JSON format in %s", path);
        // ESP_LOGW(TAG, "Raw content: %s", buffer);
        free(buffer);
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Determine bus index from config_name
    int bus_index = -1;
    if (strncmp(config_name, "bus0", 4) == 0)
    {
        bus_index = 0;
    }
    else if (strncmp(config_name, "bus1", 4) == 0)
    {
        bus_index = 1;
    }
    else
    {
        ESP_LOGE(TAG, "Unknown bus prefix in config name: %s", config_name);
        cJSON_Delete(json);
        free(buffer);
        return ESP_ERR_INVALID_ARG;
    }

    for (int i = 0; i < CHANNELS_PER_BUS; i++)
    {
        char key_type[32], key_valve[32], key_location[32], key_rh[32], key_co2[32];
        snprintf(key_type, sizeof(key_type), "b%d_s%d_type", bus_index, i);
        snprintf(key_valve, sizeof(key_valve), "b%d_s%d_valve", bus_index, i);
        snprintf(key_location, sizeof(key_location), "b%d_s%d_location", bus_index, i);
        snprintf(key_rh, sizeof(key_rh), "b%d_s%d_rh", bus_index, i);
        snprintf(key_co2, sizeof(key_co2), "b%d_s%d_co2", bus_index, i);

        cJSON *type = cJSON_GetObjectItem(json, key_type);
        cJSON *valve = cJSON_GetObjectItem(json, key_valve);
        cJSON *location = cJSON_GetObjectItem(json, key_location);
        cJSON *rh = cJSON_GetObjectItem(json, key_rh);
        cJSON *co2 = cJSON_GetObjectItem(json, key_co2);

        if (!type)
            ESP_LOGW(TAG, "Missing key: %s", key_type);
        if (!valve)
            ESP_LOGW(TAG, "Missing key: %s", key_valve);
        if (!location)
            ESP_LOGW(TAG, "Missing key: %s", key_location);
        if (!rh)
            ESP_LOGW(TAG, "Missing key: %s", key_rh);
        if (!co2)
            ESP_LOGW(TAG, "Missing key: %s", key_co2);

        bus_sensor_t *settings = (bus_index == 0) ? &bus0_sensor_settings[i] : &bus1_sensor_settings[i];

        strncpy(settings->type, cJSON_IsString(type) ? type->valuestring : "", SMALL_CONFIG_ITEM - 1);
        settings->type[SMALL_CONFIG_ITEM - 1] = '\0';

        strncpy(settings->valve, cJSON_IsString(valve) ? valve->valuestring : "", MEDIUM_CONFIG_ITEM - 1);
        settings->valve[MEDIUM_CONFIG_ITEM - 1] = '\0';

        strncpy(settings->location, cJSON_IsString(location) ? location->valuestring : "", LARGE_CONFIG_ITEM - 1);
        settings->location[LARGE_CONFIG_ITEM - 1] = '\0';

        settings->rh_enabled = cJSON_IsString(rh) && strcmp(rh->valuestring, "On") == 0;
        settings->co2_enabled = cJSON_IsString(co2) && strcmp(co2->valuestring, "On") == 0;
    }

    cJSON_Delete(json);
    free(buffer);

    ESP_LOGI(TAG, "Bus%d sensor config loaded from %s", bus_index, path);
    return ESP_OK;
}

esp_err_t load_network_config(void)
{
    const char *TAG = "Config";
    char path[128] = "/data/network_config.json";
    size_t bufferSize = XXXLARGE_CONFIG_ITEM;

    ESP_LOGI(TAG, "Attempting to read network config: %s", path);

    char *buffer = malloc(bufferSize);
    if (!buffer)
    {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = read_file(path, buffer, bufferSize);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read config file: %s", path);
        free(buffer);
        return err;
    }

    cJSON *json = cJSON_Parse(buffer);
    if (!json)
    {
        ESP_LOGE(TAG, "Invalid JSON format in %s", path);
        // ESP_LOGW(TAG, "Raw content: %s", buffer);
        free(buffer);
        return ESP_ERR_INVALID_RESPONSE;
    }

    cJSON *enable_dhcp = cJSON_GetObjectItem(json, "enable_dhcp");
    cJSON *ssid = cJSON_GetObjectItem(json, "ssid");
    cJSON *wifi_password = cJSON_GetObjectItem(json, "wifi_password");
    cJSON *ip_address = cJSON_GetObjectItem(json, "ip_address");
    cJSON *subnet_mask = cJSON_GetObjectItem(json, "subnet_mask");
    cJSON *gateway = cJSON_GetObjectItem(json, "gateway");
    cJSON *primary_dns = cJSON_GetObjectItem(json, "primary_dns");
    cJSON *secondary_dns = cJSON_GetObjectItem(json, "secondary_dns");

    // Take mutex with timeout
    if (xSemaphoreTake(network_config_mutex, pdMS_TO_TICKS(10)) != pdTRUE)
    {
        ESP_LOGE(TAG, "Failed to acquire WiFi config mutex");
        return ESP_ERR_TIMEOUT;
    }

    strncpy(network_config.enable_dhcp, cJSON_IsString(enable_dhcp) ? enable_dhcp->valuestring : "", SMALL_CONFIG_ITEM - 1);
    network_config.enable_dhcp[SMALL_CONFIG_ITEM - 1] = '\0';

    strncpy(network_config.ssid, cJSON_IsString(ssid) ? ssid->valuestring : "", MEDIUM_CONFIG_ITEM - 1);
    network_config.ssid[MEDIUM_CONFIG_ITEM - 1] = '\0';

    strncpy(network_config.wifi_password, cJSON_IsString(wifi_password) ? wifi_password->valuestring : "", LARGE_CONFIG_ITEM - 1);
    network_config.wifi_password[LARGE_CONFIG_ITEM - 1] = '\0';

    strncpy(network_config.ip_address, cJSON_IsString(ip_address) ? ip_address->valuestring : "", MEDIUM_CONFIG_ITEM - 1);
    network_config.ip_address[LARGE_CONFIG_ITEM - 1] = '\0';

    strncpy(network_config.subnet_mask, cJSON_IsString(subnet_mask) ? subnet_mask->valuestring : "", MEDIUM_CONFIG_ITEM - 1);
    network_config.subnet_mask[LARGE_CONFIG_ITEM - 1] = '\0';

    strncpy(network_config.gateway, cJSON_IsString(gateway) ? gateway->valuestring : "", MEDIUM_CONFIG_ITEM - 1);
    network_config.gateway[LARGE_CONFIG_ITEM - 1] = '\0';

    strncpy(network_config.primary_dns, cJSON_IsString(primary_dns) ? primary_dns->valuestring : "", MEDIUM_CONFIG_ITEM - 1);
    network_config.primary_dns[LARGE_CONFIG_ITEM - 1] = '\0';

    strncpy(network_config.secondary_dns, cJSON_IsString(secondary_dns) ? secondary_dns->valuestring : "", MEDIUM_CONFIG_ITEM - 1);
    network_config.secondary_dns[LARGE_CONFIG_ITEM - 1] = '\0';

    xSemaphoreGive(network_config_mutex);

    cJSON_Delete(json);
    free(buffer);

    ESP_LOGI(TAG, "Network config loaded from %s", path);

    return ESP_OK;
}

esp_err_t load_time_config(void)
{
    const char *TAG = "Config";
    char path[128] = "/data/time_config.json";
    size_t bufferSize = XXLARGE_CONFIG_ITEM;

    ESP_LOGI(TAG, "Attempting to read time config: %s", path);

    char *buffer = malloc(bufferSize);
    if (!buffer)
    {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = read_file(path, buffer, bufferSize);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read config file: %s", path);
        free(buffer);
        return err;
    }

    cJSON *json = cJSON_Parse(buffer);
    if (!json)
    {
        ESP_LOGE(TAG, "Invalid JSON format in %s", path);
        // ESP_LOGW(TAG, "Raw content: %s", buffer);
        free(buffer);
        return ESP_ERR_INVALID_RESPONSE;
    }

    cJSON *ntp_server = cJSON_GetObjectItem(json, "ntp_server");
    cJSON *timezone = cJSON_GetObjectItem(json, "timezone");

    // Take mutex with timeout
    if (xSemaphoreTake(time_config_mutex, pdMS_TO_TICKS(10)) != pdTRUE)
    {
        ESP_LOGE(TAG, "Failed to acquire time config mutex");
        return ESP_ERR_TIMEOUT;
    }

    strncpy(time_config.ntp_server, cJSON_IsString(ntp_server) ? ntp_server->valuestring : "", MEDIUM_CONFIG_ITEM - 1);
    time_config.ntp_server[MEDIUM_CONFIG_ITEM - 1] = '\0';

    strncpy(time_config.timezone, cJSON_IsString(timezone) ? timezone->valuestring : "", MEDIUM_CONFIG_ITEM - 1);
    time_config.timezone[MEDIUM_CONFIG_ITEM - 1] = '\0';

    xSemaphoreGive(time_config_mutex);

    cJSON_Delete(json);
    free(buffer);

    ESP_LOGI(TAG, "Network config loaded from %s", path);

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
    const char *TAG = "Mutexes";

    // Mutex for network settings
    network_config_mutex = xSemaphoreCreateMutex();
    if (network_config_mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create WiFi config mutex");
    }

    // Mutex for time settings
    time_config_mutex = xSemaphoreCreateMutex();
    if (time_config_mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create WiFi config mutex");
    }
    return ESP_OK;
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

void on_ping_end(esp_ping_handle_t hdl, void *args)
{
    const char *TAG = "Ping";
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

    ESP_LOGI(TAG, "Ping finished: tx=%d rx=%d loss=%d%% avg_rtt=%d ms duration=%d ms",
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
    const char *TAG = "Ping";

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
    IP_ADDR4(&target_addr,
             ip4_addr1(&ip_info.gw),
             ip4_addr2(&ip_info.gw),
             ip4_addr3(&ip_info.gw),
             ip4_addr4(&ip_info.gw));

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
    const TickType_t wait_ticks = pdMS_TO_TICKS(8000);
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