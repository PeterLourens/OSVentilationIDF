#include "websocket_func.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "ws func";

typedef struct
{
    float temp;
    float rh;
    float co2;
    uint32_t timestamp;
} sensor_value_t;

/* removed unused file-scope statics:
   snapshot_static, local_bus0_static, local_bus1_static
   (we now allocate/copy these per-request into heap buffers) */

static int valve_positions_static[MAX_VALVE_COUNT];

extern sensor_value_t sensor_data[MAX_I2C_BUSES][CHANNELS_PER_BUS];

typedef struct
{
    sensor_value_t *snapshot; // linear array: MAX_I2C_BUSES * CHANNELS_PER_BUS
    bus_sensor_t *bus0;       // array CHANNELS_PER_BUS
    bus_sensor_t *bus1;       // array CHANNELS_PER_BUS
    void *data_block;         // base pointer for single contiguous allocation
} ws_send_work_t;

static void ws_send_worker(void *arg)
{
    ws_send_work_t *w = (ws_send_work_t *)arg;
    if (!w)
    {
        vTaskDelete(NULL);
        return;
    }

    // quick heap integrity check
    if (!heap_caps_check_integrity_all(true))
    {
        ESP_LOGE(TAG, "Heap corrupted before ws_send_worker");
        goto cleanup_and_exit;
    }

    char msg[256];

    // send valves
    size_t used = 0;
    used = snprintf(msg, sizeof(msg), "{ \"valves\":[");
    for (int i = 0; i < MAX_VALVE_COUNT && used < sizeof(msg) - 16; ++i)
    {
        int n = snprintf(msg + used, sizeof(msg) - used, "%d%s",
                         valve_positions_static[i],
                         (i < MAX_VALVE_COUNT - 1) ? "," : "");
        if (n < 0)
            break;
        used += (size_t)n;
    }
    if (used < sizeof(msg) - 4)
    {
        int n = snprintf(msg + used, sizeof(msg) - used, "] }");
        if (n > 0)
            used += (size_t)n;
    }
    else
    {
        msg[sizeof(msg) - 1] = '\0';
        used = strlen(msg);
    }
    ws_async_send(msg, used);

    // send each sensor (small independent messages)
    for (int bus = 0; bus < MAX_I2C_BUSES; ++bus)
    {
        for (int ch = 0; ch < CHANNELS_PER_BUS; ++ch)
        {
            sensor_value_t *val = &w->snapshot[bus * CHANNELS_PER_BUS + ch];
            bus_sensor_t *cfg = (bus == 0) ? &w->bus0[ch] : &w->bus1[ch];

            int n = snprintf(msg, sizeof(msg),
                             "{\"bus\":%d,\"ch\":%d,"
                             "\"type\":\"%s\",\"valve\":\"%s\",\"location\":\"%s\","
                             "\"rhs\":%d,\"co2s\":%d,"
                             "\"temp\":%.1f,\"rh\":%.1f,\"co2\":%.1f,\"ts\":%lu}",
                             bus, ch,
                             cfg->type, cfg->valve, cfg->location,
                             cfg->rh_enabled, cfg->co2_enabled,
                             val->temp, val->rh, val->co2, (unsigned long)val->timestamp);

            if (n <= 0)
                continue;
            size_t to_send = (size_t)n;
            if (to_send >= sizeof(msg))
            {
                msg[sizeof(msg) - 1] = '\0';
                to_send = strlen(msg);
            }
            ws_async_send(msg, to_send);
        }
    }

cleanup_and_exit:
    // free the single contiguous block (if present) and the work struct
    if (w->data_block)
        free(w->data_block);
    free(w);
    vTaskDelete(NULL);
}

void send_index_html_ws(void)
{
    // allocate work container
    ws_send_work_t *work = calloc(1, sizeof(ws_send_work_t));
    if (!work)
    {
        ESP_LOGE(TAG, "calloc work failed");
        return;
    }

    size_t snap_count = (size_t)MAX_I2C_BUSES * (size_t)CHANNELS_PER_BUS;
    size_t snapshot_bytes = sizeof(sensor_value_t) * snap_count;
    size_t bus_bytes = sizeof(bus_sensor_t) * (size_t)CHANNELS_PER_BUS;
    size_t total_bytes = snapshot_bytes + bus_bytes * 2;

    // quick heap integrity check before allocating
    if (!heap_caps_check_integrity_all(true))
    {
        ESP_LOGE(TAG, "Heap integrity check failed before allocating ws data");
        free(work);
        return;
    }

    // allocate one contiguous block to reduce fragmentation / avoid mismatched frees
    void *block = heap_caps_malloc(total_bytes, MALLOC_CAP_8BIT);
    if (!block)
    {
        ESP_LOGE(TAG, "heap_caps_malloc for ws data failed (%u bytes)", (unsigned)total_bytes);
        free(work);
        return;
    }

    work->data_block = block;
    work->snapshot = (sensor_value_t *)block;
    work->bus0 = (bus_sensor_t *)((char *)block + snapshot_bytes);
    work->bus1 = (bus_sensor_t *)((char *)block + snapshot_bytes + bus_bytes);

    // peek into snapshot buffer
    if (xQueuePeek(sensor_queue, work->snapshot, 0) != pdTRUE)
    {
        ESP_LOGE(TAG, "No sensor data available in queue");
        free(work->data_block);
        free(work);
        return;
    }

    // copy config under mutex briefly
    bool cfg_copied = false;
    if (sensor_cfg_mutex && xSemaphoreTake(sensor_cfg_mutex, pdMS_TO_TICKS(20)) == pdTRUE)
    {
        memcpy(work->bus0, bus0_sensor_settings, bus_bytes);
        memcpy(work->bus1, bus1_sensor_settings, bus_bytes);
        xSemaphoreGive(sensor_cfg_mutex);
        cfg_copied = true;
    }
    if (!cfg_copied)
    {
        ESP_LOGW(TAG, "sensor_cfg_mutex unavailable; using defaults");
        memset(work->bus0, 0, bus_bytes);
        memset(work->bus1, 0, bus_bytes);
    }

    BaseType_t r = xTaskCreate(ws_send_worker, "ws_send", 6144, work, tskIDLE_PRIORITY + 1, NULL);
    if (r != pdPASS)
    {
        ESP_LOGE(TAG, "xTaskCreate ws_send_worker failed");
        free(work->data_block);
        free(work);
        return;
    }

    // return immediately so httpd thread isn't blocked
    return;
}