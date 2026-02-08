/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include "http_send_ws_data.h"
#include "statemachine_states.h"

// Functions to send configuration as JSON via WebSocket

static const char *TAG = "HTTP_JSON_ws";

void send_index_html_ws(void)
{
    // Sensor data array to contain data from queue
    float snapshot[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS];
    char key[64];
    char buf[16];
    char uptime[32];
    char date_time[64];

    formatted_uptime(uptime, sizeof(uptime));
    formatted_daydatetime(date_time, sizeof(date_time));

    cJSON *root = cJSON_CreateObject();

    if (!root)
        return;

    // --- Valves ---
    nvs_handle_t nvs;
    int32_t valve_pos = 0;

    esp_err_t err = nvs_open("valves", NVS_READONLY, &nvs);

    if (err == ESP_OK)
    {
        for (int i = 0; i < 12; i++)
        {
            char key[16];
            snprintf(key, sizeof(key), "valve%d", i);

            err = nvs_get_i32(nvs, key, &valve_pos);
            if (err == ESP_OK)
            {
                snprintf(buf, sizeof(buf), "%li", valve_pos);
                cJSON_AddStringToObject(root, key, buf);
            }
            else
            {
                cJSON_AddStringToObject(root, key, "N/A");
            }
        }
        nvs_close(nvs);
    }
    else
    {
        LOGE(TAG, "NVS open failed for send_index_html_ws: %d", err);
    }

    // --- Sensors ---
    if (sensor_avg_queue != NULL)
    {
        if (xQueuePeek(sensor_avg_queue, snapshot, portMAX_DELAY))
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
        LOGE(TAG, "Sensor queue is NULL");
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
                cJSON_AddStringToObject(root, key, bus1_sensor_settings[ch].valve);
                snprintf(key, sizeof(key), "b1s%d_location", ch);
                cJSON_AddStringToObject(root, key, bus1_sensor_settings[ch].location);
                snprintf(key, sizeof(key), "b1s%d_rhs", ch);
                cJSON_AddBoolToObject(root, key, bus1_sensor_settings[ch].rh_enabled);
                snprintf(key, sizeof(key), "b1s%d_co2s", ch);
                cJSON_AddBoolToObject(root, key, bus1_sensor_settings[ch].co2_enabled);
            }
            xSemaphoreGive(sensor_cfg_mutex);
        }
        else
        {
            LOGE(TAG, "Failed to take sensor_cfg_mutex for sending index HTML WS data");
        }

        if (statemachine_mutex)
        {
            if (xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
            {
                cJSON_AddStringToObject(root, "statemachine_state", state_nr_to_str(sm_actuals.state));
                cJSON_AddStringToObject(root, "fanspeed", fan_speed_to_str(sm_actuals.fanspeed));
                cJSON_AddNumberToObject(root, "co2_sensors_high", sm_actuals.co2_sensors_high);
                cJSON_AddNumberToObject(root, "rh_sensors_high", sm_actuals.rh_sensors_high);
            }
            xSemaphoreGive(statemachine_mutex);
        }

        cJSON_AddStringToObject(root, "uptime", uptime);
        cJSON_AddStringToObject(root, "date_time", date_time);

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
        cJSON_AddStringToObject(root, "hostname", network_config.hostname);
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
        // cJSON_AddStringToObject(root, "lcd_i2c_address", i2c_config.lcd_i2c_address);
        xSemaphoreGive(i2c_config_mutex);
    }

    // --- Syslog ---
    if (xSemaphoreTake(syslog_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        cJSON_AddStringToObject(root, "enable_syslog", syslog_config.enable_syslog);
        cJSON_AddStringToObject(root, "syslog_ip", syslog_config.syslog_ip);
        cJSON_AddStringToObject(root, "syslog_port", syslog_config.syslog_port);
        cJSON_AddStringToObject(root, "syslog_hostname", syslog_config.syslog_hostname);
        cJSON_AddStringToObject(root, "syslog_appname", syslog_config.syslog_appname);
        cJSON_AddStringToObject(root, "syslog_device_id", syslog_config.syslog_device_id);
        xSemaphoreGive(syslog_config_mutex);
    }

    // Serialize to string
    char *out = cJSON_PrintUnformatted(root);
    LOGD(TAG, "Config JSON: %s", out ? out : "null");
    cJSON_Delete(root);

    if (out)
    {
        ws_async_send(out, strlen(out));
        free(out);
    }
}

