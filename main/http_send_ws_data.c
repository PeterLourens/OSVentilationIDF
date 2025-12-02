#include "http_send_ws_data.h"

// Functions to send configuration as JSON via WebSocket

static const char *TAG = "http json ws";

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
        cJSON *net = cJSON_CreateObject();
        cJSON_AddStringToObject(net, "enable_dhcp", network_config.enable_dhcp);
        cJSON_AddStringToObject(net, "ssid", network_config.ssid);
        cJSON_AddStringToObject(net, "wifi_password", network_config.wifi_password);
        cJSON_AddStringToObject(net, "ip_address", network_config.ip_address);
        cJSON_AddStringToObject(net, "subnet_mask", network_config.subnet_mask);
        cJSON_AddStringToObject(net, "gateway", network_config.gateway);
        cJSON_AddStringToObject(net, "primary_dns", network_config.primary_dns);
        cJSON_AddStringToObject(net, "secondary_dns", network_config.secondary_dns);
        cJSON_AddItemToObject(root, "network", net);
        xSemaphoreGive(network_config_mutex);
    }

    // --- Time ---
    if (xSemaphoreTake(time_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        cJSON *time = cJSON_CreateObject();
        cJSON_AddStringToObject(time, "ntp_server", time_config.ntp_server);
        cJSON_AddStringToObject(time, "timezone", time_config.timezone);
        cJSON_AddItemToObject(root, "time", time);
        xSemaphoreGive(time_config_mutex);
    }

    // --- MQTT ---
    if (xSemaphoreTake(mqtt_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        cJSON *mqtt = cJSON_CreateObject();
        cJSON_AddStringToObject(mqtt, "enable_mqtt", mqtt_config.enable_mqtt);
        cJSON_AddStringToObject(mqtt, "mqtt_server", mqtt_config.mqtt_server);
        cJSON_AddStringToObject(mqtt, "mqtt_port", mqtt_config.mqtt_port);
        cJSON_AddStringToObject(mqtt, "mqtt_base_topic", mqtt_config.mqtt_base_topic);
        cJSON_AddItemToObject(root, "mqtt", mqtt);
        xSemaphoreGive(mqtt_config_mutex);
    }

    // --- InfluxDB ---
    if (xSemaphoreTake(influxdb_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        cJSON *influx = cJSON_CreateObject();
        cJSON_AddStringToObject(influx, "enable_influxdb", influxdb_config.enable_influxdb);
        cJSON_AddStringToObject(influx, "influxdb_url", influxdb_config.influxdb_url);
        cJSON_AddStringToObject(influx, "influxdb_org", influxdb_config.influxdb_org);
        cJSON_AddStringToObject(influx, "influxdb_bucket", influxdb_config.influxdb_bucket);
        cJSON_AddStringToObject(influx, "influxdb_token", influxdb_config.influxdb_token);
        cJSON_AddItemToObject(root, "influxdb", influx);
        xSemaphoreGive(influxdb_config_mutex);
    }

    // --- Fan ---
    if (xSemaphoreTake(fan_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        cJSON *fan = cJSON_CreateObject();
        cJSON_AddStringToObject(fan, "fan_control_mode", fan_config.fan_control_mode);
        cJSON_AddStringToObject(fan, "fan_control_mqtt_server", fan_config.fan_control_mqtt_server);
        cJSON_AddStringToObject(fan, "fan_control_mqtt_port", fan_config.fan_control_mqtt_port);
        cJSON_AddStringToObject(fan, "fan_control_mqtt_topic", fan_config.fan_control_mqtt_topic);
        cJSON_AddStringToObject(fan, "fan_control_url_hs", fan_config.fan_control_url_hs);
        cJSON_AddStringToObject(fan, "fan_control_url_ms", fan_config.fan_control_url_ms);
        cJSON_AddStringToObject(fan, "fan_control_url_ls", fan_config.fan_control_url_ls);
        cJSON_AddItemToObject(root, "fan", fan);
        xSemaphoreGive(fan_config_mutex);
    }

    // --- I2C ---
    if (xSemaphoreTake(i2c_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        cJSON *i2c = cJSON_CreateObject();
        cJSON_AddStringToObject(i2c, "bus0_multiplexer_address", i2c_config.bus0_multiplexer_address);
        cJSON_AddStringToObject(i2c, "bus1_multiplexer_address", i2c_config.bus1_multiplexer_address);
        cJSON_AddStringToObject(i2c, "enable_lcd", i2c_config.enable_lcd);
        cJSON_AddStringToObject(i2c, "lcd_i2c_address", i2c_config.lcd_i2c_address);
        cJSON_AddItemToObject(root, "i2c", i2c);
        xSemaphoreGive(i2c_config_mutex);
    }

    // --- Syslog ---
    if (xSemaphoreTake(syslog_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        cJSON *syslog = cJSON_CreateObject();
        cJSON_AddStringToObject(syslog, "enable_syslog", syslog_config.enable_syslog);
        cJSON_AddStringToObject(syslog, "syslog_ip", syslog_config.syslog_ip);
        cJSON_AddStringToObject(syslog, "syslog_port", syslog_config.syslog_port);
        cJSON_AddItemToObject(root, "syslog", syslog);
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