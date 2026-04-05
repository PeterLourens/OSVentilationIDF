/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include "task_influxdb.h"

static const char *TAG = "task_influxdb";
static StackType_t influxdb_stack[8192];  // stack buffer
static StaticTask_t influxdb_tcb;         // TCB buffer
static size_t influxdb_stack_size = 8192; // 8192 words = 32 KB

void start_task_influxdb(void)
{
    // Create influxdb task
    // xTaskCreate(task_influxdb_code, "influxdb", 8192, NULL, 12, &task_influxdb);
    task_influxdb = xTaskCreateStatic(task_influxdb_code, "influxdb", influxdb_stack_size, NULL, 6, influxdb_stack, &influxdb_tcb);

    if (task_influxdb == NULL)
    {
        LOGE(TAG, "InfluxDB task creation failed");
    }
}

void task_influxdb_code(void *pvParameters)
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
}
