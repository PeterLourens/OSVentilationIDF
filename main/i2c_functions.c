#include "i2c_functions.h"

static const char *TAG = "i2c functions";

esp_err_t i2c_init_master0(void)
{
    esp_err_t err = ESP_OK;

    // I2C master descriptor
    master0.port = I2C_NUM_0;
    master0.cfg.sda_io_num = I2C_MASTER_BUS0_SDA_IO;
    master0.cfg.scl_io_num = I2C_MASTER_BUS0_SCL_IO;
    master0.cfg.sda_pullup_en = GPIO_PULLUP_ENABLE;
    master0.cfg.scl_pullup_en = GPIO_PULLUP_ENABLE;
    master0.cfg.master.clk_speed = I2C_MASTER_FREQ_HZ;

    err = i2cdev_init();
    return err;
}

esp_err_t i2c_init_master1(void)
{
    esp_err_t err = ESP_OK;

    // I2C master descriptor
    master1.port = I2C_NUM_1;
    master1.cfg.sda_io_num = I2C_MASTER_BUS1_SDA_IO;
    master1.cfg.scl_io_num = I2C_MASTER_BUS1_SCL_IO;
    master1.cfg.sda_pullup_en = GPIO_PULLUP_ENABLE;
    master1.cfg.scl_pullup_en = GPIO_PULLUP_ENABLE;
    master1.cfg.master.clk_speed = I2C_MASTER_FREQ_HZ;

    err = i2cdev_init();
    return err;
}

esp_err_t i2c_init_multiplexer_tca9548a(uint8_t i2c_master_bus_number)
{
    esp_err_t err = ESP_OK;

    switch (i2c_master_bus_number)
    {
    case 0:
        err = tca9548_init_desc(&i2c_switch0, TCA9548_I2C_ADDR, I2C_NUM_0, I2C_MASTER_BUS0_SDA_IO, I2C_MASTER_BUS0_SCL_IO);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to init_desc multiplexer on bus0: %s", esp_err_to_name(err));
            return err;
        }
        err = tca9548_set_channels(&i2c_switch0, 0x01); // Select channel 0
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to switch TCA9548A channel on bus0: %s", esp_err_to_name(err));
            return err;
        }
        break;
    case 1:
        err = tca9548_init_desc(&i2c_switch1, TCA9548_I2C_ADDR, I2C_NUM_1, I2C_MASTER_BUS1_SDA_IO, I2C_MASTER_BUS1_SCL_IO);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to init_desc multiplexer on bus1: %s", esp_err_to_name(err));
            return err;
        }
        err = tca9548_set_channels(&i2c_switch1, 0x01); // Select channel 0
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to switch TCA9548A channel on bus1: %s", esp_err_to_name(err));
            return err;
        }
        break;
    default:
        return ESP_ERR_INVALID_ARG;
    }
    return err;
}

esp_err_t i2c_init_sensor_aht20(uint8_t i2c_master_bus_number, int channel)
{
    esp_err_t err = ESP_OK;

    switch (i2c_master_bus_number)
    {
    case 0:
        err = aht_init_desc(&aht20_bus0, AHT20_I2C_ADDR, I2C_NUM_0, I2C_MASTER_BUS0_SDA_IO, I2C_MASTER_BUS0_SCL_IO);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to init_desc AHT/DHT20 sensor on bus0: %s", esp_err_to_name(err));
            return err;
        }
        err = tca9548_set_channels(&i2c_switch0, channel); // Select channel 0
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to switch TCA9548A channel on bus0: %s", esp_err_to_name(err));
            return err;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
        err = aht_init(&aht20_bus0);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to init AHT/DHT20 sensor on bus0: %s", esp_err_to_name(err));
            return err;
        }
        break;
    case 1:
        err = aht_init_desc(&aht20_bus1, AHT20_I2C_ADDR, I2C_NUM_1, I2C_MASTER_BUS1_SDA_IO, I2C_MASTER_BUS1_SCL_IO);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to init_desc AHT/DHT20 sensor on bus1: %s", esp_err_to_name(err));
            return err;
        }
        err = tca9548_set_channels(&i2c_switch1, channel); // Select channel 0
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to switch TCA9548A channel on bus1: %s", esp_err_to_name(err));
            return err;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
        err = aht_init(&aht20_bus1);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to init AHT/DHT20 sensor on bus1: %s", esp_err_to_name(err));
            return err;
        }
        break;
    default:
        return ESP_ERR_INVALID_ARG;
    }
    return err;
}

esp_err_t i2c_init_sensor_scd4x(uint8_t i2c_master_bus_number, int channel)
{
    esp_err_t err = ESP_OK;

    switch (i2c_master_bus_number)
    {
    case 0:
        err = scd4x_init_desc(&scd41_bus0, I2C_NUM_0, I2C_MASTER_BUS0_SDA_IO, I2C_MASTER_BUS0_SCL_IO);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to init_desc SCD4x sensor on bus0: %s", esp_err_to_name(err));
            return err;
        }
        err = tca9548_set_channels(&i2c_switch0, channel); // Always init on channel 0
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to switch TCA9548A channel on bus0: %s", esp_err_to_name(err));
            return err;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
        // ESP_LOGI(TAG, "Initializing SCD41 sensor...");
        err = scd4x_wake_up(&scd41_bus0);
        err = scd4x_stop_periodic_measurement(&scd41_bus0);
        err = scd4x_reinit(&scd41_bus0);
        // ESP_LOGI(TAG, "Sensor initialized");
        err = scd4x_start_periodic_measurement(&scd41_bus0);
        // ESP_LOGI(TAG, "Periodic measurements started");
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to init SCD4x sensor on bus0: %s", esp_err_to_name(err));
            return err;
        }
        break;
    case 1:
        err = scd4x_init_desc(&scd41_bus1, I2C_NUM_1, I2C_MASTER_BUS1_SDA_IO, I2C_MASTER_BUS1_SCL_IO);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to init SCD41 sensor on bus1: %s", esp_err_to_name(err));
            return err;
        }
        err = tca9548_set_channels(&i2c_switch1, 0x01); // Select channel 0
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to switch TCA9548A channel on bus1: %s", esp_err_to_name(err));
            return err;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
        // ESP_LOGI(TAG, "Initializing SCD41 sensor...");
        err = scd4x_wake_up(&scd41_bus1);
        err = scd4x_stop_periodic_measurement(&scd41_bus1);
        err = scd4x_reinit(&scd41_bus1);
        // ESP_LOGI(TAG, "Sensor initialized");
        err = scd4x_start_periodic_measurement(&scd41_bus1);
        // ESP_LOGI(TAG, "Periodic measurements started");
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to init SCD4x sensor on bus1: %s", esp_err_to_name(err));
            return err;
        }
        break;
    default:
        return ESP_ERR_INVALID_ARG;
    }
    return err;
}

esp_err_t i2c_init_rtc()
{
    esp_err_t err = ESP_OK;

    err = ds3231_init_desc(&ds3231_rtc_bus1, I2C_NUM_1, I2C_MASTER_BUS1_SDA_IO, I2C_MASTER_BUS1_SCL_IO);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to init_desc DS3231 RTC on bus1: %s", esp_err_to_name(err));
        return err;
    }

    return err;
}

esp_err_t i2c_rtc_set_esp()
{
    esp_err_t err = ESP_OK;

    // Set DS3231 time to ESP32 time
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    err = ds3231_set_time(&ds3231_rtc_bus1, &timeinfo);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set DS3231 RTC time on bus1: %s", esp_err_to_name(err));
        return err;
    }

    return err;
}