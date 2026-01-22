#include "i2c_functions.h"

static const char *TAG = "i2c_functions";

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