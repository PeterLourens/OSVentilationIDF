#include "task_i2c.h"

void start_task_i2c(void)
{
    xTaskCreate(task_i2c_code, "taski2c", 10000, NULL, 6, &task_i2c);
}

void task_i2c_code(void *pvParameters)
{
    float temperature = 0;
    float humidity = 0;
    uint16_t co2 = 0;
    esp_err_t err = ESP_OK;

    const char *TAG1 = "i2c init";

    /*
    Whats next:
    - Init sensors should have input the channel. This required when reading config file
    - Add second master bus
    - Add sensor to second master bus
    */

    // init I2C bus and sensors
    err = i2c_init_master0();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG1, "I2C master bus initialization failed: %s", esp_err_to_name(err));
    }

    // Init TCA9548a multiplexer
    err = i2c_init_multiplexer_tca9548a(0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG1, "TCA9548A initialization failed: %s", esp_err_to_name(err));
    }

    // Init AHT20 sensor
    err = i2c_init_sensor_aht20(0x01);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG1, "AHT20 initialization failed: %s", esp_err_to_name(err));
    }

    // Init AHT20 sensor
    err = i2c_init_sensor_aht20(0x03);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG1, "AHT20 initialization failed: %s", esp_err_to_name(err));
    }

    // Init SCD4x sensor
    err = i2c_init_sensor_scd4x(0x02);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG1, "SCD4x initialization failed: %s", esp_err_to_name(err));
    }

    while (1)
    {
        // Read AHT20 data
        err = tca9548_set_channels(&i2c_switch0, 0x01); // Select channel 0
        err = aht_get_data(&aht20, &temperature, &humidity);
        printf("AHT20 on TCA9548A Channel 0 → Temp: %.2f °C, Humidity: %.2f %%\n", temperature, humidity);

        vTaskDelay(pdMS_TO_TICKS(100)); // Delay between readings

        // Read SCD41 data
        err = tca9548_set_channels(&i2c_switch0, 0x02); // Select channel 1
        err = scd4x_read_measurement(&scd41, &co2, &temperature, &humidity);
        printf("SCD41 on TCA9548A Channel 1 → Temp: %.2f °C, Humidity: %.2f %%, CO2: %.2d ppm\n", temperature, humidity, co2);

        vTaskDelay(pdMS_TO_TICKS(100)); // Delay between readings

        // Read AHT20 data
        err = tca9548_set_channels(&i2c_switch0, 0x03); // Select channel 0
        err = aht_get_data(&aht20, &temperature, &humidity);
        printf("AHT20 on TCA9548A Channel 2 → Temp: %.2f °C, Humidity: %.2f %%\n", temperature, humidity);

        vTaskDelay(pdMS_TO_TICKS(5000)); // Delay after readings
    }
}