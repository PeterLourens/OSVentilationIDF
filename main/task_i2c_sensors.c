/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2025 Peter Lourens
*/

static const char *TAG = "i2c sensors task";

#include "task_i2c_sensors.h"

void start_task_i2c_sensors(void)
{
    xTaskCreate(task_i2c_sensors_code, "i2c_sensors", 10000, NULL, 10, &task_i2c_sensors);
}

void task_i2c_sensors_code(void *pvParameters)
{
    esp_err_t err = ESP_OK;

    // sensor_queue = xQueueCreate(1, sizeof(double[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS]));
    // sensor_queue = xQueueCreate(1, sizeof(float) * MAX_I2C_BUSES * CHANNELS_PER_BUS * DATA_FIELDS);

    // create sensor_queue at init time (place in startup where you currently create the queue)
    size_t snap_count = (size_t)MAX_I2C_BUSES * (size_t)CHANNELS_PER_BUS * (size_t)DATA_FIELDS;
    size_t snapshot_bytes = sizeof(double) * snap_count;
    sensor_queue = xQueueCreate(1, snapshot_bytes);
    sensor_avg_queue = xQueueCreate(1, snapshot_bytes);

    if (!sensor_queue)
    {
        ESP_LOGE(TAG, "failed to create sensor_queue (%u bytes)", sizeof(float) * MAX_I2C_BUSES * CHANNELS_PER_BUS * DATA_FIELDS);
        // task cannot continue without the queue — delete this task
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "created sensor_queue, item size=%u", sizeof(float) * MAX_I2C_BUSES * CHANNELS_PER_BUS * DATA_FIELDS);

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

    err = init_sensors_on_mux_channels();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize sensors on mux channels: %s", esp_err_to_name(err));
    }
    else
    {
        ESP_LOGI(TAG, "Sensors on mux channels initialized successfully");
    }

    while (1)
    {
        err = read_sensors_into_queue();
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to read sensors into queue: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(100)); // Delay after readings
        // ESP_LOGI(TAG, "Update average sensor queue");
        sensor_data_average();
        vTaskDelay(pdMS_TO_TICKS(5000)); // Delay after readings
    }
}
