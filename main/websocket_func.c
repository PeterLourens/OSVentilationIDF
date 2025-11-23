#include "websocket_func.h"

static const char *TAG = "ws func";

typedef struct
{
    float temp;
    float rh;
    float co2;
    uint32_t timestamp;
} sensor_value_t;

extern sensor_value_t sensor_data[MAX_I2C_BUSES][CHANNELS_PER_BUS];

void send_index_html_ws(void)
{
    sensor_value_t snapshot[MAX_I2C_BUSES][CHANNELS_PER_BUS];
    int valve_positions[MAX_VALVE_COUNT] = {0};

    // Peek latest sensor readings from queue
    if (!xQueuePeek(sensor_queue, &snapshot, 0))
    {
        ESP_LOGE(TAG, "No sensor data available in queue");
        return;
    }

    char buf[4096];
    size_t used = 0;

    // Valves
    used += snprintf(buf + used, sizeof(buf) - used, "{ \"valves\":[");
    for (int i = 0; i < MAX_VALVE_COUNT; i++)
    {
        used += snprintf(buf + used, sizeof(buf) - used, "%d%s",
                         valve_positions[i],
                         (i < MAX_VALVE_COUNT - 1) ? "," : "");
    }

    // Protect config while reading
    xSemaphoreTake(sensor_cfg_mutex, portMAX_DELAY);

    // Bus0
    used += snprintf(buf + used, sizeof(buf) - used, "], \"bus0\":[");
    for (int ch = 0; ch < CHANNELS_PER_BUS; ch++)
    {
        bus_sensor_t *cfg = &bus0_sensor_settings[ch];
        sensor_value_t *val = &snapshot[0][ch];
        used += snprintf(buf + used, sizeof(buf) - used,
                         "{\"type\":\"%s\",\"valve\":\"%s\",\"location\":\"%s\","
                         "\"rhs\":%d,\"co2s\":%d,"
                         "\"temp\":%.1f,\"rh\":%.1f,\"co2\":%.1f,\"ts\":%lu}%s",
                         cfg->type, cfg->valve, cfg->location,
                         cfg->rh_enabled, cfg->co2_enabled,
                         val->temp, val->rh, val->co2, val->timestamp,
                         (ch < CHANNELS_PER_BUS - 1) ? "," : "");
    }

    // Bus1
    used += snprintf(buf + used, sizeof(buf) - used, "], \"bus1\":[");
    for (int ch = 0; ch < CHANNELS_PER_BUS; ch++)
    {
        bus_sensor_t *cfg = &bus1_sensor_settings[ch];
        sensor_value_t *val = &snapshot[1][ch];
        used += snprintf(buf + used, sizeof(buf) - used,
                         "{\"type\":\"%s\",\"valve\":\"%s\",\"location\":\"%s\","
                         "\"rhs\":%d,\"co2s\":%d,"
                         "\"temp\":%.1f,\"rh\":%.1f,\"co2\":%.1f,\"ts\":%lu}%s",
                         cfg->type, cfg->valve, cfg->location,
                         cfg->rh_enabled, cfg->co2_enabled,
                         val->temp, val->rh, val->co2, val->timestamp,
                         (ch < CHANNELS_PER_BUS - 1) ? "," : "");
    }

    xSemaphoreGive(sensor_cfg_mutex);

    used += snprintf(buf + used, sizeof(buf) - used, "] }");

    // Send JSON over WebSocket
    ws_async_send(buf, used);
}