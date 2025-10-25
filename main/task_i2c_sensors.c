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

    // Init TCA9548a multiplexer
    err = i2c_init_tca9548();
    if (err != ESP_OK)
    {
        ESP_LOGE(I2C_SENSOR_TASK_TAG, "TCA9548A initialization failed: %s", esp_err_to_name(err));
    }

    while (1)
    {
        // uint8_t channel = 0x01;
        /*err = i2c_tca9548_set_channel(tca9548_bus0_handle, channel);
        if (err != ESP_OK)
        {
            ESP_LOGE(I2C_SENSOR_TASK_TAG, "Set channel failed: %s", esp_err_to_name(err));
        }
        else
        {
            ESP_LOGI(I2C_SENSOR_TASK_TAG, "TCA9548A channel set to %d", channel);
        }*/
        // Scan all 8 channels of the TCA9548A
        // for (uint8_t ch = 0x01; ch < 0x08; ch++)
        //{
        uint8_t ch = 0x01;
        err = i2c_tca9548_set_channel(tca9548_bus0_handle, ch);
        if (err != ESP_OK)
        {
            ESP_LOGE(I2C_SENSOR_TASK_TAG, "Failed to set channel %d: %s", ch, esp_err_to_name(err));
            continue;
        }
        else
        {
            ESP_LOGI(I2C_SENSOR_TASK_TAG, "TCA9548A channel set to %d", ch);
        }

        err = i2c_aht20_init();
        if (err != ESP_OK)
        {
            ESP_LOGE(I2C_SENSOR_TASK_TAG, "AHT20 initialization failed: %s", esp_err_to_name(err));
        }

        float temperature = 0.0f;
        float humidity = 0.0f;
        err = aht20_read(&temperature, &humidity);
        if (err != ESP_OK)
        {
            ESP_LOGE(I2C_SENSOR_TASK_TAG, "AHT20 read failed: %s", esp_err_to_name(err));
        }
        else
        {
            ESP_LOGI(I2C_SENSOR_TASK_TAG, "\nAHT20 Temperature: %.2f °C, Humidity: %.2f %%", temperature, humidity);
        }

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