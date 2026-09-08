/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

/**
 * @file influxdb.c
 * @brief InfluxDB payload building and send-task implementation.
 */

#include "influxdb.h"

static const char *TAG = "influxdb";

QueueHandle_t influx_queue = NULL;
TaskHandle_t influx_sender_task_handle = NULL;
static volatile bool influx_sender_stop_requested = false;

//   Configuration
#define INFLUX_QUEUE_LEN 32
#define INFLUX_MAX_PAYLOAD 8192
#define INFLUX_SENSORS_CHUNK 2048
#define INFLUX_SENDER_STACK 8192
#define INFLUX_SENDER_PRIO 5

// --- DMA monitor configuration (influxdb.c) ---
uint32_t g_influx_task_interval_ms = 20000;   // keep default 20s (matches existing vTaskDelay)
uint32_t g_dma_monitor_interval_ms = 5000;    // default 5s for DMA monitor
uint32_t g_monitor_tick_resolution_ms = 1000; // resolution for the integrated loop (1s)
static int g_dma_monitor_multiplier = 1;

// Queue item and globals
typedef struct
{
    char *payload; // heap-allocated, null-terminated
    size_t len;
} influx_job_t;

/**
 * @brief Wait for the InfluxDB queue to become ready.
 *
 * @param timeout_ticks Number of RTOS ticks to wait.
 * @return true if ready before timeout, false otherwise.
 */
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

/**
 * @brief Escape InfluxDB tag values for safe line protocol formatting.
 *
 * @param[out] dst Destination buffer.
 * @param dst_size Destination buffer size.
 * @param[in] src Source string to escape.
 */
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

/**
 * @brief Build and enqueue an InfluxDB line for a single sensor.
 *
 * @param bus Sensor bus index.
 * @param ch Sensor channel index.
 * @param sensor_val Raw measurement values.
 * @param avg_val Averaged measurement values.
 * @return true if the payload was successfully enqueued.
 */
bool influxdb_enqueue_sensor_line(int bus, int ch, float sensor_val[DATA_FIELDS], float avg_val[DATA_FIELDS])
{
    if (ch < 0)
        return false;

    // Small stack buffer per sensor; increase if you expect very long tag strings.
    char line[512];
    memset(line, 0, sizeof(line));

    // Prepare tags (escaped)
    char type_esc[64] = {0};
    char valve_esc[64] = {0};
    char loc_esc[128] = {0};

    // You must provide a way to get meta for the sensor (same as in build_payload_sensors_for_bus)
    const bus_sensor_t *meta = (bus == 0) ? &bus0_sensor_settings[ch] : &bus1_sensor_settings[ch];

    escape_tag(type_esc, sizeof(type_esc), meta->type);
    escape_tag(valve_esc, sizeof(valve_esc), meta->valve);
    escape_tag(loc_esc, sizeof(loc_esc), meta->location);

    if (type_esc[0] == '\0')
        strcpy(type_esc, "None");
    if (loc_esc[0] == '\0')
        strcpy(loc_esc, "None");
    if (valve_esc[0] == '\0')
        strcpy(valve_esc, "None");

    char sensor_id[32];
    snprintf(sensor_id, sizeof(sensor_id), "bus%d_ch%d", bus, ch);

    int64_t ts = influxdb_get_timestamp();

    // Build the line. Keep format consistent with your existing code.
    // If CO2 is present, append those fields.
    int n = snprintf(line, sizeof(line),
                     "sensors,bus=%d,channel=%d,sensor_id=%s,type=%s,location=%s,valve=%s "
                     "temperature_raw=%.2f,temperature_avg=%.2f,humidity_raw=%.2f,humidity_avg=%.2f",
                     bus, ch, sensor_id, type_esc, loc_esc, valve_esc,
                     sensor_val[IDX_TEMP], avg_val[IDX_TEMP],
                     sensor_val[IDX_HUMIDITY], avg_val[IDX_HUMIDITY]);

    if (n < 0 || n >= (int)sizeof(line))
    {
        ESP_LOGW(TAG, "sensor line truncated for bus=%d ch=%d (n=%d buf=%zu)", bus, ch, n, sizeof(line));
        // If truncated, still attempt to enqueue what we have (or skip)
    }
    else
    {
        // Append CO2 fields if present
        if (sensor_val[IDX_CO2] > 0.0f)
        {
            int m = snprintf(line + n, sizeof(line) - n, ",co2_raw=%.2f,co2_avg=%.2f", sensor_val[IDX_CO2], avg_val[IDX_CO2]);
            if (m < 0 || m >= (int)(sizeof(line) - n))
            {
                ESP_LOGW(TAG, "sensor CO2 append truncated for bus=%d ch=%d", bus, ch);
            }
            else
            {
                n += m;
            }
        }

        // Append timestamp and newline
        int t = snprintf(line + n, sizeof(line) - n, " %lld\n", (long long)ts);
        if (t < 0 || t >= (int)(sizeof(line) - n))
        {
            ESP_LOGW(TAG, "sensor timestamp append truncated for bus=%d ch=%d", bus, ch);
        }
    }

    // Enqueue the single-line payload
    if (strlen(line) == 0)
        return false;
    return influxdb_enqueue_payload(line, strlen(line));
}

