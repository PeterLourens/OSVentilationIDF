#include "i2c_functions.h"

static const char *TAG = "i2c test";

esp_err_t i2c_init_master0(void)
{
    esp_err_t err = ESP_OK;

    // I2C master descriptor
    dev.port = I2C_MASTER_NUM;
    dev.cfg.sda_io_num = I2C_MASTER_SDA_IO;
    dev.cfg.scl_io_num = I2C_MASTER_SCL_IO;
    dev.cfg.sda_pullup_en = GPIO_PULLUP_ENABLE;
    dev.cfg.scl_pullup_en = GPIO_PULLUP_ENABLE;
    dev.cfg.master.clk_speed = I2C_MASTER_FREQ_HZ;

    err = i2cdev_init();
    return err;
}

esp_err_t i2c_init_multiplexer_tca9548a(void)
{
    esp_err_t err = ESP_OK;
    err = tca9548_init_desc(&i2c_switch, TCA9548_ADDR, I2C_MASTER_NUM, I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);
    err = tca9548_set_channels(&i2c_switch, 0x01); // Select channel 0
    return err;
}

esp_err_t i2c_init_sensor_aht20(int channel)
{
    esp_err_t err = ESP_OK;
    err = aht_init_desc(&aht20, AHT20_ADDR, I2C_MASTER_NUM, I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);

    // First change channel and then init sensor
    err = tca9548_set_channels(&i2c_switch, 0x01); // Select channel 0
    if (err != ESP_OK)
    {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    err = aht_init(&aht20);
    if (err != ESP_OK)
    {
        return err;
    }

    return err;
}

esp_err_t i2c_init_sensor_scd4x(int channel)
{
    esp_err_t err = ESP_OK;
    err = scd4x_init_desc(&scd41, I2C_MASTER_NUM, I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);

    err = tca9548_set_channels(&i2c_switch, 0x02); // Select channel 1
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to switch TCA9548A channel: %s", esp_err_to_name(err));
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGI(TAG, "Initializing SCD41 sensor...");
    err = scd4x_wake_up(&scd41);
    err = scd4x_stop_periodic_measurement(&scd41);
    err = scd4x_reinit(&scd41);
    ESP_LOGI(TAG, "Sensor initialized");
    err = scd4x_start_periodic_measurement(&scd41);
    ESP_LOGI(TAG, "Periodic measurements started");

    vTaskDelay(pdMS_TO_TICKS(5100)); // Wait for sensor to be ready

    return err;
}

esp_err_t i2c_init_sensor_bme680(int channel)
{
    esp_err_t err = ESP_OK;

    err = bme680_init_desc(&bme680, BME680_ADDR, I2C_MASTER_NUM, I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);

    err = tca9548_set_channels(&i2c_switch, 0x04); // Select channel 4
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to switch TCA9548A channel: %s", esp_err_to_name(err));
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    // Configure oversampling
    err = bme680_set_oversampling_rates(&bme680, BME680_OSR_8X, BME680_OSR_8X, BME680_OSR_8X);
    if (err != ESP_OK)
    {
        return err;
    }

    // Configure filter
    err = bme680_set_filter_size(&bme680, BME680_IIR_SIZE_3);
    if (err != ESP_OK)
    {
        return err;
    }

    // Configure heater
    err = bme680_set_heater_profile(&bme680, 0, 320, 150); // profile 0: 320°C, 150ms
    if (err != ESP_OK)
    {
        return err;
    }

    err = bme680_use_heater_profile(&bme680, 0);
    if (err != ESP_OK)
    {
        return err;
    }

    return err;
}

bme680_data_t i2c_sensor_read_bme680(int channel)
{
    bme680_data_t result;
    esp_err_t err;

    //uint32_t duration = bme680_get_measurement_duration(&bme680);

    // Trigger measurement
    err = bme680_force_measurement(&bme680);
    if (err != ESP_OK)
    {
        result.status = err;
        return result;
    }

    // Wait for measurement to complete
    vTaskDelay(pdMS_TO_TICKS(200));

    // Read results
    bme680_values_float_t values;
    err = bme680_get_results_float(&bme680, &values);
    if (err != ESP_OK)
    {
        result.status = err;
        return result;
    }

    result.temperature = values.temperature;
    result.humidity = values.humidity;
    result.pressure = values.pressure / 100.0; // Pa → hPa
    result.gas_resistance = values.gas_resistance;
    result.status = ESP_OK;

    return result;
}
