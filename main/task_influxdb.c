#include "task_influxdb.h"

static const char *TAG = "Influxdb";

void start_task_influxdb(void)
{
    // Create influxdb task
    xTaskCreate(task_influxdb_code, "influxdb", 4096, NULL, 6, &task_influxdb);
}

void task_influxdb_code(void *pvParameters)
{
}