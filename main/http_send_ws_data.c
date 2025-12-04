#include "http_send_ws_data.h"

// Functions to send configuration as JSON via WebSocket

static const char *TAG = "http json ws";

void send_index_html_ws(void)
{
    // Sensor data array to contain data from queue
    double snapshot[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS];
    char key[64];
    char buf[16];

    cJSON *root = cJSON_CreateObject();

    if (!root)
        return;

    // --- Sensors ---
    if (sensor_queue != NULL)
    {
        if (xQueuePeek(sensor_queue, &snapshot, portMAX_DELAY))
        {
            for (int bus = 0; bus < MAX_I2C_BUSES; bus++)
            {
                for (int ch = 0; ch < CHANNELS_PER_BUS; ch++)
                {
                    snprintf(key, sizeof(key), "b%ds%d_temp", bus, ch);
                    snprintf(buf, sizeof(buf), "%.2f", snapshot[bus][ch][0]);
                    cJSON_AddStringToObject(root, key, buf);
                    snprintf(key, sizeof(key), "b%ds%d_hum", bus, ch);
                    snprintf(buf, sizeof(buf), "%.2f", snapshot[bus][ch][1]);
                    cJSON_AddStringToObject(root, key, buf);
                    snprintf(key, sizeof(key), "b%ds%d_co2", bus, ch);
                    snprintf(buf, sizeof(buf), "%.2f", snapshot[bus][ch][2]);
                    cJSON_AddStringToObject(root, key, buf);
                }
            }
        }
    }
    else
    {
        ESP_LOGE(TAG, "Sensor queue is NULL");
    }

    if (sensor_cfg_mutex)
    {
        if (xSemaphoreTake(sensor_cfg_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            // code to read sensor config from struct into json
            for (int ch = 0; ch < CHANNELS_PER_BUS; ch++)
            {
                snprintf(key, sizeof(key), "b0s%d_type", ch);
                cJSON_AddStringToObject(root, key, bus0_sensor_settings[ch].type);
                snprintf(key, sizeof(key), "b0s%d_valve", ch);
                cJSON_AddStringToObject(root, key, bus0_sensor_settings[ch].valve);
                snprintf(key, sizeof(key), "b0s%d_location", ch);
                cJSON_AddStringToObject(root, key, bus0_sensor_settings[ch].location);
                snprintf(key, sizeof(key), "b0s%d_rhs", ch);
                cJSON_AddBoolToObject(root, key, bus0_sensor_settings[ch].rh_enabled);
                snprintf(key, sizeof(key), "b0s%d_co2s", ch);
                cJSON_AddBoolToObject(root, key, bus0_sensor_settings[ch].co2_enabled);

                snprintf(key, sizeof(key), "b1s%d_type", ch);
                cJSON_AddStringToObject(root, key, bus1_sensor_settings[ch].type);
                snprintf(key, sizeof(key), "b1s%d_valve", ch);
                cJSON_AddStringToObject(root, key, bus0_sensor_settings[ch].valve);
                snprintf(key, sizeof(key), "b1s%d_location", ch);
                cJSON_AddStringToObject(root, key, bus0_sensor_settings[ch].location);
                snprintf(key, sizeof(key), "b1s%d_rhs", ch);
                cJSON_AddBoolToObject(root, key, bus0_sensor_settings[ch].rh_enabled);
                snprintf(key, sizeof(key), "b1s%d_co2s", ch);
                cJSON_AddBoolToObject(root, key, bus0_sensor_settings[ch].co2_enabled);
            }
            xSemaphoreGive(sensor_cfg_mutex);
        }
        else
        {
            ESP_LOGW(TAG, "Failed to take sensor_cfg_mutex for sending index HTML WS data");
        }

        char *out = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);

        if (out)
        {
            ws_async_send(out, strlen(out));
            free(out);
        }
    }
}

void send_sensor_config_json_ws(void)
{
    cJSON *root = cJSON_CreateObject();
    if (!root)
        return;

    char key[64];
    for (int bus = 0; bus < MAX_I2C_BUSES; ++bus)
    {
        bus_sensor_t *cfg = (bus == 0) ? bus0_sensor_settings : bus1_sensor_settings;

        for (int ch = 0; ch < CHANNELS_PER_BUS; ++ch)
        {
            const char *type = (cfg[ch].type[0] != '\0') ? cfg[ch].type : "None";
            const char *valve = (cfg[ch].valve[0] != '\0') ? cfg[ch].valve : "None";
            const char *loc = (cfg[ch].location[0] != '\0') ? cfg[ch].location : "";
            const char *rh = cfg[ch].rh_enabled ? "On" : "Off";
            const char *co2 = cfg[ch].co2_enabled ? "On" : "Off";

            snprintf(key, sizeof(key), "b%d_s%d_type", bus, ch);
            cJSON_AddStringToObject(root, key, type);

            snprintf(key, sizeof(key), "b%d_s%d_valve", bus, ch);
            cJSON_AddStringToObject(root, key, valve);

            snprintf(key, sizeof(key), "b%d_s%d_location", bus, ch);
            cJSON_AddStringToObject(root, key, loc);

            snprintf(key, sizeof(key), "b%d_s%d_rh", bus, ch);
            cJSON_AddStringToObject(root, key, rh);

            snprintf(key, sizeof(key), "b%d_s%d_co2", bus, ch);
            cJSON_AddStringToObject(root, key, co2);
        }
    }

    char *out = cJSON_PrintUnformatted(root);
    if (out)
    {
        ws_async_send(out, strlen(out));
        free(out);
    }

    cJSON_Delete(root);
}

