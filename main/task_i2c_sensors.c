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

    err = init_sensors_on_mux_channels();
    if (err != ESP_OK)
    {
        ESP_LOGE(I2C_SENSOR_TASK_TAG, "Failed to initialize sensors on mux channels: %s", esp_err_to_name(err));
    }
    else
    {
        ESP_LOGI(I2C_SENSOR_TASK_TAG, "Sensors on mux channels initialized successfully");
    }

    // Init TCA9548a multiplexer on bus0
    /*err = i2c_init_tca9548(i2c_bus0_handle, &tca9548_bus0_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(I2C_SENSOR_TASK_TAG, "TCA9548A initialization failed: %s", esp_err_to_name(err));
    }

    uint8_t ch = 0x01;
    err = i2c_tca9548_set_channel(tca9548_bus0_handle, ch);
    if (err != ESP_OK)
    {
        ESP_LOGE(I2C_SENSOR_TASK_TAG, "Failed to set channel %d: %s", ch, esp_err_to_name(err));
    }
    else
    {
        ESP_LOGI(I2C_SENSOR_TASK_TAG, "TCA9548A channel set to %d", ch);
    }

    err = i2c_aht20_init(i2c_bus0_handle, &aht20_bus0_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(I2C_SENSOR_TASK_TAG, "AHT20 initialization failed: %s", esp_err_to_name(err));
    }

    ch = 0x02;
    err = i2c_tca9548_set_channel(tca9548_bus0_handle, ch);
    if (err != ESP_OK)
    {
        ESP_LOGE(I2C_SENSOR_TASK_TAG, "Failed to set channel %d: %s", ch, esp_err_to_name(err));
    }
    else
    {
        ESP_LOGI(I2C_SENSOR_TASK_TAG, "TCA9548A channel set to %d", ch);
    }
    err = i2c_init_scd41(i2c_bus0_handle, &scd41_bus0_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(I2C_SENSOR_TASK_TAG, "SCD41 initialization failed: %s", esp_err_to_name(err));
    }*/

    while (1)
    {
        err = read_sensors_on_mux_channels();
        if (err != ESP_OK)
        {
            ESP_LOGE(I2C_SENSOR_TASK_TAG, "Error reading sensors on mux channels: %s", esp_err_to_name(err));
        }
        else
        {
            ESP_LOGI(I2C_SENSOR_TASK_TAG, "Sensor readings updated");
        }
        vTaskDelay(pdMS_TO_TICKS(10000)); // Delay after readings

        /*float temperature = 0.0f;
        float humidity = 0.0f;

        uint8_t ch = 0x01;
        err = i2c_tca9548_set_channel(tca9548_bus0_handle, ch);
        err = aht20_read(aht20_bus0_handle, &temperature, &humidity);
        if (err != ESP_OK)
        {
            ESP_LOGE(I2C_SENSOR_TASK_TAG, "AHT20 read failed: %s", esp_err_to_name(err));
        }
        else
        {
            ESP_LOGI(I2C_SENSOR_TASK_TAG, "AHT20 Temperature: %.2f °C, Humidity: %.2f %%", temperature, humidity);
        }

        vTaskDelay(pdMS_TO_TICKS(50));

        // Read SCD41 on channael 1 (0x02)
        uint16_t co2;
        float temp, rh;

        bool ready = false;
        ch = 0x02;

        err = i2c_tca9548_set_channel(tca9548_bus0_handle, ch);
        if (scd41_data_ready(scd41_bus0_handle, &ready) == ready)
        {
            // ESP_LOGI(I2C_SENSOR_TASK_TAG, "SCD41 data ready");

            if (scd41_read(scd41_bus0_handle, &co2, &temp, &rh) == ESP_OK)
            {
                ESP_LOGI(I2C_SENSOR_TASK_TAG, "SCD41 Temperature: %.2f °C, Humidity: %.2f %%, CO₂: %u ppm", temp, rh, co2);
            }
            else
            {
                ESP_LOGW(I2C_SENSOR_TASK_TAG, "SCD41", "Read failed or CRC error");
            }
        }

        else
        {
            ESP_LOGI(I2C_SENSOR_TASK_TAG, "SCD41 data not ready");
        }*/

        // vTaskDelay(pdMS_TO_TICKS(10000)); // Delay after readings
    }
}

/*    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int day_of_week;*/