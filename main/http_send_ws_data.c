/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include "http_send_ws_data.h"
#include "statemachine_states.h"

static const char *TAG = "http_send_ws_data";

static uint32_t ws_msg_seq = 0u;

// optional per-connection rate limiting helpers (enable with ENABLE_PER_CONN_RATE_LIMIT)
static struct
{
    httpd_req_t *req;
    uint64_t last_us;
} stats_client_last[MAX_WS_CLIENTS_TRACK];

static uint64_t client_last_us(httpd_req_t *req)
{
    for (int i = 0; i < MAX_WS_CLIENTS_TRACK; ++i)
    {
        if (stats_client_last[i].req == req)
            return stats_client_last[i].last_us;
    }
    return 0;
}

static void client_set_last_us(httpd_req_t *req, uint64_t us)
{
    for (int i = 0; i < MAX_WS_CLIENTS_TRACK; ++i)
    {
        if (stats_client_last[i].req == req || stats_client_last[i].req == NULL)
        {
            stats_client_last[i].req = req;
            stats_client_last[i].last_us = us;
            return;
        }
    }
    // fallback overwrite
    int idx = (int)(us % MAX_WS_CLIENTS_TRACK);
    stats_client_last[idx].req = req;
    stats_client_last[idx].last_us = us;
}

// helper: send a cJSON object to the requesting websocket connection (req).
// Falls back to ws_async_send() if req is NULL or direct send fails.
static void send_cjson_to_req_and_free(cJSON *obj, httpd_req_t *req)
{
    if (!obj)
        return;

    char *out = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);

    if (!out)
    {
        LOGW(TAG, "send_cjson_to_req_and_free: cJSON_PrintUnformatted returned NULL");
        return;
    }

    if (req != NULL)
    {
        httpd_ws_frame_t out_pkt;
        memset(&out_pkt, 0, sizeof(out_pkt));
        out_pkt.type = HTTPD_WS_TYPE_TEXT;
        out_pkt.payload = (uint8_t *)out;
        out_pkt.len = strlen(out);

        esp_err_t r = httpd_ws_send_frame(req, &out_pkt);
        if (r != ESP_OK)
        {
            LOGW(TAG, "httpd_ws_send_frame failed: %d - falling back to broadcast", r);
            ws_async_send(out, strlen(out));
        }
        else
        {
            LOGD(TAG, "Sent %u bytes to requesting client", (unsigned)out_pkt.len);
        }
    }
    else
    {
        ws_async_send(out, strlen(out));
    }

    free(out);
}

// Return the latest CPU temperature sampled by the sys_stats task.
float get_cpu_temp_c(void)
{
    if (sm_actuals.cpu_temp >= 0)
        return (float)sm_actuals.cpu_temp;
    return 0.0f;
}

