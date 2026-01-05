#include "influxdb.h"

#define TAG "influx_sender"

// ---------------------------------------------------------------------------
// Mutex-protected config snapshot
// ---------------------------------------------------------------------------
void influxdb_get_config_snapshot(Influxdb_config_t *out)
{
    xSemaphoreTake(influxdb_config_mutex, portMAX_DELAY);
    memcpy(out, &influxdb_config, sizeof(Influxdb_config_t));
    xSemaphoreGive(influxdb_config_mutex);
}

// ---------------------------------------------------------------------------
// Get timestamp from DS3231 (UTC seconds)
// ---------------------------------------------------------------------------
int64_t influxdb_get_timestamp(void)
{
    struct tm rtc_time = {0};

    if (ds3231_get_time(ds3231_bus0_handle, &rtc_time) != ESP_OK)
    {
        // Fallback: return system time if RTC fails
        time_t now;
        time(&now);
        return (int64_t)now;
    }

    // Convert struct tm → Unix timestamp (UTC)
    time_t ts = mktime(&rtc_time);

    return (int64_t)ts;
}

// ---------------------------------------------------------------------------
// Build full InfluxDB write URL
// ---------------------------------------------------------------------------
void influxdb_build_url(char *out, size_t out_size, const Influxdb_config_t *cfg)
{
    snprintf(
        out,
        out_size,
        "%s/api/v2/write?org=%s&bucket=%s&precision=s",
        cfg->influxdb_url,
        cfg->influxdb_org,
        cfg->influxdb_bucket);
}

// ---------------------------------------------------------------------------
// Format a single line of line protocol
// ---------------------------------------------------------------------------
void influxdb_format_line(
    char *out,
    size_t out_size,
    int bus,
    int ch,
    float raw0, float raw1, float raw2,
    float avg0, float avg1, float avg2,
    int64_t ts)
{
    snprintf(
        out,
        out_size,
        "sensors,bus=%d,channel=%d "
        "raw0=%.3f,raw1=%.3f,raw2=%.3f,"
        "avg0=%.3f,avg1=%.3f,avg2=%.3f %lld\n",
        bus,
        ch,
        raw0, raw1, raw2,
        avg0, avg1, avg2,
        ts);
}

// ---------------------------------------------------------------------------
// Append a line to the payload buffer
// ---------------------------------------------------------------------------
void influxdb_append_line(char *payload, size_t payload_size, const char *line)
{
    strlcat(payload, line, payload_size);
}

// ---------------------------------------------------------------------------
// Build the full payload (modular, extendable)
// ---------------------------------------------------------------------------
void influxdb_build_payload(
    char *out,
    size_t out_size,
    float sensor[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS],
    float avg[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS])
{
    out[0] = '\0';

    char line[128];
    int64_t ts = influxdb_get_timestamp();

    for (int bus = 0; bus < MAX_I2C_BUSES; bus++)
    {
        for (int ch = 0; ch < CHANNELS_PER_BUS; ch++)
        {
            influxdb_format_line(
                line,
                sizeof(line),
                bus,
                ch,
                sensor[bus][ch][0],
                sensor[bus][ch][1],
                sensor[bus][ch][2],
                avg[bus][ch][0],
                avg[bus][ch][1],
                avg[bus][ch][2],
                ts);

            influxdb_append_line(out, out_size, line);
        }
    }
}

// ---------------------------------------------------------------------------
// Send payload to InfluxDB
// ---------------------------------------------------------------------------
void influxdb_send_payload(const char *payload, const Influxdb_config_t *cfg)
{
    char url[1024];
    influxdb_build_url(url, sizeof(url), cfg);

    char auth_header[1024];
    snprintf(auth_header, sizeof(auth_header), "Token %s", cfg->influxdb_token);

    esp_http_client_config_t http_cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client)
    {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        return;
    }

    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Content-Type", "text/plain; charset=utf-8");
    esp_http_client_set_header(client, "Accept", "application/json");

    esp_http_client_set_post_field(client, payload, strlen(payload));

    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "Influx write status: %d", esp_http_client_get_status_code(client));
    }
    else
    {
        ESP_LOGE(TAG, "HTTP error: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

// ---------------------------------------------------------------------------
// High-level process function
// ---------------------------------------------------------------------------
void influxdb_process(QueueHandle_t sensor_q, QueueHandle_t avg_q)
{
    Influxdb_config_t cfg;
    influxdb_get_config_snapshot(&cfg);

    if (strcmp(cfg.enable_influxdb, "On") != 0)
        return;

    float sensor[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS] = {0};
    float avg[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS] = {0};

    if (xQueuePeek(sensor_q, &sensor, 0) != pdTRUE)
        return;

    if (xQueuePeek(avg_q, &avg, 0) != pdTRUE)
        return;

    char payload[4096];
    influxdb_build_payload(payload, sizeof(payload), sensor, avg);

    influxdb_send_payload(payload, &cfg);
}