// Setter/getter for runtime configuration
/**
 * @brief Set the DMA monitor reporting interval.
 *
 * @param ms Interval in milliseconds.
 */
void influxdb_set_dma_monitor_interval_ms(uint32_t ms)
{
    if (ms < 100)
        ms = 100; // enforce a sane minimum
    g_dma_monitor_interval_ms = ms;
}

/**
 * @brief Get the current DMA monitor reporting interval.
 *
 * @return Interval in milliseconds.
 */
uint32_t influxdb_get_dma_monitor_interval_ms(void)
{
    return g_dma_monitor_interval_ms;
}

// Optional: set the base influx task interval if you want to change it programmatically
/**
 * @brief Set the base InfluxDB task interval.
 *
 * @param ms Interval in milliseconds.
 */
void influxdb_set_influx_task_interval_ms(uint32_t ms)
{
    if (ms < 1000)
        ms = 1000;
    g_influx_task_interval_ms = ms;
}

/**
 * @brief Get the current base InfluxDB task interval.
 *
 * @return Interval in milliseconds.
 */
uint32_t influxdb_get_influx_task_interval_ms(void)
{
    return g_influx_task_interval_ms;
}

// Mutex-protected config snapshot
/**
 * @brief Copy the current InfluxDB configuration into the provided snapshot.
 *
 * @param[out] out Output configuration snapshot.
 */
void influxdb_get_config_snapshot(Influxdb_config_t *out)
{
    xSemaphoreTake(influxdb_config_mutex, portMAX_DELAY);
    memcpy(out, &influxdb_config, sizeof(Influxdb_config_t));
    xSemaphoreGive(influxdb_config_mutex);
}

// Get timestamp from DS3231 (UTC seconds) — ESP-IDF safe
/**
 * @brief Retrieve a nanosecond-resolution timestamp for InfluxDB line protocol.
 *
 * @return Timestamp in nanoseconds since the Unix epoch.
 */
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

/**
 * @brief Normalize an InfluxDB base URL by removing any trailing slash.
 *
 * @param[in,out] url URL buffer to normalize.
 */
static void normalize_influx_url(char *url)
{
    size_t len = strlen(url);
    while (len > 0 && url[len - 1] == '/')
    {
        url[len - 1] = '\0';
        len--;
    }
}

/**
 * @brief Build the InfluxDB write endpoint URL from configuration.
 *
 * @param[out] out URL buffer.
 * @param out_size Size of the URL buffer.
 * @param[in] cfg Configuration snapshot.
 */
void influxdb_build_url(char *out, size_t out_size, const Influxdb_config_t *cfg)
{
    char base[256];
    strlcpy(base, cfg->influxdb_url, sizeof(base));
    normalize_influx_url(base);
    snprintf(out, out_size, "%s/api/v2/write?org=%s&bucket=%s&precision=ns", base, cfg->influxdb_org, cfg->influxdb_bucket);
}

/**
 * @brief Append a single line to the InfluxDB payload buffer.
 *
 * @param[out] out Payload buffer.
 * @param out_size Buffer size.
 * @param[in] line Line text to append.
 */
void influxdb_append_line(char *out, size_t out_size, const char *line)
{
    if (!out || out_size == 0 || !line)
        return;

    size_t used = strlen(out);
    if (used >= out_size - 1)
        return;

    size_t out_free = out_size - used - 1;
    size_t len = strlen(line);

    if (len > out_free)
    {
        LOGE(TAG, "Influx payload overflow: line needs %zu bytes, free %zu", len, out_free);
        len = out_free;
    }

    memcpy(out + used, line, len);
    out[used + len] = '\0';
}

