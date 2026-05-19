/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include "task_influxdb.h"

static const char *TAG = "task_influxdb";
static StackType_t influxdb_stack[8192];  // stack buffer
static StaticTask_t influxdb_tcb;         // TCB buffer
static size_t influxdb_stack_size = 8192; // 8192 words = 32768 KB

void start_task_influxdb(void)
{
    // Create influxdb task
    task_influxdb = xTaskCreateStatic(task_influxdb_code, "influxdb", influxdb_stack_size, NULL, 6, influxdb_stack, &influxdb_tcb);

    if (task_influxdb == NULL)
    {
        LOGE(TAG, "InfluxDB task creation failed");
    }
}

/*void task_influxdb_code(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(5000));

    while (!network_has_ip())
    {
        LOGW(TAG, "InfluxDB: Waiting for network...");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    LOGI(TAG, "InfluxDB: Network ready");

    if (!influxdb_wait_ready(pdMS_TO_TICKS(2000)))
    {
        LOGW(TAG, "Influx sender not ready after wait; enqueues will be skipped until ready");
    }
    else
    {
        LOGI(TAG, "Influx sender ready (queue=%p)", (void *)influx_queue);
    }

    while (true)
    {
        LOGI(TAG, "Update InfluxDB...");
        influxdb_process(sensor_queue, sensor_avg_queue);
        vTaskDelay(pdMS_TO_TICKS(20000));
    }
}*/
void task_influxdb_code(void *pvParameters)
{
    (void)pvParameters;

    // initial startup delay
    vTaskDelay(pdMS_TO_TICKS(5000));

    while (!network_has_ip())
    {
        LOGW(TAG, "InfluxDB: Waiting for network...");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    LOGI(TAG, "InfluxDB: Network ready");

    if (!influxdb_wait_ready(pdMS_TO_TICKS(2000)))
    {
        LOGW(TAG, "Influx sender not ready after wait; enqueues will be skipped until ready");
    }
    else
    {
        LOGI(TAG, "Influx sender ready (queue=%p)", (void *)influx_queue);
    }

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
        // Sleep for the resolution slice
        vTaskDelay(pdMS_TO_TICKS(tick_resolution_ms));

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
