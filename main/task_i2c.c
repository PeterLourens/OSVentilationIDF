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

    // init I2C bus and sensors
    read_sensors();

    while (1)
    {
        // Read AHT20 data
        err = tca9548_set_channels(&i2c_switch, 0x01); // Select channel 0
        err = aht_get_data(&aht20, &temperature, &humidity);
        printf("AHT20 on TCA9548A Channel 0 → Temp: %.2f °C, Humidity: %.2f %%\n", temperature, humidity);

        vTaskDelay(pdMS_TO_TICKS(500)); // Delay between readings

        // Read SCD41 data
        err = tca9548_set_channels(&i2c_switch, 0x02); // Select channel 1
        err = scd4x_read_measurement(&scd41, &co2, &temperature, &humidity);
        printf("SCD41 on TCA9548A Channel 1 → Temp: %.2f °C, Humidity: %.2f %%, CO2: %.2d ppm\n", temperature, humidity, co2);

        vTaskDelay(pdMS_TO_TICKS(5000)); // Delay after readings
    }
}