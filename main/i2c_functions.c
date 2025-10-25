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

esp_err_t i2c_init_tca9548(void)
{
    esp_err_t err = ESP_OK;

    i2c_device_config_t tca9548_cfg = {
        .device_address = TCA9548_I2C_ADDR,
        .scl_speed_hz = 100000,
    };

    err = i2c_master_bus_add_device(i2c_bus0_handle, &tca9548_cfg, &tca9548_bus0_handle);
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

    // Send channel mask to select active channel
    uint8_t data = channel;
    err = i2c_master_transmit(dev_handle, &data, 1, -1);
    if (err != ESP_OK)
    {
        ESP_LOGE("TCA9548A", "Failed to select channel: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t i2c_aht20_init(void)
{
    esp_err_t err = ESP_OK;
    i2c_device_config_t aht20_cfg = {
        .device_address = AHT20_I2C_ADDR,
        .scl_speed_hz = 100000,
    };
    err = i2c_master_bus_add_device(i2c_bus0_handle, &aht20_cfg, &aht20_bus0_handle);
    return err;
}

esp_err_t aht20_read(float *temperature, float *humidity)
{
    esp_err_t err = ESP_OK;

    if (aht20_bus0_handle == NULL)
        return ESP_ERR_INVALID_STATE;

    uint8_t cmd[] = {0xAC, 0x33, 0x00};
    err = i2c_master_transmit(aht20_bus0_handle, cmd, sizeof(cmd), -1);

    vTaskDelay(pdMS_TO_TICKS(80)); // Wait for measurement

    uint8_t data[7];
    err = i2c_master_receive(aht20_bus0_handle, data, sizeof(data), -1);

    uint32_t raw_humidity = ((uint32_t)(data[1] << 12 | data[2] << 4 | (data[3] >> 4)));
    uint32_t raw_temperature = ((uint32_t)((data[3] & 0x0F) << 16 | data[4] << 8 | data[5]));

    *humidity = ((float)raw_humidity * 100) / 1048576.0;
    *temperature = ((float)raw_temperature * 200 / 1048576.0) - 50;

    return ESP_OK;
}