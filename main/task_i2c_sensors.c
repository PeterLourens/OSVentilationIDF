/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include "task_i2c_sensors.h"

static const char *TAG = "task_i2c_sensors";
static StackType_t i2c_sensors_stack[5120];  // stack buffer
static StaticTask_t i2c_sensors_tcb;         // TCB buffer
static size_t i2c_sensors_stack_size = 5120; // 5120 words = 20.480 KB

void start_task_i2c_sensors(void)
{
    task_i2c_sensors = xTaskCreateStatic(task_i2c_sensors_code, "i2c_sensors", i2c_sensors_stack_size, NULL, 6, i2c_sensors_stack, &i2c_sensors_tcb);
}

void task_i2c_sensors_code(void *pvParameters)
{
    esp_err_t err = ESP_OK;

    // Compute snapshot size (must match other modules)
    const size_t snap_count = (size_t)MAX_I2C_BUSES * (size_t)CHANNELS_PER_BUS * (size_t)DATA_FIELDS;
    const size_t snapshot_bytes = sizeof(float) * snap_count;

    // Record item size globally so other tasks can sanity-check before peeking
    sensor_avg_item_size = snapshot_bytes;

    ESP_LOGD(TAG, "task_i2c_sensors: snapshot_bytes=%zu, free_heap=%u", snapshot_bytes, esp_get_free_heap_size());

    // Static storage must match the snapshot layout exactly
    static uint8_t sensor_queue_storage[1 * sizeof(float[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS])];
    static StaticQueue_t sensor_queue_struct;

    static uint8_t sensor_avg_queue_storage[1 * sizeof(float[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS])];
    static StaticQueue_t sensor_avg_queue_struct;

    // Create static queues (1 slot each) to avoid large heap allocations
    sensor_queue = xQueueCreateStatic(1,snapshot_bytes,sensor_queue_storage,&sensor_queue_struct);

    if (sensor_queue == NULL)
    {
        ESP_LOGE(TAG, "xQueueCreateStatic(sensor_queue) failed (item size=%zu) free_heap=%u", snapshot_bytes, esp_get_free_heap_size());
        // Terminate task cleanly; consumers will wait for readiness flag
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGD(TAG, "created static sensor_queue, item size=%zu", snapshot_bytes);

    sensor_avg_queue = xQueueCreateStatic(1,snapshot_bytes,sensor_avg_queue_storage,&sensor_avg_queue_struct);

    if (sensor_avg_queue == NULL)
    {
        ESP_LOGE(TAG, "xQueueCreateStatic(sensor_avg_queue) failed (item size=%zu) free_heap=%u", snapshot_bytes, esp_get_free_heap_size());
        // Clean up previously created queue before exiting
        vQueueDelete(sensor_queue);
        sensor_queue = NULL;
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGD(TAG, "created static sensor_avg_queue, item size=%zu", snapshot_bytes);

    // Signal readiness for consumers
    sensors_queues_ready = true;
    ESP_LOGI(TAG, "sensor queues ready");

    // Initialize both TCA9548A multiplexers (log but continue even if one fails)
    err = i2c_init_tca9548(i2c_bus0_handle, &tca9548_bus0_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize TCA9548 on bus 0: %s", esp_err_to_name(err));
    }
    else
    {
        ESP_LOGI(TAG, "TCA9548 initialized on bus 0 successfully");
    }

    err = i2c_init_tca9548(i2c_bus1_handle, &tca9548_bus1_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize TCA9548 on bus 1: %s", esp_err_to_name(err));
    }
    else
    {
        ESP_LOGI(TAG, "TCA9548 initialized on bus 1 successfully");
    }

    // Main loop: read + average
    while (1)
    {
        // DO NOT reinitialize sensors here — only read them
        err = read_sensors_into_queue();
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to read sensors into queue: %s", esp_err_to_name(err));
        }

        sensor_data_average();

        // Read sensors every 5 seconds
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