/**
 * @brief Build a DMA metrics payload line for InfluxDB.
 *
 * @param[out] out Payload buffer.
 * @param out_size Buffer size.
 * @param[in] host_tag Optional host identifier tag.
 */
void influxdb_build_payload_dma(char *out, size_t out_size, const char *host_tag)
{
    if (!out || out_size == 0)
        return;

    uint32_t dma_free = heap_caps_get_free_size(MALLOC_CAP_DMA);
    uint32_t heap_free = esp_get_free_heap_size();
    uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    int64_t ts = influxdb_get_timestamp();

    int n = snprintf(out, out_size,
                     "esp_dma,host=%s dma_free=%u,heap_free=%u,largest=%u %lld\n",
                     host_tag ? host_tag : "osvent",
                     (unsigned)dma_free, (unsigned)heap_free, (unsigned)largest,
                     (long long)ts);
    if (n < 0 || (size_t)n >= out_size)
        out[0] = '\0';
}

// Build and enqueue a DMA metric payload using existing enqueue helper.
// Returns true if enqueue succeeded.
/**
 * @brief Enqueue DMA metrics payload for sending.
 *
 * @param host_tag Optional host tag.
 * @return true if the payload was enqueued successfully.
 */
bool influxdb_enqueue_dma_metrics(const char *host_tag)
{
    char buf[DMA_MONITOR_PAYLOAD_SZ];
    memset(buf, 0, sizeof(buf));
    influxdb_build_payload_dma(buf, sizeof(buf), host_tag);
    if (buf[0] == '\0')
        return false;

    // influxdb_enqueue_payload copies the payload into the queue, so passing stack buffer is fine.
    return influxdb_enqueue_payload(buf, strlen(buf));
}

// Called from the InfluxDB task loop. Maintains an internal counter and enqueues DMA metrics
// only when the configured multiplier condition is met. Returns true if a DMA metric was enqueued.
/**
 * @brief Tick the DMA monitor and enqueue metrics on the configured interval.
 *
 * @return true if a DMA metric payload was enqueued.
 */
bool influxdb_dma_monitor_tick(void)
{
    static uint32_t tick = 0;
    tick++;

    // Avoid division by zero
    int mult = g_dma_monitor_multiplier > 0 ? g_dma_monitor_multiplier : 1;

    if ((tick % (uint32_t)mult) == 0)
    {
        // Use a short host tag; replace with dynamic node id if you want (ensure thread-safety)
        const char *host_tag = "osvent";
        bool ok = influxdb_enqueue_dma_metrics(host_tag);

        // Local debug log (keeps output small)
        uint32_t dma_free = heap_caps_get_free_size(MALLOC_CAP_DMA);
        uint32_t heap_free = esp_get_free_heap_size();
        uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        ESP_LOGD("influxdb", "dma_monitor_tick: enqueued=%d dma_free=%u heap_free=%u largest=%u", ok ? 1 : 0, dma_free, heap_free, largest);

        if (dma_free < 8000)
        {
            ESP_LOGW("influxdb", "dma_monitor_tick: DMA low: %u bytes", dma_free);
        }

        return ok;
    }

    return false;
}

// Replace influxdb_build_payload_actuals
/**
 * @brief Build an InfluxDB payload for system actuals.
 *
 * @param[out] out Payload buffer.
 * @param out_size Buffer size.
 */
