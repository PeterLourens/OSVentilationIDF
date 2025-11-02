#include "i2c_lcd.h"

static const char *TAG = "LCD";

esp_err_t i2c_lcd_init(i2c_master_bus_handle_t bus_handle, i2c_master_dev_handle_t *dev_handle)
{
    i2c_device_config_t lcd_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = LCD_I2C_ADDR, // 0x27
        .scl_speed_hz = 100000,
    };

    esp_err_t err = i2c_master_bus_add_device(bus_handle, &lcd_cfg, dev_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to add LCD device: %s", esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}
