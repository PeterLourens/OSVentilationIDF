#include "i2c_ds3231.h"

static const char *TAG = "DS3231";

esp_err_t i2c_ds3231_init(i2c_master_bus_handle_t bus_handle, i2c_master_dev_handle_t *dev_handle)
{
    i2c_device_config_t ds3231_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = DS3231_I2C_ADDR,
        .scl_speed_hz = 100000,
    };

    esp_err_t err = i2c_master_bus_add_device(bus_handle, &ds3231_cfg, dev_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to add DS3231 device: %s", esp_err_to_name(err));
        return err;
    }

    // Check if device responds
    uint8_t reg = 0x0F; // Control/Status register
    uint8_t status;

    err = i2c_master_transmit(*dev_handle, &reg, 1, pdMS_TO_TICKS(100));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to send register address: %s", esp_err_to_name(err));
        return err;
    }

    err = i2c_master_receive(*dev_handle, &status, 1, pdMS_TO_TICKS(100));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read status register: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "DS3231 initialized successfully");
    return ESP_OK;
}

esp_err_t i2c_ds3231_read_time(i2c_master_dev_handle_t dev_handle, struct tm *timeinfo)
{
    if (!dev_handle || !timeinfo)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t reg = 0x00; // Start from seconds register
    uint8_t data[7];    // Seconds through Year

    // Send register address
    esp_err_t err = i2c_master_transmit(dev_handle, &reg, 1, pdMS_TO_TICKS(100));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to send register address: %s", esp_err_to_name(err));
        return err;
    }

    // Read 7 bytes of time data
    err = i2c_master_receive(dev_handle, data, sizeof(data), pdMS_TO_TICKS(100));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read time data: %s", esp_err_to_name(err));
        return err;
    }

    // Convert BCD to decimal
    timeinfo->tm_sec = ((data[0] >> 4) * 10) + (data[0] & 0x0F);
    timeinfo->tm_min = ((data[1] >> 4) * 10) + (data[1] & 0x0F);
    timeinfo->tm_hour = ((data[2] >> 4) * 10) + (data[2] & 0x0F); // Assumes 24h mode
    timeinfo->tm_wday = (data[3] & 0x07) - 1;                     // Convert 1-7 to 0-6
    timeinfo->tm_mday = ((data[4] >> 4) * 10) + (data[4] & 0x0F);
    timeinfo->tm_mon = (((data[5] >> 4) * 10) + (data[5] & 0x0F)) - 1;          // Convert 1-12 to 0-11
    timeinfo->tm_year = ((data[6] >> 4) * 10) + (data[6] & 0x0F) + 2000 - 1900; // Convert to years since 1900

    ESP_LOGD(TAG, "Time read: %02d:%02d:%02d %02d/%02d/%04d",
             timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec,
             timeinfo->tm_mday, timeinfo->tm_mon + 1, timeinfo->tm_year + 1900);

    return ESP_OK;
}
