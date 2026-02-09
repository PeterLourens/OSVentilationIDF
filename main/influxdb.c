/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include "influxdb.h"

static const char *TAG = "Influxdb";

QueueHandle_t influx_queue = NULL;
TaskHandle_t influx_sender_task_handle = NULL;

// ---------------------------------------------------------------------------
//   Configuration
//   ---------------------------------------------------------------------------
#define INFLUX_QUEUE_LEN 8
#define INFLUX_MAX_PAYLOAD 8192
#define INFLUX_SENSORS_CHUNK 4096
#define INFLUX_SENDER_STACK 8192
#define INFLUX_SENDER_PRIO 5

// ---------------------------------------------------------------------------
// Queue item and globals
// ---------------------------------------------------------------------------
typedef struct
{
    char *payload; // heap-allocated, null-terminated
    size_t len;
} influx_job_t;

// ---------------------------------------------------------------------------
// Existing helpers (kept from your original file)
// -------------------------------------------------------------------
bool influxdb_wait_ready(TickType_t timeout_ticks)
{
    TickType_t start = xTaskGetTickCount();
    while (influx_queue == NULL)
    {
        if ((xTaskGetTickCount() - start) > timeout_ticks)
            return false;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return true;
}

// Mutex-protected config snapshot
void influxdb_get_config_snapshot(Influxdb_config_t *out)
{
    xSemaphoreTake(influxdb_config_mutex, portMAX_DELAY);
    memcpy(out, &influxdb_config, sizeof(Influxdb_config_t));
    xSemaphoreGive(influxdb_config_mutex);
}

// Get timestamp from DS3231 (UTC seconds) — ESP-IDF safe
int64_t influxdb_get_timestamp(void)
{
    struct tm rtc_time = {0};

    if (ds3231_get_time(ds3231_bus0_handle, &rtc_time) != ESP_OK)
    {
        time_t now;
        time(&now);
        return (int64_t)now * 1000000000LL;
    }

    int year = rtc_time.tm_year + 1900;
    int month = rtc_time.tm_mon + 1;
    int day = rtc_time.tm_mday;
    int hour = rtc_time.tm_hour;
    int min = rtc_time.tm_min;
    int sec = rtc_time.tm_sec;

    if (month <= 2)
    {
        year -= 1;
        month += 12;
    }

    int64_t era = year / 400;
    int64_t yoe = year - era * 400;
    int64_t doy = (153 * (month - 3) + 2) / 5 + day - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;

    int64_t days = era * 146097 + doe - 719468;
    int64_t ts = days * 86400LL + hour * 3600 + min * 60 + sec;

    return ts * 1000000000LL;
}

static void normalize_influx_url(char *url)
{
    size_t len = strlen(url);
    while (len > 0 && url[len - 1] == '/')
    {
        url[len - 1] = '\0';
        len--;
    }
}

static void escape_tag(char *dst, size_t dst_size, const char *src)
{
    size_t j = 0;
    for (size_t i = 0; src[i] != '\0' && j + 2 < dst_size; i++)
    {
        char c = src[i];
        if (c == ' ' || c == ',' || c == '=')
        {
            dst[j++] = '\\';
        }
        dst[j++] = c;
    }
    dst[j] = '\0';
}

void influxdb_build_url(char *out, size_t out_size, const Influxdb_config_t *cfg)
{
    char base[256];
    strlcpy(base, cfg->influxdb_url, sizeof(base));
    normalize_influx_url(base);
    snprintf(out, out_size, "%s/api/v2/write?org=%s&bucket=%s&precision=ns", base, cfg->influxdb_org, cfg->influxdb_bucket);
}

void influxdb_append_line(char *out, size_t out_size, const char *line)
{
    size_t used = strlen(out);
    size_t out_free = out_size - used - 1;
    if (out_free == 0)
        return;
    size_t len = strlen(line);
    if (len > out_free)
    {
        LOGE(TAG, "Influx payload overflow: line needs %u bytes, free %d", (unsigned)len, (int)out_free);
        len = out_free;
    }
    memcpy(out + used, line, len);
    out[used + len] = '\0';
}

// ---------------------------------------------------------------------------
// Payload builders (adapted and split)
// ---------------------------------------------------------------------------

void influxdb_build_payload_actuals(char *out, size_t out_size)
{
    if (xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(200)) != pdTRUE)
        return;

    char line[1024];
    memset(line, 0, sizeof(line));

    int64_t ts = influxdb_get_timestamp();
    uint64_t uptime = esp_timer_get_time() / 1000000;

    snprintf(line, sizeof(line),
             "actuals "
             "fanspeed=%d,"
             "state=%d,"
             "co2_sensors_high=%d,"
             "rh_sensors_high=%d,"
             "valve_lock=%s,"
             "wifi_error=%s,"
             "wifi_connected=%s,"
             "wifi_ap_mode=%s,"
             "wifi_rssi=%d,"
             "wifi_channel=%d,"
             "wifi_authmode=%d,"
             "wifi_phy=%d,"
             "wifi_bandwidth=%d,"
             "dhcp_enabled=%s,"
             "cpu_temp=%d,"
             "uptime=%llu,"
             "heap_free=%u,"
             "heap_min_ever=%u,"
             "stack_i2c_sensors=%lu,"
             "stack_i2c_ds3231=%lu,"
             "stack_wifi=%lu,"
             "stack_i2c_lcd=%lu,"
             "stack_rgb_leds=%lu,"
             "stack_statemachine=%lu,"
             "stack_valvectrl=%lu,"
             "stack_mqtt=%lu,"
             "stack_influxdb=%lu"
             " %lld\n",
             sm_actuals.fanspeed,
             sm_actuals.state,
             sm_actuals.co2_sensors_high,
             sm_actuals.rh_sensors_high,
             sm_actuals.valve_lock ? "true" : "false",
             sm_actuals.wifi_error ? "true" : "false",
             sm_actuals.wifi_connected ? "true" : "false",
             sm_actuals.wifi_ap_mode ? "true" : "false",
             sm_actuals.wifi_rssi,
             sm_actuals.wifi_channel,
             sm_actuals.wifi_authmode,
             sm_actuals.wifi_phy,
             sm_actuals.wifi_bandwidth,
             sm_actuals.dhcp_enabled ? "true" : "false",
             sm_actuals.cpu_temp,
             (unsigned long long)uptime,
             (unsigned)sm_actuals.heap_free,
             (unsigned)sm_actuals.heap_min_ever,
             sm_actuals.stack_i2c_sensors,
             sm_actuals.stack_i2c_ds3231,
             sm_actuals.stack_wifi,
             sm_actuals.stack_i2c_lcd,
             sm_actuals.stack_rgb_leds,
             sm_actuals.stack_statemachine,
             sm_actuals.stack_valvectrl,
             sm_actuals.stack_mqtt,
             sm_actuals.stack_influxdb,
             (long long)ts);

    xSemaphoreGive(statemachine_mutex);

    influxdb_append_line(out, out_size, line);
}

