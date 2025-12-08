#include "config_files.h"

static const char *TAG = "Config";

esp_err_t load_sensor_config(const char *config_name)
{
    char path[128];
    size_t bufferSize = XXXLARGE_CONFIG_ITEM;

    if (!config_name || strlen(config_name) > 48)
    {
        ESP_LOGE(TAG, "Invalid config name");
        return ESP_ERR_INVALID_ARG;
    }

    snprintf(path, sizeof(path), "/data/json/%s.json", config_name);
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

    // Acquire sensor config mutex before updating global settings
    bool taken = false;
    if (sensor_cfg_mutex)
    {
        if (xSemaphoreTake(sensor_cfg_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
        {
            ESP_LOGE(TAG, "Failed to acquire sensor config mutex");
            cJSON_Delete(json);
            free(buffer);
            return ESP_ERR_TIMEOUT;
        }
        taken = true;
    }
    else
    {
        ESP_LOGW(TAG, "sensor_cfg_mutex not initialized; proceeding without mutex (unsafe)");
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

    // Release sensor config mutex after updates
    if (taken)
    {
        xSemaphoreGive(sensor_cfg_mutex);
    }

    cJSON_Delete(json);
    free(buffer);

    ESP_LOGI(TAG, "Bus%d sensor config loaded from %s", bus_index, path);
    return ESP_OK;
}

esp_err_t load_network_config(void)
{
    char path[128] = "/data/json/network_config.json";
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

    // Acquire network config mutex before updating global settings
    bool taken = false;
    if (network_config_mutex)
    {
        if (xSemaphoreTake(network_config_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
        {
            ESP_LOGE(TAG, "Failed to acquire sensor config mutex");
            cJSON_Delete(json);
            free(buffer);
            return ESP_ERR_TIMEOUT;
        }
        taken = true;
    }
    else
    {
        ESP_LOGW(TAG, "network_config_mutex not initialized; proceeding without mutex (unsafe)");
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

    // Release sensor config mutex after updates
    if (taken)
    {
        xSemaphoreGive(network_config_mutex);
    }

    cJSON_Delete(json);
    free(buffer);

    ESP_LOGI(TAG, "Network config loaded from %s", path);

    return ESP_OK;
}

esp_err_t load_time_config(void)
{
    const char *TAG = "Config";
    char path[128] = "/data/json/time_config.json";
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

    // Acquire time config mutex before updating global settings
    bool taken = false;
    if (time_config_mutex)
    {
        if (xSemaphoreTake(time_config_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
        {
            ESP_LOGE(TAG, "Failed to acquire time config mutex");
            cJSON_Delete(json);
            free(buffer);
            return ESP_ERR_TIMEOUT;
        }
        taken = true;
    }
    else
    {
        ESP_LOGW(TAG, "time_config_mutex not initialized; proceeding without mutex (unsafe)");
    }

    strncpy(time_config.ntp_server, cJSON_IsString(ntp_server) ? ntp_server->valuestring : "", MEDIUM_CONFIG_ITEM - 1);
    time_config.ntp_server[MEDIUM_CONFIG_ITEM - 1] = '\0';

    strncpy(time_config.timezone, cJSON_IsString(timezone) ? timezone->valuestring : "", MEDIUM_CONFIG_ITEM - 1);
    time_config.timezone[MEDIUM_CONFIG_ITEM - 1] = '\0';

    // Release sensor config mutex after updates
    if (taken)
    {
        xSemaphoreGive(time_config_mutex);
    }

    cJSON_Delete(json);
    free(buffer);

    ESP_LOGI(TAG, "Network config loaded from %s", path);

    return ESP_OK;
}

esp_err_t load_mqtt_config(void)
{
    const char *TAG = "Config";
    char path[128] = "/data/json/mqtt_config.json";
    size_t bufferSize = XXLARGE_CONFIG_ITEM;

    ESP_LOGI(TAG, "Attempting to read MQTT config: %s", path);

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

    cJSON *enable_mqtt = cJSON_GetObjectItem(json, "enable_mqtt");
    cJSON *mqtt_server = cJSON_GetObjectItem(json, "mqtt_server");
    cJSON *mqtt_port = cJSON_GetObjectItem(json, "mqtt_port");
    cJSON *mqtt_base_topic = cJSON_GetObjectItem(json, "mqtt_base_topic");

    // Acquire mqtt config mutex before updating global settings
    bool taken = false;
    if (mqtt_config_mutex)
    {
        if (xSemaphoreTake(mqtt_config_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
        {
            ESP_LOGE(TAG, "Failed to acquire MQTT config mutex");
            cJSON_Delete(json);
            free(buffer);
            return ESP_ERR_TIMEOUT;
        }
        taken = true;
    }
    else
    {
        ESP_LOGW(TAG, "mqtt_config_mutex not initialized; proceeding without mutex (unsafe)");
    }

    strncpy(mqtt_config.enable_mqtt, cJSON_IsString(enable_mqtt) ? enable_mqtt->valuestring : "", SMALL_CONFIG_ITEM - 1);
    mqtt_config.enable_mqtt[SMALL_CONFIG_ITEM - 1] = '\0';

    strncpy(mqtt_config.mqtt_server, cJSON_IsString(mqtt_server) ? mqtt_server->valuestring : "", LARGE_CONFIG_ITEM - 1);
    mqtt_config.mqtt_server[LARGE_CONFIG_ITEM - 1] = '\0';

    strncpy(mqtt_config.mqtt_port, cJSON_IsString(mqtt_port) ? mqtt_port->valuestring : "", SMALL_CONFIG_ITEM - 1);
    mqtt_config.mqtt_port[SMALL_CONFIG_ITEM - 1] = '\0';

    strncpy(mqtt_config.mqtt_base_topic, cJSON_IsString(mqtt_base_topic) ? mqtt_base_topic->valuestring : "", XLARGE_CONFIG_ITEM - 1);
    mqtt_config.mqtt_base_topic[XLARGE_CONFIG_ITEM - 1] = '\0';

    // Release sensor config mutex after updates
    if (taken)
    {
        xSemaphoreGive(mqtt_config_mutex);
    }

    cJSON_Delete(json);
    free(buffer);

    ESP_LOGI(TAG, "MQTT config loaded from %s", path);

    return ESP_OK;
}

esp_err_t load_influxdb_config(void)
{
    const char *TAG = "Config";
    char path[128] = "/data/json/influxdb_config.json";
    size_t bufferSize = XXLARGE_CONFIG_ITEM;

    ESP_LOGI(TAG, "Attempting to read InfluxDB config: %s", path);

    char *buffer = malloc(bufferSize);
    if (!buffer)
    {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = read_file(path, buffer, bufferSize);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read InfluxDB config file: %s", path);
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

    cJSON *enable_influxdb = cJSON_GetObjectItem(json, "enable_influxdb");
    cJSON *influxdb_url = cJSON_GetObjectItem(json, "influxdb_url");
    cJSON *influxdb_org = cJSON_GetObjectItem(json, "influxdb_org");
    cJSON *influxdb_bucket = cJSON_GetObjectItem(json, "influxdb_bucket");
    cJSON *influxdb_token = cJSON_GetObjectItem(json, "influxdb_token");

    // Acquire mqtt config mutex before updating global settings
    bool taken = false;
    if (influxdb_config_mutex)
    {
        if (xSemaphoreTake(influxdb_config_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
        {
            ESP_LOGE(TAG, "Failed to acquire MQTT config mutex");
            cJSON_Delete(json);
            free(buffer);
            return ESP_ERR_TIMEOUT;
        }
        taken = true;
    }
    else
    {
        ESP_LOGW(TAG, "influxdb_config_mutex not initialized; proceeding without mutex (unsafe)");
    }

    strncpy(influxdb_config.enable_influxdb, cJSON_IsString(enable_influxdb) ? enable_influxdb->valuestring : "", SMALL_CONFIG_ITEM - 1);
    influxdb_config.enable_influxdb[LARGE_CONFIG_ITEM - 1] = '\0';

    strncpy(influxdb_config.influxdb_url, cJSON_IsString(influxdb_url) ? influxdb_url->valuestring : "", XXLARGE_CONFIG_ITEM - 1);
    influxdb_config.influxdb_url[XXLARGE_CONFIG_ITEM - 1] = '\0';

    strncpy(influxdb_config.influxdb_org, cJSON_IsString(influxdb_org) ? influxdb_org->valuestring : "", LARGE_CONFIG_ITEM - 1);
    influxdb_config.influxdb_org[LARGE_CONFIG_ITEM - 1] = '\0';

    strncpy(influxdb_config.influxdb_bucket, cJSON_IsString(influxdb_bucket) ? influxdb_bucket->valuestring : "", LARGE_CONFIG_ITEM - 1);
    influxdb_config.influxdb_bucket[LARGE_CONFIG_ITEM - 1] = '\0';

    strncpy(influxdb_config.influxdb_token, cJSON_IsString(influxdb_token) ? influxdb_token->valuestring : "", XLARGE_CONFIG_ITEM - 1);
    influxdb_config.influxdb_token[XLARGE_CONFIG_ITEM - 1] = '\0';

    if (taken)
    {
        xSemaphoreGive(influxdb_config_mutex);
    }

    cJSON_Delete(json);
    free(buffer);

    ESP_LOGI(TAG, "InfluxDB config loaded from %s", path);

    return ESP_OK;
}

esp_err_t load_fan_config(void)
{
    const char *TAG = "Config";
    char path[128] = "/data/json/fan_config.json";
    size_t bufferSize = XXLARGE_CONFIG_ITEM;

    ESP_LOGI(TAG, "Attempting to read fan config: %s", path);

    char *buffer = malloc(bufferSize);
    if (!buffer)
    {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = read_file(path, buffer, bufferSize);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read fan config file: %s", path);
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

    cJSON *fan_control_mode = cJSON_GetObjectItem(json, "fan_control_mode");
    cJSON *fan_control_mqtt_server = cJSON_GetObjectItem(json, "fan_control_mqtt_server");
    cJSON *fan_control_mqtt_port = cJSON_GetObjectItem(json, "fan_control_mqtt_port");
    cJSON *fan_control_mqtt_topic = cJSON_GetObjectItem(json, "fan_control_mqtt_topic");
    cJSON *fan_control_url_hs = cJSON_GetObjectItem(json, "fan_control_url_hs");
    cJSON *fan_control_url_ms = cJSON_GetObjectItem(json, "fan_control_url_ms");
    cJSON *fan_control_url_ls = cJSON_GetObjectItem(json, "fan_control_url_ls");

    // Acquire fan control config mutex before updating global settings
    bool taken = false;
    if (fan_config_mutex)
    {
        if (xSemaphoreTake(fan_config_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
        {
            ESP_LOGE(TAG, "Failed to acquire MQTT config mutex");
            cJSON_Delete(json);
            free(buffer);
            return ESP_ERR_TIMEOUT;
        }
        taken = true;
    }
    else
    {
        ESP_LOGW(TAG, "fan_config_mutex not initialized; proceeding without mutex (unsafe)");
    }

    strncpy(fan_config.fan_control_mode, cJSON_IsString(fan_control_mode) ? fan_control_mode->valuestring : "", SMALL_CONFIG_ITEM - 1);
    fan_config.fan_control_mode[LARGE_CONFIG_ITEM - 1] = '\0';

    strncpy(fan_config.fan_control_mqtt_server, cJSON_IsString(fan_control_mqtt_server) ? fan_control_mqtt_server->valuestring : "", LARGE_CONFIG_ITEM - 1);
    fan_config.fan_control_mqtt_server[LARGE_CONFIG_ITEM - 1] = '\0';

    strncpy(fan_config.fan_control_mqtt_port, cJSON_IsString(fan_control_mqtt_port) ? fan_control_mqtt_port->valuestring : "", SMALL_CONFIG_ITEM - 1);
    fan_config.fan_control_mqtt_port[SMALL_CONFIG_ITEM - 1] = '\0';

    strncpy(fan_config.fan_control_mqtt_topic, cJSON_IsString(fan_control_mqtt_topic) ? fan_control_mqtt_topic->valuestring : "", XLARGE_CONFIG_ITEM - 1);
    fan_config.fan_control_mqtt_topic[XLARGE_CONFIG_ITEM - 1] = '\0';

    strncpy(fan_config.fan_control_url_hs, cJSON_IsString(fan_control_url_hs) ? fan_control_url_hs->valuestring : "", XLARGE_CONFIG_ITEM - 1);
    fan_config.fan_control_url_hs[XLARGE_CONFIG_ITEM - 1] = '\0';

    strncpy(fan_config.fan_control_url_ms, cJSON_IsString(fan_control_url_ms) ? fan_control_url_ms->valuestring : "", XLARGE_CONFIG_ITEM - 1);
    fan_config.fan_control_url_ms[XLARGE_CONFIG_ITEM - 1] = '\0';

    strncpy(fan_config.fan_control_url_ls, cJSON_IsString(fan_control_url_ls) ? fan_control_url_ls->valuestring : "", XLARGE_CONFIG_ITEM - 1);
    fan_config.fan_control_url_ls[XLARGE_CONFIG_ITEM - 1] = '\0';

    if (taken)
    {
        xSemaphoreGive(fan_config_mutex);
    }

    cJSON_Delete(json);
    free(buffer);

    ESP_LOGI(TAG, "Fan config loaded from %s", path);

    return ESP_OK;
}

esp_err_t load_i2c_config(void)
{
    const char *TAG = "Config";
    char path[128] = "/data/json/i2c_config.json";
    size_t bufferSize = XXLARGE_CONFIG_ITEM;

    ESP_LOGI(TAG, "Attempting to read i2c config: %s", path);

    char *buffer = malloc(bufferSize);
    if (!buffer)
    {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = read_file(path, buffer, bufferSize);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read i2c config file: %s", path);
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

    cJSON *bus0_multiplexer_address = cJSON_GetObjectItem(json, "bus0_multiplexer_address");
    cJSON *bus1_multiplexer_address = cJSON_GetObjectItem(json, "bus1_multiplexer_address");
    cJSON *enable_lcd = cJSON_GetObjectItem(json, "enable_lcd");
    // cJSON *lcd_i2c_address = cJSON_GetObjectItem(json, "lcd_i2c_address");

    // Acquire mqtt config mutex before updating global settings
    bool taken = false;
    if (i2c_config_mutex)
    {
        if (xSemaphoreTake(i2c_config_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
        {
            ESP_LOGE(TAG, "Failed to acquire I2C config mutex");
            cJSON_Delete(json);
            free(buffer);
            return ESP_ERR_TIMEOUT;
        }
        taken = true;
    }
    else
    {
        ESP_LOGW(TAG, "i2c_config_mutex not initialized; proceeding without mutex (unsafe)");
    }

    strncpy(i2c_config.bus0_multiplexer_address, cJSON_IsString(bus0_multiplexer_address) ? bus0_multiplexer_address->valuestring : "", SMALL_CONFIG_ITEM - 1);
    i2c_config.bus0_multiplexer_address[SMALL_CONFIG_ITEM - 1] = '\0';

    strncpy(i2c_config.bus1_multiplexer_address, cJSON_IsString(bus1_multiplexer_address) ? bus1_multiplexer_address->valuestring : "", SMALL_CONFIG_ITEM - 1);
    i2c_config.bus1_multiplexer_address[SMALL_CONFIG_ITEM - 1] = '\0';

    strncpy(i2c_config.enable_lcd, cJSON_IsString(enable_lcd) ? enable_lcd->valuestring : "", SMALL_CONFIG_ITEM - 1);
    i2c_config.enable_lcd[SMALL_CONFIG_ITEM - 1] = '\0';

    // strncpy(i2c_config.lcd_i2c_address, cJSON_IsString(lcd_i2c_address) ? lcd_i2c_address->valuestring : "", SMALL_CONFIG_ITEM - 1);
    // i2c_config.lcd_i2c_address[SMALL_CONFIG_ITEM - 1] = '\0';

    if (taken)
    {
        xSemaphoreGive(i2c_config_mutex);
    }

    cJSON_Delete(json);
    free(buffer);

    ESP_LOGI(TAG, "I2C config loaded from %s", path);

    return ESP_OK;
}

esp_err_t load_syslog_config(void)
{
    const char *TAG = "Config";
    char path[128] = "/data/json/syslog_config.json";
    size_t bufferSize = XLARGE_CONFIG_ITEM;

    ESP_LOGI(TAG, "Attempting to read syslog config: %s", path);

    char *buffer = malloc(bufferSize);
    if (!buffer)
    {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = read_file(path, buffer, bufferSize);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read syslog config file: %s", path);
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

    cJSON *enable_syslog = cJSON_GetObjectItem(json, "enable_syslog");
    cJSON *syslog_ip = cJSON_GetObjectItem(json, "syslog_ip");
    cJSON *syslog_port = cJSON_GetObjectItem(json, "syslog_port");

    // Acquire mqtt config mutex before updating global settings
    bool taken = false;
    if (syslog_config_mutex)
    {
        if (xSemaphoreTake(syslog_config_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
        {
            ESP_LOGE(TAG, "Failed to acquire syslog config mutex");
            cJSON_Delete(json);
            free(buffer);
            return ESP_ERR_TIMEOUT;
        }
        taken = true;
    }
    else
    {
        ESP_LOGW(TAG, "syslog_config_mutex not initialized; proceeding without mutex (unsafe)");
    }

    strncpy(syslog_config.enable_syslog, cJSON_IsString(enable_syslog) ? enable_syslog->valuestring : "", SMALL_CONFIG_ITEM - 1);
    syslog_config.enable_syslog[SMALL_CONFIG_ITEM - 1] = '\0';

    strncpy(syslog_config.syslog_ip, cJSON_IsString(syslog_ip) ? syslog_ip->valuestring : "", MEDIUM_CONFIG_ITEM - 1);
    syslog_config.syslog_ip[MEDIUM_CONFIG_ITEM - 1] = '\0';

    strncpy(syslog_config.syslog_port, cJSON_IsString(syslog_port) ? syslog_port->valuestring : "", SMALL_CONFIG_ITEM - 1);
    syslog_config.syslog_port[SMALL_CONFIG_ITEM - 1] = '\0';

    if (taken)
    {
        xSemaphoreGive(syslog_config_mutex);
    }

    cJSON_Delete(json);
    free(buffer);

    ESP_LOGI(TAG, "Syslog config loaded from %s", path);

    return ESP_OK;
}

// Load statemachine settings
esp_err_t load_statemachine_general_config(void)
{
    const char *TAG = "Config";
    char path[128] = "/data/json/sm_general_config.json";
    size_t bufferSize = XLARGE_CONFIG_ITEM;

    ESP_LOGI(TAG, "Attempting to read general statemachine config: %s", path);

    char *buffer = malloc(bufferSize);
    if (!buffer)
    {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = read_file(path, buffer, bufferSize);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read statemachine general config file: %s", path);
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

    cJSON *wday_d_h_start = cJSON_GetObjectItem(json, "wday_d_h_start");
    cJSON *wday_d_m_start = cJSON_GetObjectItem(json, "wday_d_m_start");
    cJSON *wday_n_h_start = cJSON_GetObjectItem(json, "wday_n_d_h_start");
    cJSON *wday_n_m_start = cJSON_GetObjectItem(json, "wday_n_d_m_start");
    cJSON *wkend_d_h_start = cJSON_GetObjectItem(json, "wkend_d_h_start");
    cJSON *wkend_d_m_start = cJSON_GetObjectItem(json, "wkend_d_m_start");
    cJSON *wkend_n_h_start = cJSON_GetObjectItem(json, "wkend_n_h_start");
    cJSON *wkend_n_m_start = cJSON_GetObjectItem(json, "wkend_n_m_start");
    cJSON *wkend_d1 = cJSON_GetObjectItem(json, "wkend_d1");
    cJSON *wkend_d2 = cJSON_GetObjectItem(json, "wkend_d2");

    // Acquire time config mutex before updating global settings
    bool taken = false;
    if (statemachine_config_mutex)
    {
        if (xSemaphoreTake(statemachine_config_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
        {
            ESP_LOGE(TAG, "Failed to acquire statemachine config mutex");
            cJSON_Delete(json);
            free(buffer);
            return ESP_ERR_TIMEOUT;
        }
        taken = true;
    }
    else
    {
        ESP_LOGW(TAG, "statemachine_config_mutex not initialized; proceeding without mutex (unsafe)");
    }

    if (cJSON_IsNumber(wday_d_h_start))
        statemachine_gen_config.wday_d_h_start = wday_d_h_start->valueint;
    if (cJSON_IsNumber(wday_d_m_start))
        statemachine_gen_config.wday_d_m_start = wday_d_m_start->valueint;
    if (cJSON_IsNumber(wday_n_h_start))
        statemachine_gen_config.wday_n_h_start = wday_n_h_start->valueint;
    if (cJSON_IsNumber(wday_n_m_start))
        statemachine_gen_config.wday_n_m_start = wday_n_m_start->valueint;
    if (cJSON_IsNumber(wkend_d_h_start))
        statemachine_gen_config.wkend_d_h_start = wkend_d_h_start->valueint;
    if (cJSON_IsNumber(wkend_d_m_start))
        statemachine_gen_config.wkend_d_m_start = wkend_d_m_start->valueint;
    if (cJSON_IsNumber(wkend_n_h_start))
        statemachine_gen_config.wkend_n_h_start = wkend_n_h_start->valueint;
    if (cJSON_IsNumber(wkend_n_m_start))
        statemachine_gen_config.wkend_n_m_start = wkend_n_m_start->valueint;

    strncpy(statemachine_gen_config.wkend_d1, cJSON_IsString(wkend_d1) ? wkend_d1->valuestring : "", SMALL_CONFIG_ITEM - 1);
    statemachine_gen_config.wkend_d1[SMALL_CONFIG_ITEM - 1] = '\0';
    strncpy(statemachine_gen_config.wkend_d2, cJSON_IsString(wkend_d2) ? wkend_d2->valuestring : "", SMALL_CONFIG_ITEM - 1);
    statemachine_gen_config.wkend_d2[SMALL_CONFIG_ITEM - 1] = '\0';

    // Release sensor config mutex after updates
    if (taken)
    {
        xSemaphoreGive(statemachine_config_mutex);
    }

    cJSON_Delete(json);
    free(buffer);

    ESP_LOGI(TAG, "Statemachine general config loaded from %s", path);

    return ESP_OK;
}