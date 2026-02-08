/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include "task_influxdb.h"

static const char *TAG = "task_influxdb";
static StackType_t influxdb_stack[8192];  // stack buffer
static StaticTask_t influxdb_tcb;         // TCB buffer
static size_t influxdb_stack_size = 8192; // 4096 words = 16 KB

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
    // Some delay a boot
    vTaskDelay(pdMS_TO_TICKS(10000));

    while (true)
    {
        LOGI(TAG, "Update InfluxDB...");
        influxdb_process(sensor_queue, sensor_avg_queue);
        vTaskDelay(pdMS_TO_TICKS(20000));
    }
}