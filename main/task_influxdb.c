#include "task_influxdb.h"

static const char *TAG = "Influxdb task";

void start_task_influxdb(void)
{
    // Create influxdb task
    xTaskCreate(task_influxdb_code, "influxdb", 15000, NULL, 6, &task_influxdb);
}

void task_influxdb_code(void *pvParameters)
{
    // Some delay a boot
    vTaskDelay(pdMS_TO_TICKS(10000));

    while (true)
    {
        ESP_LOGI(TAG, "Update InfluxDB...");
        influxdb_process(sensor_queue, sensor_avg_queue);
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}