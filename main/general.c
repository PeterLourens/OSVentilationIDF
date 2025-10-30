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
        ESP_LOGW(TAG, "Raw content: %s", buffer);
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

esp_err_t load_networksettings(void)
{
    const char *TAG = "Config";
    char path[] = "/data/networksettings.json";
    size_t bufferSize = XXXLARGE_CONFIG_ITEM;

    char *buffer = malloc(bufferSize);
    if (!buffer)
    {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = read_file(path, buffer, bufferSize);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read network settings file: %s", path);
        free(buffer);
        return err;
    }

    cJSON *json = cJSON_Parse(buffer);
    if (!json)
    {
        ESP_LOGE(TAG, "Invalid JSON format in %s", path);
        ESP_LOGW(TAG, "Raw content: %s", buffer);
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

    if( enable_dhcp && cJSON_IsString(enable_dhcp))
    {
        strncpy(networksettings.enable_dhcp, enable_dhcp->valuestring, sizeof(networksettings.enable_dhcp) - 1);
        networksettings.enable_dhcp[sizeof(networksettings.enable_dhcp) - 1] = '\0';
    }
    else
    {
        ESP_LOGW(TAG, "Missing or invalid DHCP setting in network settings");
    }
    
    if (cJSON_IsString(ssid))
    {
        strncpy(networksettings.ssid, ssid->valuestring, sizeof(networksettings.ssid) - 1);
        networksettings.ssid[sizeof(networksettings.ssid) - 1] = '\0';
    }
    else
    {
        ESP_LOGW(TAG, "Missing or invalid SSID in network settings");
    }

    if (cJSON_IsString(wifi_password))
    {
        strncpy(networksettings.wifi_password, wifi_password->valuestring, sizeof(networksettings.wifi_password) - 1);
        networksettings.wifi_password[sizeof(networksettings.wifi_password) - 1] = '\0';
    }
    else
    {
        ESP_LOGW(TAG, "Missing or invalid password in network settings");
    }
    
    if (cJSON_IsString(ip_address))
    {
        strncpy(networksettings.ip_address, ip_address->valuestring, sizeof(networksettings.ip_address) - 1);
        networksettings.ip_address[sizeof(networksettings.ip_address) - 1] = '\0';
    }
    else
    {
        ESP_LOGW(TAG, "Missing or invalid IP address in network settings");
    }
    
    if (cJSON_IsString(subnet_mask))
    {
        strncpy(networksettings.subnet_mask, subnet_mask->valuestring, sizeof(networksettings.subnet_mask) - 1);
        networksettings.subnet_mask[sizeof(networksettings.subnet_mask) - 1] = '\0';
    }
    else
    {
        ESP_LOGW(TAG, "Missing or invalid subnet mask in network settings");
    }
    
    if (cJSON_IsString(gateway))
    {
        strncpy(networksettings.gateway, gateway->valuestring, sizeof(networksettings.gateway) - 1);
        networksettings.gateway[sizeof(networksettings.gateway) - 1] = '\0';
    }
    else
    {
        ESP_LOGW(TAG, "Missing or invalid gateway in network settings");
    }
    
    if (cJSON_IsString(primary_dns))
    {
        strncpy(networksettings.primary_dns, primary_dns->valuestring, sizeof(networksettings.primary_dns) - 1);
        networksettings.primary_dns[sizeof(networksettings.primary_dns) - 1] = '\0';
    }
    else
    {
        ESP_LOGW(TAG, "Missing or invalid primary DNS in network settings");
    }
    
    if (cJSON_IsString(secondary_dns))
    {
        strncpy(networksettings.secondary_dns, secondary_dns->valuestring, sizeof(networksettings.secondary_dns) - 1);
        networksettings.secondary_dns[sizeof(networksettings.secondary_dns) - 1] = '\0';
    }
    else
    {
        ESP_LOGW(TAG, "Missing or invalid primary DNS in network settings");
    }

    cJSON_Delete(json);
    free(buffer);

    ESP_LOGI(TAG, "Network settings loaded from %s", path);
    return ESP_OK;
}