void influxdb_build_payload_valves(char *out, size_t out_size)
{
    char line[256];
    char valve_name[32];
    char key[16];
    int32_t pos = 0;
    nvs_handle_t nvs;

    memset(line, 0, sizeof(line));
    int64_t ts = influxdb_get_timestamp();

    esp_err_t err = nvs_open("valves", NVS_READONLY, &nvs);
    if (err != ESP_OK)
    {
        LOGE(TAG, "Failed to open NVS for valve positions: %s", esp_err_to_name(err));
        return;
    }

    for (int i = 0; i < MAX_VALVE_COUNT; i++)
    {
        snprintf(valve_name, sizeof(valve_name), "valve%d", i);
        snprintf(key, sizeof(key), "valve%d", i);

        esp_err_t r = nvs_get_i32(nvs, key, &pos);
        if (r != ESP_OK)
            pos = 0;

        if (pos < 0)
            pos = 0;
        if (pos > 24)
            pos = 24;

        snprintf(line, sizeof(line), "valve_positions,valve=%s position=%ld %lld\n", valve_name, pos, (long long)ts);
        influxdb_append_line(out, out_size, line);
    }

    nvs_close(nvs);
}

// Build sensors payload for a single bus (keeps chunks small)
void influxdb_build_payload_sensors_for_bus(char *out, size_t out_size, float sensor[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS], float avg[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS], int bus)
{
    char line[512];
    memset(line, 0, sizeof(line));

    int64_t ts = influxdb_get_timestamp();

    xSemaphoreTake(sensor_cfg_mutex, portMAX_DELAY);

    for (int ch = 0; ch < CHANNELS_PER_BUS; ch++)
    {
        if (sensor[bus][ch][IDX_VALID] != 1)
            continue;

        const bus_sensor_t *meta = (bus == 0) ? &bus0_sensor_settings[ch] : &bus1_sensor_settings[ch];

        char type_esc[64];
        char valve_esc[64];
        char loc_esc[128];

        escape_tag(type_esc, sizeof(type_esc), meta->type);
        escape_tag(valve_esc, sizeof(valve_esc), meta->valve);
        escape_tag(loc_esc, sizeof(loc_esc), meta->location);

        char sensor_id[32];
        snprintf(sensor_id, sizeof(sensor_id), "bus%d_ch%d", bus, ch);

        int n = snprintf(line, sizeof(line),
                         "sensors,"
                         "bus=%d,"
                         "channel=%d,"
                         "sensor_id=%s,"
                         "type=%s,"
                         "location=%s,"
                         "valve=%s "
                         "temperature_raw=%.2f,temperature_avg=%.2f,"
                         "humidity_raw=%.2f,humidity_avg=%.2f",
                         bus, ch, sensor_id, type_esc, loc_esc, valve_esc,
                         sensor[bus][ch][IDX_TEMP], avg[bus][ch][IDX_TEMP],
                         sensor[bus][ch][IDX_HUMIDITY], avg[bus][ch][IDX_HUMIDITY]);

        if (sensor[bus][ch][IDX_CO2] > 0.0f)
        {
            n += snprintf(line + n, sizeof(line) - n, ",co2_raw=%.2f,co2_avg=%.2f", sensor[bus][ch][IDX_CO2], avg[bus][ch][IDX_CO2]);
        }

        snprintf(line + n, sizeof(line) - n, " %lld\n", (long long)ts);
        influxdb_append_line(out, out_size, line);
    }

    xSemaphoreGive(sensor_cfg_mutex);
}

