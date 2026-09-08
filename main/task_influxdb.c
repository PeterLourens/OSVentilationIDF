/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

/**
 * @file task_influxdb.c
 * @brief FreeRTOS task wrapper for InfluxDB telemetry publishing.
 */

#include <esp_attr.h>
#include "task_influxdb.h"

static const char *TAG = "task_influxdb";
EXT_RAM_BSS_ATTR static StackType_t influxdb_stack[8192]; // stack buffer
static StaticTask_t influxdb_tcb;                         // TCB buffer
static size_t influxdb_stack_size = 8192;                 // 8192 words = 32768 KB
static volatile bool influxdb_task_stop_requested = false;

static bool influxdb_task_enabled(void)
{
    bool enabled = false;

    if (influxdb_config_mutex && xSemaphoreTake(influxdb_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        enabled = strcmp(influxdb_config.enable_influxdb, "On") == 0;
        xSemaphoreGive(influxdb_config_mutex);
    }

    return enabled;
}

void stop_task_influxdb(void)
{
    influxdb_task_stop_requested = true;
    influxdb_stop_sender();

    if (task_influxdb == NULL)
    {
        LOGI(TAG, "InfluxDB task not running");
        return;
    }

    LOGI(TAG, "Stopping InfluxDB task");
    TickType_t start = xTaskGetTickCount();
    while (task_influxdb != NULL && (xTaskGetTickCount() - start) < pdMS_TO_TICKS(10000))
    {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void start_task_influxdb(void)
{
    if (!influxdb_task_enabled())
    {
        LOGI(TAG, "InfluxDB disabled by config; not starting task");
        if (task_influxdb != NULL)
        {
            stop_task_influxdb();
        }
        else
        {
            influxdb_stop_sender();
        }
        return;
    }

    if (task_influxdb != NULL)
    {
        LOGI(TAG, "InfluxDB task already running");
        return;
    }

    influxdb_task_stop_requested = false;
    // Create influxdb task
    task_influxdb = xTaskCreateStatic(task_influxdb_code, "influxdb", influxdb_stack_size, NULL, 6, influxdb_stack, &influxdb_tcb);

    if (task_influxdb == NULL)
    {
        LOGE(TAG, "InfluxDB task creation failed");
        return;
    }

    LOGI(TAG, "InfluxDB task started");
}

bool refresh_influxdb_task_state(void)
{
    if (influxdb_task_enabled())
    {
        start_task_influxdb();
        return true;
    }

    stop_task_influxdb();
    return false;
}

void task_influxdb_code(void *pvParameters)
{
    (void)pvParameters;

    // initial startup delay
    vTaskDelay(pdMS_TO_TICKS(5000));

    if (influxdb_task_stop_requested || !influxdb_task_enabled())
    {
        task_influxdb = NULL;
        vTaskDelete(NULL);
    }

    // Sender startup is retried from the main loop. Network availability can
    // change after boot, especially while Ethernet hands over to Wi-Fi.
    if (network_has_ip() && !influxdb_start_sender())
        LOGW(TAG, "Influx sender startup deferred until network becomes ready");

    // Use local copies of intervals so reads are atomic-ish
    uint32_t influx_interval_ms = g_influx_task_interval_ms;
    uint32_t monitor_interval_ms = g_dma_monitor_interval_ms;
    uint32_t tick_resolution_ms = g_monitor_tick_resolution_ms;

    // Track elapsed time (ms)
    uint32_t elapsed_influx_ms = 0;
    uint32_t elapsed_monitor_ms = 0;

    // Ensure resolution is sane
    if (tick_resolution_ms == 0)
        tick_resolution_ms = 1000;

    // Run once immediately (optional)
    LOGI(TAG, "Update InfluxDB (initial)...");
    influxdb_process(sensor_queue, sensor_avg_queue);
    // After initial publish, reset counters
    elapsed_influx_ms = 0;
    elapsed_monitor_ms = 0;

    for (;;)
    {
        if (influxdb_task_stop_requested || !influxdb_task_enabled())
        {
            LOGI(TAG, "InfluxDB disabled by config; task exiting");
            task_influxdb = NULL;
            vTaskDelete(NULL);
        }

        // Recover a sender that was not available at boot or became stopped
        // during an interface transition. This is deliberately non-blocking.
        if (network_has_ip() && influx_sender_task_handle == NULL)
        {
            if (influxdb_start_sender())
                LOGI(TAG, "InfluxDB sender started after network recovery");
        }

        // Sleep for the resolution slice
        TickType_t remaining = pdMS_TO_TICKS(tick_resolution_ms);
        while (remaining > 0 && !influxdb_task_stop_requested)
        {
            TickType_t slice = remaining > pdMS_TO_TICKS(100) ? pdMS_TO_TICKS(100) : remaining;
            vTaskDelay(slice);
            remaining -= slice;
        }

        // Update intervals in case they were changed at runtime
        influx_interval_ms = g_influx_task_interval_ms;
        monitor_interval_ms = g_dma_monitor_interval_ms;

        // Increment elapsed counters
        elapsed_influx_ms += tick_resolution_ms;
        elapsed_monitor_ms += tick_resolution_ms;

        // If monitor interval reached, enqueue DMA metric
        if (elapsed_monitor_ms >= monitor_interval_ms)
        {
            // Enqueue DMA metrics (non-blocking inside)
            if (!influxdb_enqueue_dma_metrics("osvent"))
            {
                LOGW(TAG, "influxdb: dma monitor enqueue failed");
            }
            // reset monitor counter but keep any overshoot
            elapsed_monitor_ms = elapsed_monitor_ms % monitor_interval_ms;
        }

        // If influx interval reached, run the full Influx process
        if (elapsed_influx_ms >= influx_interval_ms)
        {
            LOGI(TAG, "Update InfluxDB...");
            influxdb_process(sensor_queue, sensor_avg_queue);

            // Reset influx counter but keep overshoot
            elapsed_influx_ms = elapsed_influx_ms % influx_interval_ms;
        }
    }
}
