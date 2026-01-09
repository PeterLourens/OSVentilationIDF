#include "influxdb.h"

static const char *TAG = "Influxdb";

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
// Get timestamp from DS3231 (UTC seconds) — ESP-IDF safe
// ---------------------------------------------------------------------------
int64_t influxdb_get_timestamp(void)
{
  struct tm rtc_time = {0};

  if (ds3231_get_time(ds3231_bus0_handle, &rtc_time) != ESP_OK)
  {
    // Fallback: system time if RTC read fails
    time_t now;
    time(&now);
    return (int64_t)now;
  }

  // Convert struct tm (UTC) → Unix timestamp manually
  int year = rtc_time.tm_year + 1900; // full year, e.g. 2026
  int month = rtc_time.tm_mon + 1;    // 1–12
  int day = rtc_time.tm_mday;
  int hour = rtc_time.tm_hour;
  int min = rtc_time.tm_min;
  int sec = rtc_time.tm_sec;

  // Shift Jan/Feb to be months 13/14 of previous year
  if (month <= 2)
  {
    year -= 1;
    month += 12;
  }

  // Days since 0000-03-01
  int64_t era = year / 400;
  int64_t yoe = year - era * 400;                      // [0, 399]
  int64_t doy = (153 * (month - 3) + 2) / 5 + day - 1; // [0, 365]
  int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy; // [0, 146096]

  // Days since Unix epoch (1970-01-01)
  int64_t days = era * 146097 + doe - 719468;

  int64_t ts = days * 86400LL + hour * 3600 + min * 60 + sec;

  return ts;
}

static void normalize_influx_url(char *url)
{
  size_t len = strlen(url);

  // Remove trailing slash
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

// ---------------------------------------------------------------------------
// Build full InfluxDB write URL
// ---------------------------------------------------------------------------
void influxdb_build_url(char *out, size_t out_size, const Influxdb_config_t *cfg)
{
  char base[256];
  strlcpy(base, cfg->influxdb_url, sizeof(base));
  normalize_influx_url(base);

  snprintf(out, out_size, "%s/api/v2/write?org=%s&bucket=%s&precision=s", base, cfg->influxdb_org, cfg->influxdb_bucket);
}

// ---------------------------------------------------------------------------
// Format a single line of line protocol
// ---------------------------------------------------------------------------
/*void influxdb_format_line(char *out, size_t out_size, int bus, int ch, float raw0, float raw1, float raw2, float avg0, float avg1, float avg2, int64_t ts)
{
  if (raw2 == 0.0f && avg2 == 0.0f)
  {
    snprintf(out, out_size,
             "sensors,bus=%d,channel=%d "
             "raw0=%.2f,raw1=%.2f,"
             "avg0=%.2f,avg1=%.2f %lld\n",
             bus, ch, raw0, raw1, avg0, avg1, ts);
  }
  else
  {
    snprintf(out, out_size, "sensors,bus=%d,channel=%d "
                            "raw0=%.2f,raw1=%.2f,raw2=%.2f,"
                            "avg0=%.2f,avg1=%.2f,avg2=%.2f %lld\n",
             bus, ch, raw0, raw1, raw2, avg0, avg1, avg2, ts);
  }
}*/

// ---------------------------------------------------------------------------
// Append a line to the payload buffer
// ---------------------------------------------------------------------------
void influxdb_append_line(char *out, size_t out_size, const char *line)
{
  size_t used = strlen(out);
  size_t free = out_size - used - 1;

  if (free == 0)
    return;

  size_t len = strlen(line);
  if (len > free)
    len = free;

  memcpy(out + used, line, len);
  out[used + len] = '\0';
}

// ---------------------------------------------------------------------------
// Build the full payload
// ---------------------------------------------------------------------------
void influxdb_build_payload(char *out, size_t out_size, float sensor[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS], float avg[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS])
{
  memset(out, 0, out_size);

  char line[256];
  memset(line, 0, sizeof(line));

  int64_t ts = influxdb_get_timestamp();

  // 🔒 Take mutex before reading ANY sensor or metadata
  xSemaphoreTake(sensor_cfg_mutex, portMAX_DELAY);

  for (int bus = 0; bus < MAX_I2C_BUSES; bus++)
  {
    for (int ch = 0; ch < CHANNELS_PER_BUS; ch++)
    {
      if (sensor[bus][ch][IDX_VALID] != 1)
        continue;

      const bus_sensor_t *meta =
          (bus == 0) ? &bus0_sensor_settings[ch]
                     : &bus1_sensor_settings[ch];

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
                       bus, ch,
                       sensor_id,
                       type_esc, loc_esc, valve_esc,
                       sensor[bus][ch][IDX_TEMP], avg[bus][ch][IDX_TEMP],
                       sensor[bus][ch][IDX_HUMIDITY], avg[bus][ch][IDX_HUMIDITY]);

      if (n < 0 || n >= sizeof(line))
        continue;

      if (sensor[bus][ch][IDX_CO2] > 0.0f)
      {
        n += snprintf(line + n, sizeof(line) - n,
                      ",co2_raw=%.2f,co2_avg=%.2f",
                      sensor[bus][ch][IDX_CO2], avg[bus][ch][IDX_CO2]);

        if (n < 0 || n >= sizeof(line))
          continue;
      }

      n += snprintf(line + n, sizeof(line) - n, " %lld\n", (long long)ts);

      if (n < 0 || n >= sizeof(line))
        continue;

      influxdb_append_line(out, out_size, line);
    }
  }

  // 🔓 Release mutex after snapshot is complete
  xSemaphoreGive(sensor_cfg_mutex);

  // ESP_LOGI(TAG, "Payload strlen: %d", strlen(out));
}

