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
    const char *TAG = "settings";

    FILE *file = fopen(path, "r");
    if (!file)
    {
        ESP_LOGE(TAG, "Failed to open file: %s", path);
        return ESP_FAIL;
    }

    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    rewind(file);

    ESP_LOGI(TAG, "File size reported: %ld bytes", size);
    ESP_LOGI(TAG, "Buffer size: %d bytes", bufferSize);

    if (size > bufferSize - 1)
    {
        ESP_LOGW(TAG, "File too large for buffer");
        fclose(file);
        return ESP_ERR_NO_MEM;
    }

    size_t len = fread(buffer, 1, size, file);
    buffer[len] = '\0';

    fclose(file);

    ESP_LOGI(TAG, "Read %d bytes from file", len);
    return ESP_OK;
}

esp_err_t load_sensor_config(const char *config_name)
{
    const char *TAG = "Config";
    char path[128];

    if (!config_name || strlen(config_name) > 48)
    {
        ESP_LOGE(TAG, "Invalid config name");
        return ESP_ERR_INVALID_ARG;
    }

    char *buffer = malloc(40960);
    if (!buffer)
    {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        return ESP_ERR_NO_MEM;
    }
    // memset(buffer, 0, XXXLARGE_CONFIG_ITEM);

    snprintf(path, sizeof(path), "/data/%s.json", config_name);

    ESP_LOGI(TAG, "Attempting to read config: %s", path);

    esp_err_t err = read_file(path, buffer, sizeof(buffer));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read config file: %s", path);
        return err;
    }
    else
    {
        ESP_LOGI(TAG, "Config file %s read successfully", path);
    }

    ESP_LOGI(TAG, "Buffer content:\n%s", buffer);

    /*cJSON *json = cJSON_Parse(buffer);
    if (!json)
    {
        ESP_LOGE(TAG, "Invalid JSON format in %s", path);
        ESP_LOGW(TAG, "Raw content: %s", buffer);
        return ESP_ERR_INVALID_RESPONSE;
    }

    for (int i = 0; i < BUS_MAX_SENSORS; i++)
    {
        char key_type[32], key_valve[32], key_location[32], key_rh[32], key_co2[32];
        snprintf(key_type, sizeof(key_type), "b0_s%d_type", i);
        snprintf(key_valve, sizeof(key_valve), "b0_s%d_valve", i);
        snprintf(key_location, sizeof(key_location), "b0_s%d_location", i);
        snprintf(key_rh, sizeof(key_rh), "b0_s%d_rh", i);
        snprintf(key_co2, sizeof(key_co2), "b0_s%d_co2", i);

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

        strncpy(bus0_sensor_settings[i].type, cJSON_IsString(type) ? type->valuestring : "", SMALL_CONFIG_ITEM - 1);
        bus0_sensor_settings[i].type[SMALL_CONFIG_ITEM - 1] = '\0';

        strncpy(bus0_sensor_settings[i].valve, cJSON_IsString(valve) ? valve->valuestring : "", MEDIUM_CONFIG_ITEM - 1);
        bus0_sensor_settings[i].valve[MEDIUM_CONFIG_ITEM - 1] = '\0';

        strncpy(bus0_sensor_settings[i].location, cJSON_IsString(location) ? location->valuestring : "", LARGE_CONFIG_ITEM - 1);
        bus0_sensor_settings[i].location[LARGE_CONFIG_ITEM - 1] = '\0';

        bus0_sensor_settings[i].rh_enabled = cJSON_IsString(rh) && strcmp(rh->valuestring, "On") == 0;
        bus0_sensor_settings[i].co2_enabled = cJSON_IsString(co2) && strcmp(co2->valuestring, "On") == 0;
    }

    cJSON_Delete(json);
    ESP_LOGI(TAG, "Bus0 sensor config loaded from %s", path);*/

    free(buffer);

    return ESP_OK;
}