/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include "http_send_ws_data.h"
#include "statemachine_states.h"

static const char *TAG = "HTTP_JSON_ws";

static uint32_t ws_msg_seq = 0u;

static void send_cjson_and_free(cJSON *obj)
{
    if (!obj)
        return;
    char *out = cJSON_PrintUnformatted(obj);
    cJSON_Delete(obj);
    if (!out)
    {
        ESP_LOGW(TAG, "send_cjson_and_free: cJSON_PrintUnformatted returned NULL (OOM). free heap=%u",
                 esp_get_free_heap_size());
        return;
    }
    ws_async_send(out, strlen(out));
    free(out);
}

// Replace send_index_html_ws with this robust version
void send_index_html_ws(void)
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
        ESP_LOGE(TAG, "cJSON_CreateObject failed");
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
        ESP_LOGE(TAG, "NVS open failed for send_index_html_ws");
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
                ESP_LOGW(TAG, "send_index_html_ws: sensor_avg_queue peek failed");
            }
        }
        else
        {
            ESP_LOGW(TAG, "send_index_html_ws: sensor_avg_item_size mismatch");
        }
    }
    else
    {
        ESP_LOGW(TAG, "send_index_html_ws: sensor queues not ready or NULL");
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
        ESP_LOGW(TAG, "send_index_html_ws: could not take sensor_cfg_mutex");
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
        ESP_LOGW(TAG, "send_index_html_ws: could not take statemachine_mutex");
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
    else
    {
        ESP_LOGE(TAG, "send_index_html_ws: cJSON_PrintUnformatted returned NULL");
    }
}

// Replace send_sensor_config_json_ws with this safe version
void send_sensor_config_json_ws(void)
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
        ESP_LOGW(TAG, "send_sensor_config_json_ws: could not take sensor_cfg_mutex");
    }

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (out)
    {
        ws_async_send(out, strlen(out));
        free(out);
    }
    else
    {
        ESP_LOGE(TAG, "send_sensor_config_json_ws: cJSON_PrintUnformatted returned NULL");
    }
}