// ---------------------------------------------------------------------------
// Send payload to InfluxDB
// ---------------------------------------------------------------------------
void influxdb_send_payload(const char *payload, const Influxdb_config_t *cfg)
{
  char url[512];
  influxdb_build_url(url, sizeof(url), cfg);

  char auth_header[512];
  strlcpy(auth_header, "Token ", sizeof(auth_header));
  strlcat(auth_header, cfg->influxdb_token, sizeof(auth_header));

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

  int payload_len = strlen(payload);
  // ESP_LOGI(TAG, "Payload strlen: %d", payload_len);
  // ESP_LOGI(TAG, "Sending payload:\n%s", payload);

  // esp_http_client_set_post_field(client, payload, payload_len);
  esp_http_client_set_method(client, HTTP_METHOD_POST);
  esp_http_client_set_post_field(client, payload, payload_len);

  esp_err_t err = esp_http_client_perform(client);

  if (err == ESP_OK)
  {
    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "Influx write status: %d", status);

    // Read entire response body (JSON error or success)
    char resp[1024];
    int rlen = esp_http_client_read_response(client, resp, sizeof(resp) - 1);
    if (rlen > 0)
    {
      resp[rlen] = '\0';
      ESP_LOGI(TAG, "Influx response: %s", resp);
    }
    else
    {
      ESP_LOGI(TAG, "Influx response: <no body>, rlen=%d", rlen);
    }
  }
  else
  {
    ESP_LOGE(TAG, "HTTP error: %s", esp_err_to_name(err));
  }

  // ESP_LOGI(TAG, "DEBUG URL: '%s'", url);
  // ESP_LOGI(TAG, "DEBUG ORG: '%s'", cfg->influxdb_org);
  // ESP_LOGI(TAG, "DEBUG BUCKET: '%s'", cfg->influxdb_bucket);
  // ESP_LOGI(TAG, "DEBUG TOKEN: '%s'", cfg->influxdb_token);

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

  // Allocate on heap
  float (*sensor)[CHANNELS_PER_BUS][DATA_FIELDS] = malloc(sizeof(float) * MAX_I2C_BUSES * CHANNELS_PER_BUS * DATA_FIELDS);
  float (*avg)[CHANNELS_PER_BUS][DATA_FIELDS] = malloc(sizeof(float) * MAX_I2C_BUSES * CHANNELS_PER_BUS * DATA_FIELDS);
  char *payload = malloc(4096);

  if (!sensor || !avg || !payload)
  {
    ESP_LOGE(TAG, "Memory allocation failed");
    if (sensor)
      free(sensor);
    if (avg)
      free(avg);
    if (payload)
      free(payload);
    return;
  }

  memset(sensor, 0, sizeof(float) * MAX_I2C_BUSES * CHANNELS_PER_BUS * DATA_FIELDS);
  memset(avg, 0, sizeof(float) * MAX_I2C_BUSES * CHANNELS_PER_BUS * DATA_FIELDS);

  if (xQueuePeek(sensor_q, sensor, 0) != pdTRUE || xQueuePeek(avg_q, avg, 0) != pdTRUE)
  {
    free(sensor);
    free(avg);
    free(payload);
    return;
  }

  influxdb_build_payload(payload, 4096, sensor, avg);
  influxdb_send_payload(payload, &cfg);

  free(sensor);
  free(avg);
  free(payload);
}