void influxdb_build_payload_actuals(char *out, size_t out_size)
{
    if (!out || out_size == 0)
        return;

    int64_t ts = influxdb_get_timestamp();
    uint64_t uptime = (uint64_t)(esp_timer_get_time() / 1000000ULL);

    if (xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(500)) != pdTRUE)
    {
        LOGW(TAG, "influxdb_build_payload_actuals: statemachine_mutex unavailable, skipping actuals payload");
        return;
    }

    char line[2048];
    memset(line, 0, sizeof(line));

    uint32_t heap_free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    uint32_t heap_free_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    uint32_t heap_free_dma = heap_caps_get_free_size(MALLOC_CAP_DMA);
    uint32_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    uint32_t largest_spiram = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    uint32_t largest_dma = heap_caps_get_largest_free_block(MALLOC_CAP_DMA);

    int n = snprintf(line, sizeof(line),
                     "actuals "
                     "fanspeed=%d,"
                     "state=%d,"
                     "co2_sensors_high=%d,"
                     "rh_sensors_high=%d,"
                     "valve_lock=%s,"
                     "wifi_error=%s,"
                     "wifi_connected=%s,"
                     "wifi_ap_mode=%s,"
                     "ethernet_connected=%s,"
                     "ethernet_enabled=%s,"
                     "wifi_rssi=%d,"
                     "wifi_channel=%d,"
                     "wifi_authmode=%d,"
                     "wifi_phy=%d,"
                     "wifi_bandwidth=%d,"
                     "cpu_temp=%d,"
                     "uptime=%llu,"
                     "heap_free=%u,"
                     "heap_free_internal=%u,"
                     "heap_free_spiram=%u,"
                     "heap_free_dma=%u,"
                     "heap_largest_free_block_internal=%u,"
                     "heap_largest_free_block_spiram=%u,"
                     "heap_largest_free_block_dma=%u,"
                     "heap_min_ever=%u,"
                     "stack_i2c_sensors=%lu,"
                     "stack_i2c_ds3231=%lu,"
                     "stack_network_manager=%lu,"
                     "stack_i2c_lcd=%lu,"
                     "stack_rgb_leds=%lu,"
                     "stack_statemachine=%lu,"
                     "stack_valvectrl=%lu,"
                     "stack_mqtt=%lu,"
                     "stack_influxdb=%lu,"
                     "stack_pushbutton=%lu,"
                     "stack_sys_stats=%lu,"
                     "stack_syslog=%lu,"
                     "stack_uart_sender=%lu "
                     "%lld\n",
                     sm_actuals.fanspeed,
                     sm_actuals.state,
                     sm_actuals.co2_sensors_high,
                     sm_actuals.rh_sensors_high,
                     sm_actuals.valve_lock ? "true" : "false",
                     sm_actuals.wifi_error ? "true" : "false",
                     sm_actuals.wifi_connected ? "true" : "false",
                     sm_actuals.wifi_ap_mode ? "true" : "false",
                     sm_actuals.eth_connected ? "true" : "false",
                     sm_actuals.ethernet_enabled ? "true" : "false",
                     sm_actuals.wifi_rssi,
                     sm_actuals.wifi_channel,
                     sm_actuals.wifi_authmode,
                     sm_actuals.wifi_phy,
                     sm_actuals.wifi_bandwidth,
                     sm_actuals.cpu_temp,
                     (unsigned long long)uptime,
                     (unsigned)sm_actuals.heap_free,
                     (unsigned)heap_free_internal,
                     (unsigned)heap_free_spiram,
                     (unsigned)heap_free_dma,
                     (unsigned)largest_internal,
                     (unsigned)largest_spiram,
                     (unsigned)largest_dma,
                     (unsigned)sm_actuals.heap_min_ever,
                     (unsigned long)sm_actuals.stack_i2c_sensors,
                     (unsigned long)sm_actuals.stack_i2c_ds3231,
                     (unsigned long)sm_actuals.stack_network_manager,
                     (unsigned long)sm_actuals.stack_i2c_lcd,
                     (unsigned long)sm_actuals.stack_rgb_leds,
                     (unsigned long)sm_actuals.stack_statemachine,
                     (unsigned long)sm_actuals.stack_valvectrl,
                     (unsigned long)sm_actuals.stack_mqtt,
                     (unsigned long)sm_actuals.stack_influxdb,
                     (unsigned long)sm_actuals.stack_pushbutton,
                     (unsigned long)sm_actuals.stack_sys_stats,
                     (unsigned long)sm_actuals.stack_syslog,
                     (unsigned long)sm_actuals.stack_uart_sender,
                     (long long)ts);

    if (n < 0 || n >= (int)sizeof(line))
    {
        LOGW(TAG, "influxdb_build_payload_actuals: payload truncated (%d)", n);
        xSemaphoreGive(statemachine_mutex);
        return;
    }

    influxdb_append_line(out, out_size, line);
    xSemaphoreGive(statemachine_mutex);
}

// Build an uptime-only InfluxDB payload so the uptime field updates even when actuals is skipped.
/**
 * @brief Build an uptime-only InfluxDB payload.
 *
 * @param[out] out Payload buffer.
 * @param out_size Buffer size.
 */
