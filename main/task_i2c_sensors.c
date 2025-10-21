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
    const char *TAG1 = "i2c sensors";

    float temperature = 0;
    float humidity = 0;
    uint16_t co2 = 0;
    esp_err_t err = ESP_OK;

    // Init TCA9548a multiplexer
    err = i2c_init_multiplexer_tca9548a(0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG1, "TCA9548A initialization failed: %s", esp_err_to_name(err));
    }

    // Init AHT20 sensor
    err = i2c_init_sensor_aht20(0, 0x01);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG1, "AHT20 initialization failed: %s", esp_err_to_name(err));
    }

    // Init AHT20 sensor
    err = i2c_init_sensor_aht20(0, 0x03);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG1, "AHT20 initialization failed: %s", esp_err_to_name(err));
    }

    // Init SCD4x sensor
    err = i2c_init_sensor_scd4x(0, 0x02);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG1, "SCD4x initialization failed: %s", esp_err_to_name(err));
    }

    while (1)
    {
        // Read AHT20 data on bus0 channel 0
        err = tca9548_set_channels(&i2c_switch0, 0x01); // Select channel 0
        err = aht_get_data(&aht20_bus0, &temperature, &humidity);
        printf("AHT20 on TCA9548A Channel 0 → Temp: %.2f °C, Humidity: %.2f %%\n", temperature, humidity);

        vTaskDelay(pdMS_TO_TICKS(100)); // Delay between readings

        // Read SCD41 data on bus0 channel 1
        err = tca9548_set_channels(&i2c_switch0, 0x02); // Select channel 1
        err = scd4x_read_measurement(&scd41_bus0, &co2, &temperature, &humidity);
        printf("SCD41 on TCA9548A Channel 1 → Temp: %.2f °C, Humidity: %.2f %%, CO2: %.2d ppm\n", temperature, humidity, co2);

        vTaskDelay(pdMS_TO_TICKS(100)); // Delay between readings

        // Read AHT20 data on bus0 channel 2
        err = tca9548_set_channels(&i2c_switch0, 0x03); // Select channel 2
        err = aht_get_data(&aht20_bus0, &temperature, &humidity);
        printf("AHT20 on TCA9548A Channel 2 → Temp: %.2f °C, Humidity: %.2f %%\n", temperature, humidity);

        vTaskDelay(pdMS_TO_TICKS(5000)); // Delay after readings
    }
}