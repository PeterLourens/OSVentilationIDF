#include "i2c_functions.h"

static const char *TAG = "i2c test";

esp_err_t init_i2c_master0(void)
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

esp_err_t init_i2c_multiplexer_tca9548a(void)
{
    esp_err_t err = ESP_OK;
    err = tca9548_init_desc(&i2c_switch, TCA9548_ADDR, I2C_MASTER_NUM, I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);
    err = tca9548_set_channels(&i2c_switch, 0x01); // Select channel 0
    return err;
}

esp_err_t init_i2c_sensor_aht20(void)
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

esp_err_t init_i2c_sensor_scd4x(void)
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

// void read_sensors(void)
//{
//  esp_err_t err = ESP_OK;

// I2C master descriptor
// dev.port = I2C_MASTER_NUM;
// dev.cfg.sda_io_num = I2C_MASTER_SDA_IO;
// dev.cfg.scl_io_num = I2C_MASTER_SCL_IO;
// dev.cfg.sda_pullup_en = GPIO_PULLUP_ENABLE;
// dev.cfg.scl_pullup_en = GPIO_PULLUP_ENABLE;
// dev.cfg.master.clk_speed = I2C_MASTER_FREQ_HZ;

// AHT20 sensor descriptor
// aht20.mode = AHT_MODE_NORMAL;
// aht20.type = AHT_TYPE;

// Initialize the I2C master bus
// err = i2cdev_init();
// if (err != ESP_OK)
//{
//    ESP_LOGE(TAG, "I2C master bus initialization failed: %s", esp_err_to_name(err));
//    return;
//}

// Initialize the I2C multiplexer
/* err = tca9548_init_desc(&i2c_switch, TCA9548_ADDR, I2C_MASTER_NUM, I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);
if (err != ESP_OK)
{
    ESP_LOGE(TAG, "TCA9548A initialization failed: %s", esp_err_to_name(err));
    return;
} */

// Initialize AHT20
/* err = aht_init_desc(&aht20, AHT20_ADDR, I2C_MASTER_NUM, I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);
if (err != ESP_OK)
{
    ESP_LOGE(TAG, "AHT20 initialization failed: %s", esp_err_to_name(err));
    return;
} */

// Initialize SCD41
// err = scd4x_init_desc(&scd41, I2C_MASTER_NUM, I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);
// if (err != ESP_OK)
//{
//  ESP_LOGE(TAG, "SCD41 initialization failed: %s", esp_err_to_name(err));
// return;
//}

// Switch to channel 0 and initialize AHT20
/* err = tca9548_set_channels(&i2c_switch, 0x01); // Select channel 0
if (err != ESP_OK)
{
    ESP_LOGE(TAG, "Failed to switch TCA9548A channel: %s", esp_err_to_name(err));
    return;
}
vTaskDelay(pdMS_TO_TICKS(10));
err = aht_init(&aht20);
if (err != ESP_OK)
{
    ESP_LOGE(TAG, "AHT20 initialization failed: %s", esp_err_to_name(err));
    return;
} */

// Switch to channel 1 and initialize SCD41
/* err = tca9548_set_channels(&i2c_switch, 0x02); // Select channel 1
if (err != ESP_OK)
{
    ESP_LOGE(TAG, "Failed to switch TCA9548A channel: %s", esp_err_to_name(err));
    return;
}
vTaskDelay(pdMS_TO_TICKS(10));
ESP_LOGI(TAG, "Initializing SCD41 sensor...");
err = scd4x_wake_up(&scd41);
err = scd4x_stop_periodic_measurement(&scd41);
err = scd4x_reinit(&scd41);
ESP_LOGI(TAG, "Sensor initialized");
err = scd4x_start_periodic_measurement(&scd41);
ESP_LOGI(TAG, "Periodic measurements started");

vTaskDelay(pdMS_TO_TICKS(5100)); // Wait for sensor to be ready */

/*while (1)
{
    // Read AHT20 data
    err = tca9548_set_channels(&i2c_switch, 0x01); // Select channel 0
    err = aht_get_data(&aht20, &temperature, &humidity);
    printf("AHT20 on TCA9548A Channel 0 → Temp: %.2f °C, Humidity: %.2f %%\n", temperature, humidity);

    vTaskDelay(pdMS_TO_TICKS(500)); // Delay between readings

    // Read SCD41 data
    err = tca9548_set_channels(&i2c_switch, 0x02); // Select channel 1
    err = scd4x_read_measurement(&scd41, &co2, &temperature, &humidity);
    printf("SCD41 on TCA9548A Channel 1 → CO2: %.2d ppm, Temp: %.2f °C, Humidity: %.2f %%\n", co2, temperature, humidity);

    vTaskDelay(pdMS_TO_TICKS(5000)); // Delay after readings
}*/
//}