void influxdb_build_payload_uptime(char *out, size_t out_size)
{
    if (!out || out_size == 0)
        return;

    int64_t ts = influxdb_get_timestamp();
    uint64_t uptime = (uint64_t)(esp_timer_get_time() / 1000000ULL);

    char line[128];
    int n = snprintf(line, sizeof(line), "actuals uptime=%llu %lld\n",
                     (unsigned long long)uptime,
                     (long long)ts);
    if (n < 0 || n >= (int)sizeof(line))
    {
        LOGW(TAG, "influxdb_build_payload_uptime: payload truncated (%d)", n);
        return;
    }

    influxdb_append_line(out, out_size, line);
}

// Replace influxdb_build_payload_valves
/**
 * @brief Build an InfluxDB payload describing valve positions.
 *
 * @param[out] out Payload buffer.
 * @param out_size Buffer size.
 */
void influxdb_build_payload_valves(char *out, size_t out_size)
{
    if (!out || out_size == 0)
        return;

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

    for (int i = 0; i < MAX_VALVE_COUNT; ++i)
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

        int n = snprintf(line, sizeof(line), "valve_positions,valve=%s position=%ld %lld\n",
                         valve_name, (long)pos, (long long)ts);
        if (n > 0)
            influxdb_append_line(out, out_size, line);
    }

    nvs_close(nvs);
}

// Build sensors payload for a single bus (keeps chunks small)
/**
 * @brief Build InfluxDB sensor payloads for a single bus.
 *
 * @param[out] out Payload buffer.
 * @param out_size Buffer size.
 * @param sensor Raw sensor snapshot array.
 * @param avg Averaged sensor snapshot array.
 * @param bus Sensor bus index.
 */
void influxdb_build_payload_sensors_for_bus(char *out, size_t out_size, float sensor[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS], float avg[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS], int bus)
{
    if (!out || out_size == 0 || bus < 0 || bus >= MAX_I2C_BUSES)
        return;

    char line[512];
    memset(line, 0, sizeof(line));

    int64_t ts = influxdb_get_timestamp();

    if (xSemaphoreTake(sensor_cfg_mutex, pdMS_TO_TICKS(200)) != pdTRUE)
        return;

    for (int ch = 0; ch < CHANNELS_PER_BUS; ++ch)
    {
        if (sensor[bus][ch][IDX_VALID] != 1.0f)
            continue;

        const bus_sensor_t *meta = (bus == 0) ? &bus0_sensor_settings[ch] : &bus1_sensor_settings[ch];

        char type_esc[64] = {0};
        char valve_esc[64] = {0};
        char loc_esc[128] = {0};

        escape_tag(type_esc, sizeof(type_esc), meta->type);
        escape_tag(valve_esc, sizeof(valve_esc), meta->valve);
        escape_tag(loc_esc, sizeof(loc_esc), meta->location);

        if (type_esc[0] == '\0')
            strcpy(type_esc, "None");
        if (loc_esc[0] == '\0')
            strcpy(loc_esc, "None");
        if (valve_esc[0] == '\0')
            strcpy(valve_esc, "None");

        char sensor_id[32];
        snprintf(sensor_id, sizeof(sensor_id), "bus%d_ch%d", bus, ch);

        int n = snprintf(line, sizeof(line),
                         "sensors,bus=%d,channel=%d,sensor_id=%s,type=%s,location=%s,valve=%s "
                         "temperature_raw=%.2f,temperature_avg=%.2f,humidity_raw=%.2f,humidity_avg=%.2f",
                         bus, ch, sensor_id, type_esc, loc_esc, valve_esc,
                         sensor[bus][ch][IDX_TEMP], avg[bus][ch][IDX_TEMP],
                         sensor[bus][ch][IDX_HUMIDITY], avg[bus][ch][IDX_HUMIDITY]);

        if (sensor[bus][ch][IDX_CO2] > 0.0f)
        {
            n += snprintf(line + n, sizeof(line) - n, ",co2_raw=%.2f,co2_avg=%.2f",
                          sensor[bus][ch][IDX_CO2], avg[bus][ch][IDX_CO2]);
        }

        if (n > 0)
        {
            int m = snprintf(line + n, sizeof(line) - n, " %lld\n", (long long)ts);
            if (m > 0)
                influxdb_append_line(out, out_size, line);
        }
    }

    xSemaphoreGive(sensor_cfg_mutex);
}

