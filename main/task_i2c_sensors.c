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
        for (uint8_t ch = 0x01; ch < 0x08; ch++)
        {
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

            ESP_LOGI(I2C_SENSOR_TASK_TAG, "Scanning I2C devices on channel %d...", ch);

            // Scan I2C addresses from 0x03 to 0x77
            for (uint8_t addr = 0x03; addr < 0x78; addr++)
            {
                // Try to transmit 0 bytes to each address
                err = i2c_master_write_to_device(I2C_NUM_0, addr, NULL, 0, pdMS_TO_TICKS(100));
                if (err == ESP_OK)
                {
                    ESP_LOGI(I2C_SENSOR_TASK_TAG, "Device found at address 0x%02X on channel %d", addr, ch);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(10000)); // Delay after readings
        }
    }
}

/*    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int day_of_week;*/