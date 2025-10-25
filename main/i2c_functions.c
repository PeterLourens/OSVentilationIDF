#include "i2c_functions.h"

static const char *TAG = "i2c functions";

esp_err_t i2c_init_master0(void)
{
    esp_err_t err = ESP_OK;

    i2c_master_bus_config_t i2c_bus0_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = 0,
        .sda_io_num = I2C_MASTER_BUS0_SDA_IO,
        .scl_io_num = I2C_MASTER_BUS0_SCL_IO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    err = i2c_new_master_bus(&i2c_bus0_cfg, &i2c_bus0_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize I2C master bus 0: %s", esp_err_to_name(err));
    }

    return err;
}

esp_err_t i2c_init_master1(void)
{
    esp_err_t err = ESP_OK;

    i2c_master_bus_config_t i2c_bus1_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = 1,
        .sda_io_num = I2C_MASTER_BUS1_SDA_IO,
        .scl_io_num = I2C_MASTER_BUS1_SCL_IO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    err = i2c_new_master_bus(&i2c_bus1_cfg, &i2c_bus1_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize I2C master bus 0: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t i2c_init_tca9548(i2c_master_bus_handle_t bus_handle, i2c_master_dev_handle_t *dev_handle)
{
    esp_err_t err = ESP_OK;

    i2c_device_config_t tca9548_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TCA9548_I2C_ADDR,
        .scl_speed_hz = 100000,
    };

    err = i2c_master_bus_add_device(bus_handle, &tca9548_cfg, dev_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE("TCA9548A", "Failed to add device: %s", esp_err_to_name(err));
        return err;
    }
    return err;
}

esp_err_t i2c_tca9548_set_channel(i2c_master_dev_handle_t dev_handle, uint8_t channel)
{
    esp_err_t err = ESP_OK;

    if (!dev_handle || channel > 7 || dev_handle == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    err = i2c_master_transmit(dev_handle, &channel, 1, -1);
    if (err != ESP_OK)
    {
        ESP_LOGE("TCA9548A", "Failed to select channel: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t i2c_aht20_init(i2c_master_bus_handle_t bus_handle, i2c_master_dev_handle_t *dev_handle)
{
    esp_err_t err = ESP_OK;
    i2c_device_config_t aht20_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AHT20_I2C_ADDR,
        .scl_speed_hz = 100000,
    };
    err = i2c_master_bus_add_device(i2c_bus0_handle, &aht20_cfg, dev_handle);
    return err;
}

esp_err_t aht20_read(i2c_master_dev_handle_t dev_handle, float *temperature, float *humidity)
{
    esp_err_t err = ESP_OK;

    if (aht20_bus0_handle == NULL)
        return ESP_ERR_INVALID_STATE;

    uint8_t cmd[] = {0xAC, 0x33, 0x00};
    err = i2c_master_transmit(dev_handle, cmd, sizeof(cmd), -1);

    vTaskDelay(pdMS_TO_TICKS(80)); // Wait for measurement

    uint8_t data[7];
    err = i2c_master_receive(aht20_bus0_handle, data, sizeof(data), -1);

    uint32_t raw_humidity = ((uint32_t)(data[1] << 12 | data[2] << 4 | (data[3] >> 4)));
    uint32_t raw_temperature = ((uint32_t)((data[3] & 0x0F) << 16 | data[4] << 8 | data[5]));

    *humidity = ((float)raw_humidity * 100) / 1048576.0;
    *temperature = ((float)raw_temperature * 200 / 1048576.0) - 50;

    return err;
}

esp_err_t i2c_init_scd41(i2c_master_bus_handle_t bus_handle, i2c_master_dev_handle_t *dev_handle)
{
    i2c_device_config_t scd41_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SCD41_I2C_ADDR,
        .scl_speed_hz = 100000,
    };

    esp_err_t err = i2c_master_bus_add_device(bus_handle, &scd41_cfg, dev_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE("SCD41", "Failed to add device: %s", esp_err_to_name(err));
        return err;
    }

    // First give a soft reset to ensure the sensor can be initialized properly
    // uint8_t reset_cmd[2] = {0x36, 0x32};
    // i2c_master_transmit(*dev_handle, reset_cmd, 2, pdMS_TO_TICKS(100));
    // vTaskDelay(pdMS_TO_TICKS(10));

    // Stop any ongoing measurement (command 0x3F86)
    uint8_t stop_cmd[2] = {0x3F, 0x86};
    i2c_master_transmit(*dev_handle, stop_cmd, 2, pdMS_TO_TICKS(100));
    vTaskDelay(pdMS_TO_TICKS(500));

    // Start periodic measurement(command 0x21B1)
    uint8_t cmd[2] = {0x21, 0xB1};
    err = i2c_master_transmit(*dev_handle, cmd, sizeof(cmd), pdMS_TO_TICKS(100));
    if (err != ESP_OK)
    {
        ESP_LOGE("SCD41", "Failed to start periodic measurement: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI("SCD41", "SCD41 initialized and continuous measurement started");
    return ESP_OK;
}

uint8_t scd41_crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++)
        {
            if (crc & 0x80)
                crc = (crc << 1) ^ 0x31;
            else
                crc <<= 1;
        }
    }
    return crc;
}

esp_err_t scd41_read(i2c_master_dev_handle_t dev_handle, uint16_t *co2_ppm, float *temperature, float *humidity)
{
    esp_err_t err;
    uint8_t cmd[2] = {0xEC, 0x05}; // Read measurement command

    // Send read command
    err = i2c_master_transmit(dev_handle, cmd, sizeof(cmd), pdMS_TO_TICKS(100));
    if (err != ESP_OK)
    {
        ESP_LOGE("SCD41", "Failed to send read command: %s", esp_err_to_name(err));
        return err;
    }

    // Read 9 bytes: CO₂ (2+CRC), Temp (2+CRC), RH (2+CRC)
    uint8_t raw[9];
    err = i2c_master_receive(dev_handle, raw, sizeof(raw), pdMS_TO_TICKS(100));
    if (err != ESP_OK)
    {
        ESP_LOGE("SCD41", "Failed to read measurement: %s", esp_err_to_name(err));
        return err;
    }

    // Parse CO₂
    if (scd41_crc8(raw, 2) != raw[2])
        return ESP_ERR_INVALID_CRC;
    *co2_ppm = (raw[0] << 8) | raw[1];

    // Parse Temperature
    if (scd41_crc8(&raw[3], 2) != raw[5])
        return ESP_ERR_INVALID_CRC;
    uint16_t t_raw = (raw[3] << 8) | raw[4];
    *temperature = -45.0f + 175.0f * ((float)t_raw / 65535.0f);

    // Parse Humidity
    if (scd41_crc8(&raw[6], 2) != raw[8])
        return ESP_ERR_INVALID_CRC;
    uint16_t rh_raw = (raw[6] << 8) | raw[7];
    *humidity = 100.0f * ((float)rh_raw / 65535.0f);

    return ESP_OK;
}