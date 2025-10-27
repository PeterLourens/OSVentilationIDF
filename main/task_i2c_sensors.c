/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2025 Peter Lourens
*/

#include "task_i2c_sensors.h"

void start_task_i2c_sensors(void)
{
    xTaskCreate(task_i2c_sensors_code, "i2c_sensors", 10000, NULL, 6, &task_i2c_sensors);
}

void task_i2c_sensors_code(void *pvParameters)
{
    const char *I2C_SENSOR_TASK_TAG = "i2c sensors";
    esp_err_t err = ESP_OK;

    sensor_queue = xQueueCreate(1, sizeof(double[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS]));
    if (sensor_queue == NULL)
    {
        ESP_LOGE("QUEUE", "Failed to create sensor queue!");
        abort(); // or handle error
    }
    else
    {
        ESP_LOGI("QUEUE", "Sensor queue created successfully");
    }

    err = init_sensors_on_mux_channels();
    if (err != ESP_OK)
    {
        ESP_LOGE(I2C_SENSOR_TASK_TAG, "Failed to initialize sensors on mux channels: %s", esp_err_to_name(err));
    }
    else
    {
        ESP_LOGI(I2C_SENSOR_TASK_TAG, "Sensors on mux channels initialized successfully");
    }

    while (1)
    {
        err = read_sensors_into_queue();
        if (err != ESP_OK)
        {
            ESP_LOGE(I2C_SENSOR_TASK_TAG, "Failed to read sensors into queue: %s", esp_err_to_name(err));
        }
        else
        {
            ESP_LOGI(I2C_SENSOR_TASK_TAG, "Sensor readings updated in queue");
        }

        vTaskDelay(pdMS_TO_TICKS(1000)); // Delay after readings
    }
}

/*    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int day_of_week;*/