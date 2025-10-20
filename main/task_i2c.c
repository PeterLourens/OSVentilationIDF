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
    err = i2c_init_multiplexer_tca9548a();
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

    // Init BME680 sensor
    /*err = i2c_init_sensor_bme680(0x04);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG1, "BME680 initialization failed: %s", esp_err_to_name(err));
    }*/
    memset(&bme680, 0, sizeof(bme680_t));

    bme680_init_desc(&bme680, BME680_ADDR, I2C_MASTER_NUM, I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);

    // init the sensor
    bme680_init_sensor(&bme680);

    // Changes the oversampling rates to 4x oversampling for temperature
    // and 2x oversampling for humidity. Pressure measurement is skipped.
    bme680_set_oversampling_rates(&bme680, BME680_OSR_4X, BME680_OSR_NONE, BME680_OSR_2X);

    // Change the IIR filter size for temperature and pressure to 7.
    bme680_set_filter_size(&bme680, BME680_IIR_SIZE_7);

    // Change the heater profile 0 to 200 degree Celsius for 100 ms.
    bme680_set_heater_profile(&bme680, 0, 200, 100);
    bme680_use_heater_profile(&bme680, 0);

    // Set ambient temperature to 10 degree Celsius
    bme680_set_ambient_temperature(&bme680, 10);

    // as long as sensor configuration isn't changed, duration is constant
    uint32_t duration;
    bme680_get_measurement_duration(&bme680, &duration);

    TickType_t last_wakeup = xTaskGetTickCount();
    bme680_values_float_t values;

    while (1)
    {
        // Read AHT20 data
        err = tca9548_set_channels(&i2c_switch, 0x01); // Select channel 0
        err = aht_get_data(&aht20, &temperature, &humidity);
        printf("AHT20 on TCA9548A Channel 0 → Temp: %.2f °C, Humidity: %.2f %%\n", temperature, humidity);

        vTaskDelay(pdMS_TO_TICKS(100)); // Delay between readings

        // Read SCD41 data
        err = tca9548_set_channels(&i2c_switch, 0x02); // Select channel 1
        err = scd4x_read_measurement(&scd41, &co2, &temperature, &humidity);
        printf("SCD41 on TCA9548A Channel 1 → Temp: %.2f °C, Humidity: %.2f %%, CO2: %.2d ppm\n", temperature, humidity, co2);

        vTaskDelay(pdMS_TO_TICKS(100)); // Delay between readings

        // Read AHT20 data
        err = tca9548_set_channels(&i2c_switch, 0x03); // Select channel 0
        err = aht_get_data(&aht20, &temperature, &humidity);
        printf("AHT20 on TCA9548A Channel 2 → Temp: %.2f °C, Humidity: %.2f %%\n", temperature, humidity);

        vTaskDelay(pdMS_TO_TICKS(100)); // Delay between readings

        // Read BME680 data
        err = tca9548_set_channels(&i2c_switch, 0x04); // Select channel 0
        /*bme680_data_t data = i2c_sensor_read_bme680(4);

        if (data.status == ESP_OK)
        {
            ESP_LOGI(TAG1, "BME680 on TCA9548A Channel 3 →Temp: %.2f°C, Humidity: %.2f%%, Pressure: %.2f hPa, Gas: %.2fΩ", data.temperature, data.humidity, data.pressure, data.gas_resistance);
        }
        else
        {
            ESP_LOGE(TAG1, "Failed to read BME680 on channel 4");
        }*/
        // trigger the sensor to start one TPHG measurement cycle
        if (bme680_force_measurement(&bme680) == ESP_OK)
        {
            // passive waiting until measurement results are available
            vTaskDelay(duration);

            // get the results and do something with them
            if (bme680_get_results_float(&bme680, &values) == ESP_OK)
                printf("BME680 Sensor: %.2f °C, %.2f %%, %.2f hPa, %.2f Ohm\n",
                       values.temperature, values.humidity, values.pressure, values.gas_resistance);
        }
        // passive waiting until 1 second is over
        vTaskDelayUntil(&last_wakeup, pdMS_TO_TICKS(1000));

        vTaskDelay(pdMS_TO_TICKS(5000)); // Delay after readings
    }
}