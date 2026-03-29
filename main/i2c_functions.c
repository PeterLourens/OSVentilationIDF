/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include "i2c_functions.h"

static const char *TAG = "i2c_functions";

esp_err_t i2c_init_master0(void)
{
    esp_err_t err = ESP_OK;

    i2c_master_bus_config_t i2c_bus0_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = 0,
        .sda_io_num = hw.i2c_master_bus0_sda_io,
        .scl_io_num = hw.i2c_master_bus0_scl_io,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    err = i2c_new_master_bus(&i2c_bus0_cfg, &i2c_bus0_handle);
    if (err != ESP_OK)
    {
        LOGE(TAG, "Failed to initialize I2C master bus 0: %s", esp_err_to_name(err));
    }

    return err;
}

esp_err_t i2c_init_master1(void)
{
    esp_err_t err = ESP_OK;

    i2c_master_bus_config_t i2c_bus1_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = 1,
        .sda_io_num = hw.i2c_master_bus1_sda_io,
        .scl_io_num = hw.i2c_master_bus1_scl_io,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    err = i2c_new_master_bus(&i2c_bus1_cfg, &i2c_bus1_handle);
    if (err != ESP_OK)
    {
        LOGE(TAG, "Failed to initialize I2C master bus 0: %s", esp_err_to_name(err));
    }
    return err;
}