void send_config_json_ws(void)
{
    cJSON *root = cJSON_CreateObject();
    if (!root)
        return;

    // --- Network ---
    if (xSemaphoreTake(network_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        cJSON_AddStringToObject(root, "enable_dhcp", network_config.enable_dhcp);
        cJSON_AddStringToObject(root, "ssid", network_config.ssid);
        cJSON_AddStringToObject(root, "wifi_password", network_config.wifi_password);
        cJSON_AddStringToObject(root, "ip_address", network_config.ip_address);
        cJSON_AddStringToObject(root, "subnet_mask", network_config.subnet_mask);
        cJSON_AddStringToObject(root, "gateway", network_config.gateway);
        cJSON_AddStringToObject(root, "primary_dns", network_config.primary_dns);
        cJSON_AddStringToObject(root, "secondary_dns", network_config.secondary_dns);
        xSemaphoreGive(network_config_mutex);
    }

    // --- Time ---
    if (xSemaphoreTake(time_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        cJSON_AddStringToObject(root, "ntp_server", time_config.ntp_server);
        cJSON_AddStringToObject(root, "timezone", time_config.timezone);
        xSemaphoreGive(time_config_mutex);
    }

    // --- MQTT ---
    if (xSemaphoreTake(mqtt_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        cJSON_AddStringToObject(root, "enable_mqtt", mqtt_config.enable_mqtt);
        cJSON_AddStringToObject(root, "mqtt_server", mqtt_config.mqtt_server);
        cJSON_AddStringToObject(root, "mqtt_port", mqtt_config.mqtt_port);
        cJSON_AddStringToObject(root, "mqtt_base_topic", mqtt_config.mqtt_base_topic);
        xSemaphoreGive(mqtt_config_mutex);
    }

    // --- InfluxDB ---
    if (xSemaphoreTake(influxdb_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        cJSON_AddStringToObject(root, "enable_influxdb", influxdb_config.enable_influxdb);
        cJSON_AddStringToObject(root, "influxdb_url", influxdb_config.influxdb_url);
        cJSON_AddStringToObject(root, "influxdb_org", influxdb_config.influxdb_org);
        cJSON_AddStringToObject(root, "influxdb_bucket", influxdb_config.influxdb_bucket);
        cJSON_AddStringToObject(root, "influxdb_token", influxdb_config.influxdb_token);
        xSemaphoreGive(influxdb_config_mutex);
    }

    // --- Fan ---
    if (xSemaphoreTake(fan_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        cJSON_AddStringToObject(root, "fan_control_mode", fan_config.fan_control_mode);
        cJSON_AddStringToObject(root, "fan_control_mqtt_server", fan_config.fan_control_mqtt_server);
        cJSON_AddStringToObject(root, "fan_control_mqtt_port", fan_config.fan_control_mqtt_port);
        cJSON_AddStringToObject(root, "fan_control_mqtt_topic", fan_config.fan_control_mqtt_topic);
        cJSON_AddStringToObject(root, "fan_control_url_hs", fan_config.fan_control_url_hs);
        cJSON_AddStringToObject(root, "fan_control_url_ms", fan_config.fan_control_url_ms);
        cJSON_AddStringToObject(root, "fan_control_url_ls", fan_config.fan_control_url_ls);
        xSemaphoreGive(fan_config_mutex);
    }

    // --- I2C ---
    if (xSemaphoreTake(i2c_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        cJSON_AddStringToObject(root, "bus0_multiplexer_address", i2c_config.bus0_multiplexer_address);
        cJSON_AddStringToObject(root, "bus1_multiplexer_address", i2c_config.bus1_multiplexer_address);
        cJSON_AddStringToObject(root, "enable_lcd", i2c_config.enable_lcd);
        cJSON_AddStringToObject(root, "lcd_i2c_address", i2c_config.lcd_i2c_address);
        xSemaphoreGive(i2c_config_mutex);
    }

    // --- Syslog ---
    if (xSemaphoreTake(syslog_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        cJSON_AddStringToObject(root, "enable_syslog", syslog_config.enable_syslog);
        cJSON_AddStringToObject(root, "syslog_ip", syslog_config.syslog_ip);
        cJSON_AddStringToObject(root, "syslog_port", syslog_config.syslog_port);
        xSemaphoreGive(syslog_config_mutex);
    }

    // Serialize to string
    char *out = cJSON_PrintUnformatted(root);
    ESP_LOGI(TAG, "Config JSON: %s", out ? out : "null");
    cJSON_Delete(root);

    if (out)
    {
        ws_async_send(out, strlen(out));
        free(out);
    }
}