// Replace send_config_json_ws with this robust version
void send_config_json_ws(void)
{
    cJSON *root = cJSON_CreateObject();
    if (!root)
    {
        ESP_LOGE(TAG, "send_config_json_ws: cJSON_CreateObject failed");
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
        ESP_LOGW(TAG, "send_config_json_ws: could not take network_config_mutex");
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
        ESP_LOGW(TAG, "send_config_json_ws: could not take time_config_mutex");
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
        ESP_LOGW(TAG, "send_config_json_ws: could not take mqtt_config_mutex");
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
        ESP_LOGW(TAG, "send_config_json_ws: could not take influxdb_config_mutex");
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
        ESP_LOGW(TAG, "send_config_json_ws: could not take fan_config_mutex");
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
        ESP_LOGW(TAG, "send_config_json_ws: could not take i2c_config_mutex");
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
        ESP_LOGW(TAG, "send_config_json_ws: could not take syslog_config_mutex");
    }

    char *out = cJSON_PrintUnformatted(root);
    ESP_LOGD(TAG, "Config JSON: %s", out ? out : "null");
    cJSON_Delete(root);
    if (out)
    {
        ws_async_send(out, strlen(out));
        free(out);
    }
    else
    {
        ESP_LOGE(TAG, "send_config_json_ws: cJSON_PrintUnformatted returned NULL");
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

void send_stats_json_ws(void)
{
    const int PAGE_SIZE = 10;

    LOGI(TAG, "send_stats_json_ws start: free_heap=%lu, dma_free=%lu", (unsigned long)esp_get_free_heap_size(), (unsigned long)heap_caps_get_free_size(MALLOC_CAP_DMA));

    // allocate a sequence number for this send operation
    uint32_t seq = __atomic_add_fetch(&ws_msg_seq, 1u, __ATOMIC_RELAXED);

    // Get number of tasks
    UBaseType_t count = uxTaskGetNumberOfTasks();
    if (count == 0)
    {
        // send an empty page message
        cJSON *empty = cJSON_CreateObject();
        if (!empty)
        {
            ESP_LOGW(TAG, "send_stats_json_ws: cJSON_CreateObject failed (OOM)");
            return;
        }
        cJSON_AddStringToObject(empty, "type", "tasks_page");
        cJSON_AddNumberToObject(empty, "seq", seq);
        cJSON_AddNumberToObject(empty, "start", 0);
        cJSON_AddNumberToObject(empty, "count", 0);
        cJSON_AddItemToObject(empty, "tasks", cJSON_CreateArray());
        send_cjson_and_free(empty);
        return;
    }

    // Snapshot tasks into an array
    TaskStatus_t *tasks = pvPortMalloc(count * sizeof(TaskStatus_t));
    if (!tasks)
    {
        ESP_LOGW(TAG, "send_stats_json_ws: pvPortMalloc tasks failed (OOM). free heap=%u", esp_get_free_heap_size());
        /* send an error page so client knows */
        cJSON *err = cJSON_CreateObject();
        if (err)
        {
            cJSON_AddStringToObject(err, "type", "tasks_page");
            cJSON_AddNumberToObject(err, "seq", seq);
            cJSON_AddNumberToObject(err, "start", 0);
            cJSON_AddNumberToObject(err, "count", 0);
            cJSON_AddStringToObject(err, "error", "oom");
            send_cjson_and_free(err);
        }
        return;
    }

    // Fill snapshot and sort
    uint32_t total_run = uxTaskGetSystemState(tasks, count, NULL);
    qsort(tasks, count, sizeof(TaskStatus_t), compare_tasks);

    // Optional: send start message with total count
    cJSON *start = cJSON_CreateObject();
    if (start)
    {
        cJSON_AddStringToObject(start, "type", "tasks_start");
        cJSON_AddNumberToObject(start, "seq", seq);
        cJSON_AddNumberToObject(start, "total", (int)count);
        send_cjson_and_free(start);
    }
    else
    {
        ESP_LOGW(TAG, "send_stats_json_ws: cJSON_CreateObject for start failed (OOM)");
    }

    // Send pages
    for (int start_idx = 0; start_idx < (int)count; start_idx += PAGE_SIZE)
    {
        int end_idx = start_idx + PAGE_SIZE;
        if (end_idx > (int)count)
            end_idx = count;

        cJSON *page = cJSON_CreateObject();
        if (!page)
        {
            ESP_LOGW(TAG, "send_stats_json_ws: cJSON_CreateObject for page failed (OOM)");
            break;
        }
        cJSON_AddStringToObject(page, "type", "tasks_page");
        cJSON_AddNumberToObject(page, "seq", seq);
        cJSON_AddNumberToObject(page, "start", start_idx);
        cJSON_AddNumberToObject(page, "count", end_idx - start_idx);

        cJSON *arr = cJSON_CreateArray();
        if (!arr)
        {
            ESP_LOGW(TAG, "send_stats_json_ws: cJSON_CreateArray failed (OOM)");
            cJSON_Delete(page);
            break;
        }

        for (int i = start_idx; i < end_idx; ++i)
        {
            TaskStatus_t *ts = &tasks[i];
            cJSON *t = cJSON_CreateObject();
            if (!t)
            {
                ESP_LOGW(TAG, "send_stats_json_ws: cJSON_CreateObject for task failed (OOM)");
                /* free arr/page and break out */
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

        // Print and send page; if printing fails, log and break to avoid infinite loop
        char *page_out = cJSON_PrintUnformatted(page);
        cJSON_Delete(page);
        if (!page_out)
        {
            ESP_LOGW(TAG, "send_stats_json_ws: cJSON_PrintUnformatted failed for page start=%d (OOM). free heap=%u",
                     start_idx, esp_get_free_heap_size());
            break;
        }
        ws_async_send(page_out, strlen(page_out));
        free(page_out);
    }

    // Optional: send end message
    cJSON *end = cJSON_CreateObject();
    if (end)
    {
        cJSON_AddStringToObject(end, "type", "tasks_end");
        cJSON_AddNumberToObject(end, "seq", seq);
        send_cjson_and_free(end);
    }

cleanup_tasks:
    vPortFree(tasks);
}