void send_statemachine_config_json_ws(void)
{
    cJSON *root = cJSON_CreateObject();
    char key[32];
    char fanspeed_val[16];

    if (!root)
        return;

    // --- General settings ---
    if (xSemaphoreTake(state_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        cJSON_AddNumberToObject(root, "wday_d_h_start", statemachine_gen_config.wday_d_h_start);
        cJSON_AddNumberToObject(root, "wday_d_m_start", statemachine_gen_config.wday_d_m_start);
        cJSON_AddNumberToObject(root, "wday_n_h_start", statemachine_gen_config.wday_n_h_start);
        cJSON_AddNumberToObject(root, "wday_n_m_start", statemachine_gen_config.wday_n_m_start);
        cJSON_AddNumberToObject(root, "wkend_d_h_start", statemachine_gen_config.wkend_d_h_start);
        cJSON_AddNumberToObject(root, "wkend_d_m_start", statemachine_gen_config.wkend_d_m_start);
        cJSON_AddNumberToObject(root, "wkend_n_h_start", statemachine_gen_config.wkend_n_h_start);
        cJSON_AddNumberToObject(root, "wkend_n_m_start", statemachine_gen_config.wkend_n_m_start);
        cJSON_AddStringToObject(root, "wkend_d1", statemachine_gen_config.wkend_d1);
        cJSON_AddStringToObject(root, "wkend_d2", statemachine_gen_config.wkend_d2);
        cJSON_AddNumberToObject(root, "rh_l", statemachine_gen_config.rh_low_level);
        cJSON_AddNumberToObject(root, "rh_h", statemachine_gen_config.rh_high_level);
        cJSON_AddNumberToObject(root, "co2_l", statemachine_gen_config.co2_low_level);
        cJSON_AddNumberToObject(root, "co2_h", statemachine_gen_config.co2_high_level);

        // other state settings go here
        for (int i = 0; i < MAX_NR_STATES; i++)
        {
            snprintf(key, sizeof(key), "s%d_en", i);
            cJSON_AddBoolToObject(root, key, statemachine_config[i].enable_state);

            // Fanspeed is stored as int so requires conversion to string
            snprintf(key, sizeof(key), "s%d_fanspeed", i);
            if (statemachine_config[i].fanspeed == 1)
                snprintf(fanspeed_val, sizeof(fanspeed_val), "Low");
            else if (statemachine_config[i].fanspeed == 2)
                snprintf(fanspeed_val, sizeof(fanspeed_val), "Medium");
            else if (statemachine_config[i].fanspeed == 3)
                snprintf(fanspeed_val, sizeof(fanspeed_val), "High");
            else
                snprintf(fanspeed_val, sizeof(fanspeed_val), "0");
            cJSON_AddStringToObject(root, key, fanspeed_val);

            snprintf(key, sizeof(key), "s%d_name", i);
            cJSON_AddStringToObject(root, key, statemachine_config[i].state_name);
            snprintf(key, sizeof(key), "s%d_st_h", i);
            cJSON_AddNumberToObject(root, key, statemachine_config[i].state_start_hour);
            snprintf(key, sizeof(key), "s%d_st_m", i);
            cJSON_AddNumberToObject(root, key, statemachine_config[i].state_start_minute);
            snprintf(key, sizeof(key), "s%d_end_h", i);
            cJSON_AddNumberToObject(root, key, statemachine_config[i].state_end_hour);
            snprintf(key, sizeof(key), "s%d_end_m", i);
            cJSON_AddNumberToObject(root, key, statemachine_config[i].state_end_minute);
            snprintf(key, sizeof(key), "s%d_dur_m", i);
            cJSON_AddNumberToObject(root, key, statemachine_config[i].duration_minutes);
            snprintf(key, sizeof(key), "s%d_cycl_int_m", i);
            cJSON_AddNumberToObject(root, key, statemachine_config[i].cycling_interval_min);

            for (int j = 0; j < MAX_VALVE_COUNT; j++)
            {
                snprintf(key, sizeof(key), "s%d_v%d_pos", i, j);
                cJSON_AddNumberToObject(root, key, statemachine_config[i].valve_position[j]);
            }
        }
        xSemaphoreGive(state_config_mutex);
    }

    // Serialize to string
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (out)
    {
        ws_async_send(out, strlen(out));
        free(out);
    }
}