// Low-level send (returns ESP_OK on HTTP 2xx)
/**
 * @brief Send a raw InfluxDB payload using HTTP POST.
 *
 * @param[in] payload Payload string to send.
 * @param[in] cfg InfluxDB configuration snapshot.
 * @return ESP_OK for successful 2xx response, otherwise an esp_err_t.
 */
static esp_err_t influxdb_send_payload_internal(const char *payload, const Influxdb_config_t *cfg)
{
    if (!network_has_ip())
    {
        LOGW(TAG, "InfluxDB: Cannot send, no active network interface");
        return ESP_ERR_INVALID_STATE;
    }

    if (!payload || !cfg)
    {
        LOGW(TAG, "send_internal: invalid args");
        return ESP_ERR_INVALID_ARG;
    }

    char url[1024] = {0};
    influxdb_build_url(url, sizeof(url), cfg);

    esp_http_client_config_t http_cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 15000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client)
    {
        LOGE(TAG, "send_internal: esp_http_client_init failed");
        return ESP_FAIL;
    }

    char auth_header[600] = {0};
    snprintf(auth_header, sizeof(auth_header), "Token %s", cfg->influxdb_token);

    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_header(client, "Content-Type", "text/plain; charset=utf-8");
    esp_http_client_set_header(client, "Accept", "application/json");

    LOGD(TAG, "HTTP POST to: %s", url);
    LOGD(TAG, "HTTP payload preview: %.256s", payload);

    esp_err_t err = esp_http_client_set_post_field(client, payload, (int)strlen(payload));
    if (err != ESP_OK)
    {
        LOGW(TAG, "esp_http_client_set_post_field failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    err = esp_http_client_perform(client);
    int status = 0;
    if (err == ESP_OK)
    {
        status = esp_http_client_get_status_code(client);
        LOGD(TAG, "HTTP status: %d", status);

        // Read response body safely
        char resp[512];
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
        return err;

    if (status < 200 || status >= 300)
    {
        LOGW(TAG, "Influx write returned non-2xx status: %d", status);
        return ESP_FAIL;
    }

    return ESP_OK;
}

// Send with retry/backoff (synchronous, called from sender task)
/**
 * @brief Send an InfluxDB payload with retry/backoff.
 *
 * @param payload Payload string to send.
 * @param cfg InfluxDB configuration snapshot.
 */
static void influxdb_send_with_retry(const char *payload, const Influxdb_config_t *cfg)
{
    if (!payload || !cfg)
        return;

    const int max_retries = 4;
    int attempt = 0;
    int backoff_ms = 500;

    while (attempt <= max_retries)
    {
        if (influx_sender_stop_requested || !network_has_ip())
            return;

        esp_err_t err = influxdb_send_payload_internal(payload, cfg);
        if (err == ESP_OK)
        {
            LOGD(TAG, "Influx write succeeded (attempt %d)", attempt + 1);
            return;
        }

        LOGW(TAG, "Influx write failed (attempt %d): %s", attempt + 1, esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(backoff_ms));
        backoff_ms = backoff_ms * 2;
        attempt++;
    }

    LOGE(TAG, "Influx write permanently failed after %d attempts", max_retries + 1);
}

// Sender task: serializes HTTP writes
/**
 * @brief FreeRTOS task that dequeues InfluxDB payloads and sends them over HTTP.
 *
 * @param pv Unused task parameter.
 */
static void influx_sender_task(void *pv)
{
    Influxdb_config_t cfg;
    influxdb_get_config_snapshot(&cfg);

    while (!network_has_ip())
    {
        if (influx_sender_stop_requested)
        {
            LOGI(TAG, "InfluxDB sender stop requested before network ready");
            break;
        }

        LOGW(TAG, "InfluxDB: No network available, waiting...");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    if (influx_sender_stop_requested)
    {
        influx_sender_task_handle = NULL;
        vTaskDelete(NULL);
    }

    for (;;)
    {
        if (influx_sender_stop_requested)
        {
            LOGI(TAG, "InfluxDB sender task exiting on stop request");
            break;
        }

        influx_job_t job = {0};
        if (xQueueReceive(influx_queue, &job, pdMS_TO_TICKS(250)) == pdTRUE)
        {
            if (!job.payload)
            {
                LOGI(TAG, "InfluxDB sender got stop sentinel");
                break;
            }

            influxdb_get_config_snapshot(&cfg);

            // Do not hold a queued payload while waiting for an interface to
            // recover. Drop it and return to the queue so the sender can
            // resume normally after Ethernet or Wi-Fi becomes active.
            if (!network_has_ip())
            {
                heap_caps_free(job.payload);
                continue;
            }

            if (strcmp(cfg.enable_influxdb, "On") != 0)
            {
                LOGD(TAG, "InfluxDB disabled; dropping payload");
                heap_caps_free(job.payload);
                continue;
            }

            influxdb_send_with_retry(job.payload, &cfg);
            heap_caps_free(job.payload);
        }
    }

    influx_sender_task_handle = NULL;
    vTaskDelete(NULL);
}

// Public: initialize sender (call once at startup)
/**
 * @brief Initialize the InfluxDB sender subsystem.
 *
 * @return true if initialization succeeded.
 */
bool influxdb_start_sender(void)
{
    Influxdb_config_t cfg;
    influxdb_get_config_snapshot(&cfg);

    if (strcmp(cfg.enable_influxdb, "On") != 0)
    {
        LOGI(TAG, "InfluxDB sender disabled by config; queue/task not started");
        influxdb_stop_sender();
        return false;
    }

    if (influx_queue != NULL && influx_sender_task_handle != NULL)
    {
        LOGI(TAG, "influxdb_start_sender: already initialized (queue=%p, task=%p)", (void *)influx_queue, (void *)influx_sender_task_handle);
        return true;
    }

    if (!network_has_ip())
    {
        LOGW(TAG, "InfluxDB sender deferred until network is ready");
        return false;
    }

    influx_sender_stop_requested = false;

    if (influx_queue == NULL)
    {
        influx_queue = xQueueCreate(INFLUX_QUEUE_LEN, sizeof(influx_job_t));
        if (!influx_queue)
        {
            LOGE(TAG, "influxdb_start_sender: failed to create queue");
            return false;
        }
    }

    if (influx_sender_task_handle == NULL)
    {
        BaseType_t r = xTaskCreate(influx_sender_task, "influx_sender", INFLUX_SENDER_STACK, NULL, INFLUX_SENDER_PRIO, &influx_sender_task_handle);
        if (r != pdTRUE)
        {
            LOGE(TAG, "influxdb_start_sender: failed to create sender task");
            vQueueDelete(influx_queue);
            influx_queue = NULL;
            return false;
        }
    }

    LOGI(TAG, "influxdb_start_sender: initialized (queue=%p, task=%p)", (void *)influx_queue, (void *)influx_sender_task_handle);
    return true;
}

void influxdb_stop_sender(void)
{
    influx_sender_stop_requested = true;

    if (influx_queue != NULL && influx_sender_task_handle != NULL)
    {
        influx_job_t sentinel = {0};
        sentinel.payload = NULL;
        sentinel.len = 0;
        xQueueSend(influx_queue, &sentinel, pdMS_TO_TICKS(100));
    }

    if (influx_sender_task_handle != NULL)
    {
        LOGI(TAG, "Stopping InfluxDB sender task");

        TickType_t start = xTaskGetTickCount();
        while (influx_sender_task_handle != NULL && (xTaskGetTickCount() - start) < pdMS_TO_TICKS(2000))
        {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    // Keep the queue allocated across configuration changes. This prevents
    // a producer or a delayed sender wake-up from accessing freed queue memory.
}

bool influxdb_init_sender(void)
{
    return influxdb_start_sender();
}

// Enqueue helper (non-blocking with short timeout)
/**
 * @brief Enqueue a raw InfluxDB line protocol payload for sending.
 *
 * @param payload Null-terminated payload string.
 * @param len Length of the payload string.
 * @return true if queued successfully.
 */
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
        LOGW(TAG, "enqueue: payload too large (%zu), truncating to %d", len, INFLUX_MAX_PAYLOAD);
        len = INFLUX_MAX_PAYLOAD;
    }

    char *copy = heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!copy)
    {
        LOGE(TAG, "enqueue: heap_caps_malloc failed for %zu bytes", len);
        return false;
    }

    memcpy(copy, payload, len);
    copy[len] = '\0';

    influx_job_t job = {.payload = copy, .len = len};

    if (xQueueSend(influx_queue, &job, pdMS_TO_TICKS(1000)) != pdTRUE)
    {
        LOGW(TAG, "enqueue: queue full after waiting 1s, dropping payload len=%zu", len);
        heap_caps_free(copy);
        return false;
    }

    LOGD(TAG, "Enqueued Influx payload len=%zu", len);
    return true;
}

// High-level process function (reworked to enqueue separate payloads)
/**
 * @brief Process sensor and system data into InfluxDB payloads for enqueueing.
 *
 * @param sensor_q Handle to the raw sensor queue.
 * @param avg_q Handle to the averaged sensor queue.
 */
void influxdb_process(QueueHandle_t sensor_q, QueueHandle_t avg_q)
{
    Influxdb_config_t cfg;
    influxdb_get_config_snapshot(&cfg);

    if (strcmp(cfg.enable_influxdb, "On") != 0)
        return;

    // Build and enqueue uptime-only actuals first, to keep uptime updating even when full state is delayed.
    char *uptime_payload = heap_caps_malloc(256, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (uptime_payload)
    {
        memset(uptime_payload, 0, 256);
        influxdb_build_payload_uptime(uptime_payload, 256);
        if (strlen(uptime_payload) > 0)
        {
            if (!influxdb_enqueue_payload(uptime_payload, strlen(uptime_payload)))
            {
                LOGW(TAG, "influxdb_process: failed to enqueue uptime payload");
            }
        }
        heap_caps_free(uptime_payload);
    }

    // Build and enqueue actuals
    char *actuals = heap_caps_malloc(2048, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (actuals)
    {
        memset(actuals, 0, 2048);
        influxdb_build_payload_actuals(actuals, 2048);
        if (strlen(actuals) > 0)
        {
            if (!influxdb_enqueue_payload(actuals, strlen(actuals)))
            {
                LOGW(TAG, "influxdb_process: failed to enqueue actuals payload");
            }
        }
        else
        {
            LOGW(TAG, "influxdb_process: actuals payload empty, skipping actuals write");
        }
        heap_caps_free(actuals);
    }

    // Build and enqueue valves
    char *valves = heap_caps_malloc(2048, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (valves)
    {
        memset(valves, 0, 2048);
        influxdb_build_payload_valves(valves, 2048);
        if (strlen(valves) > 0)
            influxdb_enqueue_payload(valves, strlen(valves));
        heap_caps_free(valves);
    }

    // Allocate sensor buffers
    size_t buf_bytes = sizeof(float) * MAX_I2C_BUSES * CHANNELS_PER_BUS * DATA_FIELDS;
    float (*sensor)[CHANNELS_PER_BUS][DATA_FIELDS] = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    float (*avg)[CHANNELS_PER_BUS][DATA_FIELDS] = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (!sensor || !avg)
    {
        LOGE(TAG, "Memory allocation failed for sensors");
        heap_caps_free(sensor);
        heap_caps_free(avg);
        return;
    }

    memset(sensor, 0, buf_bytes);
    memset(avg, 0, buf_bytes);

    if (!sensor_q || !avg_q)
    {
        LOGW(TAG, "influxdb_process: sensor queues not provided");
        heap_caps_free(sensor);
        heap_caps_free(avg);
        return;
    }

    if (xQueuePeek(sensor_q, sensor, 0) != pdTRUE || xQueuePeek(avg_q, avg, 0) != pdTRUE)
    {
        LOGW(TAG, "Failed to read sensor queues");
        heap_caps_free(sensor);
        heap_caps_free(avg);
        return;
    }

    // Send each sensor as a separate Influx line to avoid large allocations
    for (int bus = 0; bus < MAX_I2C_BUSES; ++bus)
    {
        for (int ch = 0; ch < CHANNELS_PER_BUS; ++ch)
        {
            // Skip invalid sensors (same check as before)
            if (sensor[bus][ch][IDX_VALID] != 1.0f)
                continue;

            // Build and enqueue one sensor line
            bool ok = influxdb_enqueue_sensor_line(bus, ch, sensor[bus][ch], avg[bus][ch]);
            if (!ok)
            {
                ESP_LOGW(TAG, "Failed to enqueue sensor line bus=%d ch=%d", bus, ch);
            }
        }
    }

    heap_caps_free(sensor);
    heap_caps_free(avg);
}