void send_index_html_ws(httpd_req_t *req)
{
    char key[64];
    char buf[32];
    char uptime[32];
    char date_time[64];

    formatted_uptime(uptime, sizeof(uptime));
    formatted_daydatetime(date_time, sizeof(date_time));

    cJSON *root = cJSON_CreateObject();
    if (!root)
    {
        LOGE(TAG, "cJSON_CreateObject failed");
        return;
    }

    // --- Valves ---
    nvs_handle_t nvs;
    if (nvs_open("valves", NVS_READONLY, &nvs) == ESP_OK)
    {
        for (int i = 0; i < 12; ++i)
        {
            int32_t valve_pos = 0;
            snprintf(key, sizeof(key), "valve%d", i);
            if (nvs_get_i32(nvs, key, &valve_pos) == ESP_OK)
            {
                snprintf(buf, sizeof(buf), "%ld", (long)valve_pos);
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
        LOGE(TAG, "NVS open failed for send_index_html_ws");
    }

    // --- Sensors ---
    // Wait briefly for queues to be ready (avoid blocking forever in HTTP handler)
    const TickType_t wait_ticks = pdMS_TO_TICKS(200);
    TickType_t start = xTaskGetTickCount();
    while (!sensors_queues_ready && (xTaskGetTickCount() - start) < wait_ticks)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (sensors_queues_ready && sensor_avg_queue != NULL)
    {
        // Sanity-check item size
        if (sensor_avg_item_size == sizeof(float[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS]))
        {
            float snapshot[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS];
            if (xQueuePeek(sensor_avg_queue, snapshot, pdMS_TO_TICKS(50)) == pdTRUE)
            {
                for (int bus = 0; bus < MAX_I2C_BUSES; ++bus)
                {
                    for (int ch = 0; ch < CHANNELS_PER_BUS; ++ch)
                    {
                        snprintf(key, sizeof(key), "b%ds%d_temp", bus, ch);
                        snprintf(buf, sizeof(buf), "%.2f", snapshot[bus][ch][IDX_TEMP]);
                        cJSON_AddStringToObject(root, key, buf);

                        snprintf(key, sizeof(key), "b%ds%d_hum", bus, ch);
                        snprintf(buf, sizeof(buf), "%.2f", snapshot[bus][ch][IDX_HUMIDITY]);
                        cJSON_AddStringToObject(root, key, buf);

                        snprintf(key, sizeof(key), "b%ds%d_co2", bus, ch);
                        snprintf(buf, sizeof(buf), "%.2f", snapshot[bus][ch][IDX_CO2]);
                        cJSON_AddStringToObject(root, key, buf);
                    }
                }
            }
            else
            {
                LOGW(TAG, "send_index_html_ws: sensor_avg_queue peek failed");
            }
        }
        else
        {
            LOGW(TAG, "send_index_html_ws: sensor_avg_item_size mismatch");
        }
    }
    else
    {
        LOGW(TAG, "send_index_html_ws: sensor queues not ready or NULL");
    }

    // --- Sensor config and statemachine (take mutexes individually) ---
    if (sensor_cfg_mutex && xSemaphoreTake(sensor_cfg_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        for (int ch = 0; ch < CHANNELS_PER_BUS; ++ch)
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
        LOGW(TAG, "send_index_html_ws: could not take sensor_cfg_mutex");
    }

    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        cJSON_AddStringToObject(root, "statemachine_state", state_nr_to_str(sm_actuals.state));
        cJSON_AddStringToObject(root, "fanspeed", fan_speed_to_str(sm_actuals.fanspeed));
        cJSON_AddNumberToObject(root, "co2_sensors_high", sm_actuals.co2_sensors_high);
        cJSON_AddNumberToObject(root, "rh_sensors_high", sm_actuals.rh_sensors_high);
        xSemaphoreGive(statemachine_mutex);
    }
    else
    {
        LOGW(TAG, "send_index_html_ws: could not take statemachine_mutex");
    }

    cJSON_AddStringToObject(root, "uptime", uptime);
    cJSON_AddStringToObject(root, "date_time", date_time);

    send_cjson_to_req_and_free(root, req);
}

// Replace send_sensor_config_json_ws with this safe version
void send_sensor_config_json_ws(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    if (!root)
    {
        ESP_LOGE(TAG, "send_sensor_config_json_ws: cJSON_CreateObject failed");
        return;
    }

    char key[64];

    // Take sensor_cfg_mutex if available to read config safely
    if (sensor_cfg_mutex && xSemaphoreTake(sensor_cfg_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
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
        xSemaphoreGive(sensor_cfg_mutex);
    }
    else
    {
        LOGW(TAG, "send_sensor_config_json_ws: could not take sensor_cfg_mutex");
    }

    send_cjson_to_req_and_free(root, req);
}

// Replace send_config_json_ws with this robust version
void send_config_json_ws(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    if (!root)
    {
        LOGE(TAG, "send_config_json_ws: cJSON_CreateObject failed");
        return;
    }

    // --- Network ---
    if (network_config_mutex && xSemaphoreTake(network_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        cJSON_AddStringToObject(root, "enable_wifi", wifi_config.enable_wifi);
        cJSON_AddStringToObject(root, "enable_dhcp", wifi_config.enable_dhcp);
        cJSON_AddStringToObject(root, "hostname", wifi_config.hostname);
        cJSON_AddStringToObject(root, "ssid", wifi_config.ssid);
        cJSON_AddStringToObject(root, "wifi_password", wifi_config.wifi_password);
        cJSON_AddStringToObject(root, "ip_address", wifi_config.ip_address);
        cJSON_AddStringToObject(root, "subnet_mask", wifi_config.subnet_mask);
        cJSON_AddStringToObject(root, "gateway", wifi_config.gateway);
        cJSON_AddStringToObject(root, "primary_dns", wifi_config.primary_dns);
        cJSON_AddStringToObject(root, "secondary_dns", wifi_config.secondary_dns);

        cJSON_AddStringToObject(root, "enable_ethernet", ethernet_config.enable_ethernet);
        cJSON_AddStringToObject(root, "eth_enable_dhcp", ethernet_config.enable_dhcp);
        cJSON_AddStringToObject(root, "eth_ip_address", ethernet_config.ip_address);
        cJSON_AddStringToObject(root, "eth_subnet_mask", ethernet_config.subnet_mask);
        cJSON_AddStringToObject(root, "eth_gateway", ethernet_config.gateway);
        cJSON_AddStringToObject(root, "eth_primary_dns", ethernet_config.primary_dns);
        cJSON_AddStringToObject(root, "eth_secondary_dns", ethernet_config.secondary_dns);
        xSemaphoreGive(network_config_mutex);
    }
    else
    {
        LOGW(TAG, "send_config_json_ws: could not take network_config_mutex");
    }

    // --- Time ---
    if (time_config_mutex && xSemaphoreTake(time_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        cJSON_AddStringToObject(root, "ntp_server", time_config.ntp_server);
        cJSON_AddStringToObject(root, "timezone", time_config.timezone);
        xSemaphoreGive(time_config_mutex);
    }
    else
    {
        LOGW(TAG, "send_config_json_ws: could not take time_config_mutex");
    }

    // --- MQTT ---
    if (mqtt_config_mutex && xSemaphoreTake(mqtt_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        cJSON_AddStringToObject(root, "enable_mqtt", mqtt_config.enable_mqtt);
        cJSON_AddStringToObject(root, "mqtt_server", mqtt_config.mqtt_server);
        cJSON_AddStringToObject(root, "mqtt_port", mqtt_config.mqtt_port);
        cJSON_AddStringToObject(root, "mqtt_base_topic", mqtt_config.mqtt_base_topic);
        xSemaphoreGive(mqtt_config_mutex);
    }
    else
    {
        LOGW(TAG, "send_config_json_ws: could not take mqtt_config_mutex");
    }

    // --- InfluxDB ---
    if (influxdb_config_mutex && xSemaphoreTake(influxdb_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        cJSON_AddStringToObject(root, "enable_influxdb", influxdb_config.enable_influxdb);
        cJSON_AddStringToObject(root, "influxdb_url", influxdb_config.influxdb_url);
        cJSON_AddStringToObject(root, "influxdb_org", influxdb_config.influxdb_org);
        cJSON_AddStringToObject(root, "influxdb_bucket", influxdb_config.influxdb_bucket);
        cJSON_AddStringToObject(root, "influxdb_token", influxdb_config.influxdb_token);
        xSemaphoreGive(influxdb_config_mutex);
    }
    else
    {
        LOGW(TAG, "send_config_json_ws: could not take influxdb_config_mutex");
    }

    // --- Fan ---
    if (fan_config_mutex && xSemaphoreTake(fan_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
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
    else
    {
        LOGW(TAG, "send_config_json_ws: could not take fan_config_mutex");
    }

    // --- I2C ---
    if (i2c_config_mutex && xSemaphoreTake(i2c_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        cJSON_AddStringToObject(root, "bus0_multiplexer_address", i2c_config.bus0_multiplexer_address);
        cJSON_AddStringToObject(root, "bus1_multiplexer_address", i2c_config.bus1_multiplexer_address);
        cJSON_AddStringToObject(root, "enable_lcd", i2c_config.enable_lcd);
        xSemaphoreGive(i2c_config_mutex);
    }
    else
    {
        LOGW(TAG, "send_config_json_ws: could not take i2c_config_mutex");
    }

    // --- Syslog ---
    if (syslog_config_mutex && xSemaphoreTake(syslog_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        cJSON_AddStringToObject(root, "enable_syslog", syslog_config.enable_syslog);
        cJSON_AddStringToObject(root, "syslog_ip", syslog_config.syslog_ip);
        cJSON_AddStringToObject(root, "syslog_port", syslog_config.syslog_port);
        cJSON_AddStringToObject(root, "syslog_hostname", syslog_config.syslog_hostname);
        cJSON_AddStringToObject(root, "syslog_appname", syslog_config.syslog_appname);
        cJSON_AddStringToObject(root, "syslog_device_id", syslog_config.syslog_device_id);
        xSemaphoreGive(syslog_config_mutex);
    }
    else
    {
        LOGW(TAG, "send_config_json_ws: could not take syslog_config_mutex");
    }

    send_cjson_to_req_and_free(root, req);
}

// send_statemachine_config_json_ws - send statemachine config in small chunks to the requesting websocket
void send_statemachine_config_json_ws(httpd_req_t *req)
{
    // 1) Send start message with totals
    cJSON *start = cJSON_CreateObject();
    if (!start)
    {
        LOGW(TAG, "send_statemachine_config_json_ws: start cJSON_CreateObject failed (OOM). free_heap=%u", (unsigned)esp_get_free_heap_size());
        return;
    }
    cJSON_AddStringToObject(start, "type", "statemachine_start");
    cJSON_AddNumberToObject(start, "total_states", MAX_STATE_CONFIG_FILES);
    send_cjson_to_req_and_free(start, req);

    // 2) Send general settings as a small object
    cJSON *gen = cJSON_CreateObject();
    if (!gen)
    {
        LOGW(TAG, "send_statemachine_config_json_ws: gen cJSON_CreateObject failed (OOM). free_heap=%u", (unsigned)esp_get_free_heap_size());
        return;
    }

    // take mutex briefly to read general settings
    if (state_config_mutex && xSemaphoreTake(state_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        cJSON_AddNumberToObject(gen, "wday_d_h_start", statemachine_gen_config.wday_d_h_start);
        cJSON_AddNumberToObject(gen, "wday_d_m_start", statemachine_gen_config.wday_d_m_start);
        cJSON_AddNumberToObject(gen, "wday_n_h_start", statemachine_gen_config.wday_n_h_start);
        cJSON_AddNumberToObject(gen, "wday_n_m_start", statemachine_gen_config.wday_n_m_start);
        cJSON_AddNumberToObject(gen, "wkend_d_h_start", statemachine_gen_config.wkend_d_h_start);
        cJSON_AddNumberToObject(gen, "wkend_d_m_start", statemachine_gen_config.wkend_d_h_start);
        cJSON_AddNumberToObject(gen, "wkend_n_h_start", statemachine_gen_config.wkend_n_h_start);
        cJSON_AddNumberToObject(gen, "wkend_n_m_start", statemachine_gen_config.wkend_n_m_start);
        cJSON_AddStringToObject(gen, "wkend_d1", statemachine_gen_config.wkend_d1);
        cJSON_AddStringToObject(gen, "wkend_d2", statemachine_gen_config.wkend_d2);
        cJSON_AddNumberToObject(gen, "rh_l", statemachine_gen_config.rh_low_level);
        cJSON_AddNumberToObject(gen, "rh_h", statemachine_gen_config.rh_high_level);
        cJSON_AddNumberToObject(gen, "co2_l", statemachine_gen_config.co2_low_level);
        cJSON_AddNumberToObject(gen, "co2_h", statemachine_gen_config.co2_high_level);
        xSemaphoreGive(state_config_mutex);
    }
    else
    {
        LOGW(TAG, "send_statemachine_config_json_ws: could not take state_config_mutex for general settings");
    }

    cJSON_AddStringToObject(gen, "type", "statemachine_general");
    send_cjson_to_req_and_free(gen, req);

    // 3) Send each state as a separate small JSON message
    for (int i = 0; i < MAX_STATE_CONFIG_FILES; ++i)
    {
        cJSON *s = cJSON_CreateObject();
        if (!s)
        {
            LOGW(TAG, "send_statemachine_config_json_ws: state %d cJSON_CreateObject failed (OOM). free_heap=%u", i, (unsigned)esp_get_free_heap_size());
            // continue trying next states
            continue;
        }

        // Acquire mutex briefly to read this state's config
        if (state_config_mutex && xSemaphoreTake(state_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            char fanspeed_val[16];

            cJSON_AddBoolToObject(s, "s_en", statemachine_config[i].enable_state);

            if (statemachine_config[i].fanspeed == 1)
                snprintf(fanspeed_val, sizeof(fanspeed_val), "Low");
            else if (statemachine_config[i].fanspeed == 2)
                snprintf(fanspeed_val, sizeof(fanspeed_val), "Medium");
            else if (statemachine_config[i].fanspeed == 3)
                snprintf(fanspeed_val, sizeof(fanspeed_val), "High");
            else
                snprintf(fanspeed_val, sizeof(fanspeed_val), "0");

            cJSON_AddStringToObject(s, "s_fanspeed", fanspeed_val);
            cJSON_AddStringToObject(s, "s_name", statemachine_config[i].state_name);
            cJSON_AddNumberToObject(s, "s_st_h", statemachine_config[i].state_start_hour);
            cJSON_AddNumberToObject(s, "s_st_m", statemachine_config[i].state_start_minute);
            cJSON_AddNumberToObject(s, "s_end_h", statemachine_config[i].state_end_hour);
            cJSON_AddNumberToObject(s, "s_end_m", statemachine_config[i].state_end_minute);
            cJSON_AddNumberToObject(s, "s_dur_m", statemachine_config[i].duration_minutes);
            cJSON_AddNumberToObject(s, "s_cycl_int_m", statemachine_config[i].cycling_interval_min);

            // valves positions as small array
            cJSON *varr = cJSON_CreateArray();
            if (varr)
            {
                for (int j = 0; j < MAX_VALVE_COUNT; ++j)
                {
                    cJSON_AddItemToArray(varr, cJSON_CreateNumber(statemachine_config[i].valve_position[j]));
                }
                cJSON_AddItemToObject(s, "s_valves", varr);
            }
            else
            {
                LOGW(TAG, "send_statemachine_config_json_ws: could not create valves array for state %d", i);
            }

            xSemaphoreGive(state_config_mutex);
        }
        else
        {
            LOGW(TAG, "send_statemachine_config_json_ws: could not take state_config_mutex for state %d", i);
            // still send minimal info
            cJSON_AddStringToObject(s, "s_name", "<locked>");
        }

        // Add metadata and send
        cJSON_AddNumberToObject(s, "state_index", i);
        cJSON_AddStringToObject(s, "type", "statemachine_state");

        // Debug: log heap before printing
        LOGD(TAG, "send_statemachine_config_json_ws: about to send state %d, free_heap=%u", i, (unsigned)esp_get_free_heap_size());
        send_cjson_to_req_and_free(s, req);
    }

    // 4) Send end message
    cJSON *end = cJSON_CreateObject();
    if (end)
    {
        cJSON_AddStringToObject(end, "type", "statemachine_end");
        send_cjson_to_req_and_free(end, req);
    }
    else
    {
        LOGW(TAG, "send_statemachine_config_json_ws: end cJSON_CreateObject failed (OOM). free_heap=%u", (unsigned)esp_get_free_heap_size());
    }
}

void send_stats_json_ws(httpd_req_t *req)
{
    const int PAGE_SIZE = 3; // reduced page size to lower memory/DMA pressure and speed up first page
    LOGI(TAG, "send_stats_json_ws called (req=%p)", (void *)req);

    uint64_t t0 = esp_timer_get_time();
    LOGD(TAG, "send_stats_json_ws start: t0_us=%llu free_heap=%u dma_free=%u", (unsigned long long)t0, (unsigned)esp_get_free_heap_size(), (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA));

    /* --- runtime status fields for the HTML dashboard --- */
    char date_time[64] = "";
    char uptime[32] = "";
    formatted_daydatetime(date_time, sizeof(date_time));
    formatted_uptime(uptime, sizeof(uptime));

    uint32_t heap_free = 0;
    uint32_t heap_min_ever = 0;

    float cpu_temp_c = 0.0f;
    /* If you have a board-specific CPU temp function, replace this call */
    extern float get_cpu_temp_c(void);
    cpu_temp_c = get_cpu_temp_c();

    /* statemachine snapshot */
    const char *sm_state_str = "UNKNOWN";
    const char *sm_fanspeed_str = "UNKNOWN";
    int sm_co2_high = 0;
    int sm_rh_high = 0;
    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(20)) == pdTRUE)
    {
        sm_state_str = state_nr_to_str(sm_actuals.state);
        sm_fanspeed_str = fan_speed_to_str(sm_actuals.fanspeed);
        sm_co2_high = sm_actuals.co2_sensors_high;
        sm_rh_high = sm_actuals.rh_sensors_high;
        heap_free = (uint32_t)sm_actuals.heap_free;
        heap_min_ever = (uint32_t)sm_actuals.heap_min_ever;
        xSemaphoreGive(statemachine_mutex);
    }

    /* WiFi / Ethernet placeholders and best-effort retrieval */
    char wifi_ssid[33] = "<none>";
    char wifi_bssid[32] = "<none>";
    int wifi_rssi = 0;
    char wifi_ip[32] = "<no-ip>";
    char eth_ip[32] = "<no-ip>";
    char eth_netmask[32] = "<no-netmask>";

#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32C3
    /* Try to get station info and IP (best-effort; ignore failures) */
    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK)
    {
        snprintf(wifi_ip, sizeof(wifi_ip), IPSTR, IP2STR(&ip_info.ip));
    }
    wifi_ap_record_t apinfo;
    if (esp_wifi_sta_get_ap_info(&apinfo) == ESP_OK)
    {
        snprintf((char *)wifi_ssid, sizeof(wifi_ssid), "%s", apinfo.ssid);
        snprintf((char *)wifi_bssid, sizeof(wifi_bssid), MACSTR, MAC2STR(apinfo.bssid));
        wifi_rssi = apinfo.rssi;
    }
    /* Ethernet IP */
    esp_netif_t *ethif = esp_netif_get_handle_from_ifkey("ETH_DEF");
    if (ethif && esp_netif_get_ip_info(ethif, &ip_info) == ESP_OK)
    {
        snprintf(eth_ip, sizeof(eth_ip), IPSTR, IP2STR(&ip_info.ip));
        snprintf(eth_netmask, sizeof(eth_netmask), IPSTR, IP2STR(&ip_info.netmask));
    }
#endif

    // Optional: per-connection rate limiting (if you added client_last_us helpers)
    // #ifdef ENABLE_PER_CONN_RATE_LIMIT
    //   const uint64_t MIN_INTERVAL_US = 5ULL * 1000000ULL; // 5s
    //    uint64_t last_for_client = client_last_us(req);
    //    if (last_for_client != 0 && (t0 - last_for_client) < MIN_INTERVAL_US)
    //    {
    //       LOGD(TAG, "send_stats_json_ws: per-connection rate-limited for req=%p (delta_us=%llu)", (void *)req, (unsigned long long)(t0 - last_for_client));
    //       return;
    //   }
    //  client_set_last_us(req, t0);
    // #endif

    // 0) Quick immediate start summary so client can show a header/spinner
    cJSON *start = cJSON_CreateObject();
    if (start)
    {
        cJSON_AddStringToObject(start, "type", "tasks_start");
        cJSON_AddNumberToObject(start, "seq", 0);
        cJSON_AddNumberToObject(start, "total", -1);

        /* runtime fields */
        cJSON_AddStringToObject(start, "date_time", date_time);
        cJSON_AddStringToObject(start, "uptime", uptime);
        cJSON_AddNumberToObject(start, "heap_free", (int)heap_free);
        cJSON_AddNumberToObject(start, "heap_min_ever", (int)heap_min_ever);
        cJSON_AddNumberToObject(start, "cpu_temp_c", cpu_temp_c);

        /* statemachine snapshot */
        cJSON_AddStringToObject(start, "statemachine_state", sm_state_str);
        cJSON_AddStringToObject(start, "statemachine_fanspeed", sm_fanspeed_str);
        cJSON_AddNumberToObject(start, "co2_sensors_high", sm_co2_high);
        cJSON_AddNumberToObject(start, "rh_sensors_high", sm_rh_high);

        /* network */
        cJSON_AddStringToObject(start, "wifi_ssid", wifi_ssid);
        cJSON_AddStringToObject(start, "wifi_bssid", wifi_bssid);
        cJSON_AddNumberToObject(start, "wifi_rssi", wifi_rssi);
        cJSON_AddStringToObject(start, "wifi_ip", wifi_ip);
        cJSON_AddStringToObject(start, "eth_ip", eth_ip);
        cJSON_AddStringToObject(start, "eth_netmask", eth_netmask);

        send_cjson_to_req_and_free(start, req);
    }
    else
    {
        LOGW(TAG, "send_stats_json_ws: could not create start summary (OOM)");
    }

    // 1) Snapshot tasks into local memory
    UBaseType_t count = uxTaskGetNumberOfTasks();
    if (count == 0)
    {
        // send an empty page message (immediate)
        cJSON *empty = cJSON_CreateObject();
        if (empty)
        {
            cJSON_AddStringToObject(empty, "type", "tasks_page");
            cJSON_AddNumberToObject(empty, "seq", 0);
            cJSON_AddNumberToObject(empty, "start", 0);
            cJSON_AddNumberToObject(empty, "count", 0);
            cJSON_AddItemToObject(empty, "tasks", cJSON_CreateArray());
            send_cjson_to_req_and_free(empty, req);
        }
        LOGI(TAG, "send_stats_json_ws: no tasks to report");
        return;
    }

    TaskStatus_t *tasks = pvPortMalloc(count * sizeof(TaskStatus_t));
    if (!tasks)
    {
        LOGW(TAG, "send_stats_json_ws: pvPortMalloc tasks failed (OOM). free_heap=%u", (unsigned)esp_get_free_heap_size());
        cJSON *err = cJSON_CreateObject();
        if (err)
        {
            cJSON_AddStringToObject(err, "type", "tasks_page");
            cJSON_AddNumberToObject(err, "seq", 0);
            cJSON_AddNumberToObject(err, "start", 0);
            cJSON_AddNumberToObject(err, "count", 0);
            cJSON_AddStringToObject(err, "error", "oom");
            send_cjson_to_req_and_free(err, req);
        }
        return;
    }

    // Fill snapshot and compute total run time
    uint32_t total_run = uxTaskGetSystemState(tasks, count, NULL);
    qsort(tasks, count, sizeof(TaskStatus_t), compare_tasks);

    uint64_t t1 = esp_timer_get_time();
    LOGD(TAG, "Snapshot+sort done delta_ms=%llu free_heap=%u dma_free=%u", (unsigned long long)((t1 - t0) / 1000), (unsigned)esp_get_free_heap_size(), (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA));

    // Now we have a stable seq number for this send operation
    uint32_t seq = __atomic_add_fetch(&ws_msg_seq, 1u, __ATOMIC_RELAXED);

    // Send an updated start message with real seq and total
    cJSON *start2 = cJSON_CreateObject();
    if (start2)
    {
        cJSON_AddStringToObject(start2, "type", "tasks_start");
        cJSON_AddNumberToObject(start2, "seq", seq);
        cJSON_AddNumberToObject(start2, "total", (int)count);

        /* repeat runtime fields so client receives them with the real seq */
        cJSON_AddStringToObject(start2, "date_time", date_time);
        cJSON_AddStringToObject(start2, "uptime", uptime);
        cJSON_AddNumberToObject(start2, "heap_free", (int)heap_free);
        cJSON_AddNumberToObject(start2, "heap_min_ever", (int)heap_min_ever);
        cJSON_AddNumberToObject(start2, "cpu_temp_c", cpu_temp_c);

        cJSON_AddStringToObject(start2, "statemachine_state", sm_state_str);
        cJSON_AddStringToObject(start2, "statemachine_fanspeed", sm_fanspeed_str);
        cJSON_AddNumberToObject(start2, "co2_sensors_high", sm_co2_high);
        cJSON_AddNumberToObject(start2, "rh_sensors_high", sm_rh_high);

        cJSON_AddStringToObject(start2, "wifi_ssid", wifi_ssid);
        cJSON_AddStringToObject(start2, "wifi_bssid", wifi_bssid);
        cJSON_AddNumberToObject(start2, "wifi_rssi", wifi_rssi);
        cJSON_AddStringToObject(start2, "wifi_ip", wifi_ip);
        cJSON_AddStringToObject(start2, "eth_ip", eth_ip);
        cJSON_AddStringToObject(start2, "eth_netmask", eth_netmask);

        send_cjson_to_req_and_free(start2, req);
    }

    // 2) Stream pages in small chunks
    for (int start_idx = 0; start_idx < (int)count; start_idx += PAGE_SIZE)
    {
        int end_idx = start_idx + PAGE_SIZE;
        if (end_idx > (int)count)
            end_idx = count;

        LOGD(TAG, "send_stats_json_ws: building page start=%d end=%d free_heap=%u dma_free=%u", start_idx, end_idx, (unsigned)esp_get_free_heap_size(), (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA));

        cJSON *page = cJSON_CreateObject();
        if (!page)
        {
            LOGW(TAG, "send_stats_json_ws: cJSON_CreateObject for page failed (OOM) at start=%d", start_idx);
            break;
        }

        cJSON_AddStringToObject(page, "type", "tasks_page");
        cJSON_AddNumberToObject(page, "seq", seq);
        cJSON_AddNumberToObject(page, "start", start_idx);
        cJSON_AddNumberToObject(page, "count", end_idx - start_idx);

        cJSON *arr = cJSON_CreateArray();
        if (!arr)
        {
            LOGW(TAG, "send_stats_json_ws: cJSON_CreateArray failed (OOM) at start=%d", start_idx);
            cJSON_Delete(page);
            break;
        }

        for (int i = start_idx; i < end_idx; ++i)
        {
            TaskStatus_t *ts = &tasks[i];
            cJSON *t = cJSON_CreateObject();
            if (!t)
            {
                LOGW(TAG, "send_stats_json_ws: cJSON_CreateObject for task failed (OOM) at idx=%d", i);
                cJSON_Delete(arr);
                cJSON_Delete(page);
                goto cleanup_tasks;
            }

            // name
            if (ts->pcTaskName && ts->pcTaskName[0] != '\0')
                cJSON_AddStringToObject(t, "name", ts->pcTaskName);
            else
                cJSON_AddStringToObject(t, "name", "<no-name>");

            // state
            cJSON_AddStringToObject(t, "state", state_char_from_enum(ts->eCurrentState));

            // priority
            cJSON_AddNumberToObject(t, "prio", ts->uxCurrentPriority);

            // run ticks and cpu percent
            cJSON_AddNumberToObject(t, "run_ticks", ts->ulRunTimeCounter);
            float pct = total_run ? (100.0f * ts->ulRunTimeCounter) / total_run : 0.0f;
            cJSON_AddNumberToObject(t, "cpu_pct", pct);

            // stack high water mark
            cJSON_AddNumberToObject(t, "stack_high_water", ts->usStackHighWaterMark);

            cJSON_AddItemToArray(arr, t);
        }

        cJSON_AddItemToObject(page, "tasks", arr);

        // Diagnostic log right before send
        LOGD(TAG, "send_stats_json_ws: sending page start=%d end=%d seq=%u free_heap=%u dma_free=%u", start_idx, end_idx, (unsigned)seq, (unsigned)esp_get_free_heap_size(), (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA));

        // Send per-request and free
        send_cjson_to_req_and_free(page, req);

        // Small yield to allow other tasks (optional)
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    // 3) Send end message
    cJSON *end = cJSON_CreateObject();
    if (end)
    {
        cJSON_AddStringToObject(end, "type", "tasks_end");
        cJSON_AddNumberToObject(end, "seq", seq);
        send_cjson_to_req_and_free(end, req);
    }

cleanup_tasks:
    vPortFree(tasks);

    uint64_t t_end = esp_timer_get_time();
    LOGD(TAG, "send_stats_json_ws finished total_ms=%llu free_heap=%u dma_free=%u", (unsigned long long)((t_end - t0) / 1000), (unsigned)esp_get_free_heap_size(), (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA));
}