// ---------------------------------------------------------------------------
// Low-level send (returns ESP_OK on HTTP 2xx)
// ---------------------------------------------------------------------------
static esp_err_t influxdb_send_payload_internal(const char *payload, const Influxdb_config_t *cfg)
{
    if (!payload || !cfg)
    {
        LOGW(TAG, "send_internal: invalid args");
        return ESP_ERR_INVALID_ARG;
    }

    char url[1024];
    influxdb_build_url(url, sizeof(url), cfg);

    esp_http_client_config_t http_cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 15000, // slightly larger timeout for network variability
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client)
    {
        LOGE(TAG, "send_internal: esp_http_client_init failed");
        return ESP_FAIL;
    }

    char auth_header[1024];
    snprintf(auth_header, sizeof(auth_header), "Token %s", cfg->influxdb_token);

    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Content-Type", "text/plain; charset=utf-8");
    esp_http_client_set_header(client, "Accept", "application/json");

    LOGD(TAG, "HTTP POST to: %s", url);
    LOGD(TAG, "HTTP payload preview: %.256s", payload);

    esp_http_client_set_post_field(client, payload, (int)strlen(payload));

    esp_err_t err = esp_http_client_perform(client);
    int status = 0;
    char resp[512] = {0};

    if (err == ESP_OK)
    {
        status = esp_http_client_get_status_code(client);
        LOGD(TAG, "HTTP status: %d", status);

        int rlen = esp_http_client_read_response(client, resp, sizeof(resp) - 1);
        if (rlen > 0)
        {
            resp[rlen] = '\0';
            LOGI(TAG, "Influx response: %s", resp);
        }
        else
        {
            LOGD(TAG, "No response body (rlen=%d)", rlen);
        }
    }
    else
    {
        LOGW(TAG, "HTTP perform error: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);

    if (err != ESP_OK)
    {
        return err;
    }

    if (status < 200 || status >= 300)
    {
        LOGW(TAG, "Influx write returned non-2xx status: %d", status);
        return ESP_FAIL;
    }

    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Send with retry/backoff (synchronous, called from sender task)
// ---------------------------------------------------------------------------
static void influxdb_send_with_retry(const char *payload, const Influxdb_config_t *cfg)
{
    const int max_retries = 4;
    int attempt = 0;
    int backoff_ms = 500;

    while (attempt <= max_retries)
    {
        esp_err_t err = influxdb_send_payload_internal(payload, cfg);
        if (err == ESP_OK)
        {
            LOGI(TAG, "Influx write succeeded (attempt %d)", attempt + 1);
            return;
        }

        LOGW(TAG, "Influx write failed (attempt %d): %s", attempt + 1, esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(backoff_ms));
        backoff_ms = backoff_ms * 2;
        attempt++;
    }

    LOGE(TAG, "Influx write permanently failed after %d attempts", max_retries + 1);
    // Optional: persist payload to NVS for later retry (not implemented here)
}

// ---------------------------------------------------------------------------
// Sender task: serializes HTTP writes
// ---------------------------------------------------------------------------
static void influx_sender_task(void *pv)
{
    Influxdb_config_t cfg;
    influxdb_get_config_snapshot(&cfg);

    for (;;)
    {
        influx_job_t job;
        if (xQueueReceive(influx_queue, &job, portMAX_DELAY) == pdTRUE)
        {
            if (!job.payload)
                continue;

            // Refresh config snapshot before each send
            influxdb_get_config_snapshot(&cfg);

            if (strcmp(cfg.enable_influxdb, "On") != 0)
            {
                LOGW(TAG, "InfluxDB disabled; dropping payload");
                free(job.payload);
                continue;
            }

            influxdb_send_with_retry(job.payload, &cfg);
            free(job.payload);
        }
    }
}

// ---------------------------------------------------------------------------
// Public: initialize sender (call once at startup)
// ---------------------------------------------------------------------------
bool influxdb_init_sender(void)
{
    if (influx_queue != NULL)
    {
        LOGI(TAG, "influxdb_init_sender: already initialized (queue=%p)", (void *)influx_queue);
        return true;
    }

    influx_queue = xQueueCreate(INFLUX_QUEUE_LEN, sizeof(influx_job_t));
    if (!influx_queue)
    {
        LOGE(TAG, "influxdb_init_sender: failed to create queue");
        return false;
    }

    BaseType_t r = xTaskCreate(influx_sender_task, "influx_sender", INFLUX_SENDER_STACK, NULL, INFLUX_SENDER_PRIO, &influx_sender_task_handle);
    if (r != pdTRUE)
    {
        LOGE(TAG, "influxdb_init_sender: failed to create sender task");
        vQueueDelete(influx_queue);
        influx_queue = NULL;
        return false;
    }

    LOGI(TAG, "influxdb_init_sender: initialized (queue=%p, task=%p)", (void *)influx_queue, (void *)influx_sender_task_handle);
    return true;
}

// ---------------------------------------------------------------------------
// Enqueue helper (non-blocking with short timeout)
// ---------------------------------------------------------------------------
bool influxdb_enqueue_payload(const char *payload, size_t len)
{
    if (!payload || len == 0)
    {
        LOGW(TAG, "enqueue: empty payload");
        return false;
    }

    if (!influx_queue)
    {
        LOGW(TAG, "enqueue: influx queue not initialized");
        return false;
    }

    if (len > INFLUX_MAX_PAYLOAD)
    {
        LOGW(TAG, "enqueue: payload too large (%u), truncating to %u", (unsigned)len, (unsigned)INFLUX_MAX_PAYLOAD);
        len = INFLUX_MAX_PAYLOAD;
    }

    char *copy = malloc(len + 1);
    if (!copy)
    {
        LOGE(TAG, "enqueue: malloc failed for %u bytes", (unsigned)len);
        return false;
    }

    memcpy(copy, payload, len);
    copy[len] = '\0';

    influx_job_t job = {.payload = copy, .len = len};

    if (xQueueSend(influx_queue, &job, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        LOGW(TAG, "enqueue: queue full, dropping payload len=%u", (unsigned)len);
        free(copy);
        return false;
    }

    LOGD(TAG, "Enqueued Influx payload len=%u", (unsigned)len);
    return true;
}

// ---------------------------------------------------------------------------
// High-level process function (reworked to enqueue separate payloads)
// ---------------------------------------------------------------------------
void influxdb_process(QueueHandle_t sensor_q, QueueHandle_t avg_q)
{
    Influxdb_config_t cfg;
    influxdb_get_config_snapshot(&cfg);

    if (strcmp(cfg.enable_influxdb, "On") != 0)
        return;

    // Build and enqueue actuals
    char *actuals = malloc(2048);
    if (actuals)
    {
        memset(actuals, 0, 2048);
        influxdb_build_payload_actuals(actuals, 2048);
        if (strlen(actuals) > 0)
            influxdb_enqueue_payload(actuals, strlen(actuals));
        free(actuals);
    }

    // Build and enqueue valves
    char *valves = malloc(2048);
    if (valves)
    {
        memset(valves, 0, 2048);
        influxdb_build_payload_valves(valves, 2048);
        if (strlen(valves) > 0)
            influxdb_enqueue_payload(valves, strlen(valves));
        free(valves);
    }

    // Read sensor queues into local buffers
    float (*sensor)[CHANNELS_PER_BUS][DATA_FIELDS] = malloc(sizeof(float) * MAX_I2C_BUSES * CHANNELS_PER_BUS * DATA_FIELDS);
    float (*avg)[CHANNELS_PER_BUS][DATA_FIELDS] = malloc(sizeof(float) * MAX_I2C_BUSES * CHANNELS_PER_BUS * DATA_FIELDS);

    if (!sensor || !avg)
    {
        LOGE(TAG, "Memory allocation failed for sensors");
        if (sensor)
            free(sensor);
        if (avg)
            free(avg);
        return;
    }

    memset(sensor, 0, sizeof(float) * MAX_I2C_BUSES * CHANNELS_PER_BUS * DATA_FIELDS);
    memset(avg, 0, sizeof(float) * MAX_I2C_BUSES * CHANNELS_PER_BUS * DATA_FIELDS);

    if (xQueuePeek(sensor_q, sensor, 0) != pdTRUE || xQueuePeek(avg_q, avg, 0) != pdTRUE)
    {
        LOGW(TAG, "Failed to read sensor queues");
        free(sensor);
        free(avg);
        return;
    }

    // Build sensors per bus and enqueue
    for (int bus = 0; bus < MAX_I2C_BUSES; bus++)
    {
        char *sbuf = malloc(INFLUX_SENSORS_CHUNK);
        if (!sbuf)
        {
            LOGE(TAG, "Failed to allocate sensors chunk for bus %d", bus);
            continue;
        }
        memset(sbuf, 0, INFLUX_SENSORS_CHUNK);
        influxdb_build_payload_sensors_for_bus(sbuf, INFLUX_SENSORS_CHUNK, sensor, avg, bus);
        if (strlen(sbuf) > 0)
            influxdb_enqueue_payload(sbuf, strlen(sbuf));
        free(sbuf);
    }

    free(sensor);
    free(avg);
}
