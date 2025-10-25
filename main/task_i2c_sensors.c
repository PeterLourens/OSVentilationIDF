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

    err = i2c_init_master0();
    if (err != ESP_OK)
    {
        ESP_LOGE(I2C_SENSOR_TASK_TAG, "Master bus 0 initialization failed: %s", esp_err_to_name(err));
    }

    // Init TCA9548a multiplexer
    err = i2c_init_tca9548();
    if (err != ESP_OK)
    {
        ESP_LOGE(I2C_SENSOR_TASK_TAG, "TCA9548A initialization failed: %s", esp_err_to_name(err));
    }

    uint8_t channel = 0x01;
    err = i2c_tca9548_set_channel(tca9548_bus0_handle, channel);
    if (err != ESP_OK)
    {
        ESP_LOGE(I2C_SENSOR_TASK_TAG, "Set channel failed: %s", esp_err_to_name(err));
    }

    while (1)
    {
        ESP_LOGI(I2C_SENSOR_TASK_TAG, "Task i2c sensors running");
        vTaskDelay(pdMS_TO_TICKS(10000)); // Delay after readings
    }
}

/*    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int day_of_week;*/