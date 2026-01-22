/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2025 Peter Lourens
*/

#include "task_i2c_sensors.h"

static const char *TAG = "task_i2c_sensors";

void start_task_i2c_sensors(void)
{
    xTaskCreate(task_i2c_sensors_code, "i2c_sensors", 4096, NULL, 10, &task_i2c_sensors);
}

void task_i2c_sensors_code(void *pvParameters)
{
    esp_err_t err = ESP_OK;

    // ------------------------------------------------------------
    // Create queues for raw and averaged sensor data
    // ------------------------------------------------------------
    size_t snap_count = MAX_I2C_BUSES * CHANNELS_PER_BUS * DATA_FIELDS;
    size_t snapshot_bytes = sizeof(float) * snap_count;

    sensor_queue = xQueueCreate(1, snapshot_bytes);
    sensor_avg_queue = xQueueCreate(1, snapshot_bytes);

    if (!sensor_queue)
    {
        ESP_LOGE(TAG, "failed to create sensor_queue (%u bytes)", snapshot_bytes);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGD(TAG, "created sensor_queue, item size=%u", snapshot_bytes);

    if (!sensor_avg_queue)
    {
        ESP_LOGE(TAG, "failed to create sensor_avg_queue (%u bytes)", snapshot_bytes);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGD(TAG, "created sensor_avg_queue, item size=%u", snapshot_bytes);

    // ------------------------------------------------------------
    // Initialize both TCA9548A multiplexers
    // ------------------------------------------------------------
    err = i2c_init_tca9548(i2c_bus0_handle, &tca9548_bus0_handle);
    if (err != ESP_OK)
        ESP_LOGE(TAG, "Failed to initialize TCA9548 on bus 0: %s", esp_err_to_name(err));
    else
        ESP_LOGI(TAG, "TCA9548 initialized on bus 0 successfully");

    err = i2c_init_tca9548(i2c_bus1_handle, &tca9548_bus1_handle);
    if (err != ESP_OK)
        ESP_LOGE(TAG, "Failed to initialize TCA9548 on bus 1: %s", esp_err_to_name(err));
    else
        ESP_LOGI(TAG, "TCA9548 initialized on bus 1 successfully");

    // ------------------------------------------------------------
    // Main loop: read + average
    // ------------------------------------------------------------
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