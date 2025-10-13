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
        .base_path = "/littlefs",
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

    FILE *file = fopen(path, "r");
    if (!file)
    {
        ESP_LOGE(TAG, "Failed to open file: %s", path);
        return ESP_FAIL;
    }

    size_t len = fread(buffer, 1, bufferSize - 1, file);
    buffer[len] = '\0'; // Null-terminate

    fclose(file);
    return ESP_OK;
}

esp_err_t load_bus0_sensor_config(const char *config_name)
{
    static const char *TAG = "settings";

    char buffer[XXLARGE_CONFIG_ITEM];
    char path[64];

    snprintf(path, sizeof(path), "/littlefs/%s.json", config_name);

    esp_err_t err = read_file(path, buffer, sizeof(buffer));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read config file: %s", path);
        return err;
    }

    cJSON *json = cJSON_Parse(buffer);
    if (!json)
    {
        ESP_LOGE(TAG, "Invalid JSON format in %s", path);
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

        strncpy(bus0_sensor_settings[i].type, cJSON_IsString(type) ? type->valuestring : "", SMALL_CONFIG_ITEM);
        strncpy(bus0_sensor_settings[i].valve, cJSON_IsString(valve) ? valve->valuestring : "", MEDIUM_CONFIG_ITEM);
        strncpy(bus0_sensor_settings[i].location, cJSON_IsString(location) ? location->valuestring : "", LARGE_CONFIG_ITEM);
        bus0_sensor_settings[i].rh_enabled = cJSON_IsString(rh) && strcmp(rh->valuestring, "On") == 0;
        bus0_sensor_settings[i].co2_enabled = cJSON_IsString(co2) && strcmp(co2->valuestring, "On") == 0;
    }

    cJSON_Delete(json);
    ESP_LOGI(TAG, "Bus0 sensor config loaded from %s", path);
    return ESP_OK;
}