#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/i2c_master.h>
#include <esp_log.h>
#include <esp_err.h>

#define I2C_PORT I2C_NUM_0
#define I2C_MASTER_SCL_IO 38
#define I2C_MASTER_SDA_IO 39
#define I2C_FREQ_HZ 100000

#define TCA9548A_I2C_ADDRESS 0x70

#define AHT20_I2C_ADDRESS 0x38
#define AHT20_CMD_TRIGGER 0xBE

#define SCD41_ADDR 0x62

static const char *TAG = "I2C Test";

esp_err_t i2c_master_init(i2c_master_bus_handle_t *bus_handle)
{
    i2c_master_bus_config_t bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_PORT,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    return i2c_new_master_bus(&bus_config, bus_handle);
}

esp_err_t tca9548a_select_channel(i2c_master_bus_handle_t bus_handle, uint8_t channel)
{
    if (channel > 7)
        return ESP_ERR_INVALID_ARG;

    i2c_master_dev_handle_t dev_handle;
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TCA9548A_I2C_ADDRESS, // Default address of TCA9548A
        .scl_speed_hz = 100000,
    };

    esp_err_t err = i2c_master_bus_add_device(bus_handle, &dev_config, &dev_handle);
    if (err != ESP_OK)
        return err;

    uint8_t channel_mask = 1 << channel;
    err = i2c_master_transmit(dev_handle, &channel_mask, 1, -1);

    i2c_master_bus_rm_device(dev_handle); // Optional: remove device after use

    return err;
}

esp_err_t aht20_read(i2c_master_bus_handle_t bus, float *temperature, float *humidity)
{
    i2c_master_dev_handle_t dev_handle;
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AHT20_I2C_ADDRESS,
        .scl_speed_hz = 100000,
    };

    esp_err_t err = i2c_master_bus_add_device(bus, &dev_config, &dev_handle);
    if (err != ESP_OK)
        return err;

    // Trigger measurement
    uint8_t cmd = AHT20_CMD_TRIGGER;
    err = i2c_master_transmit(dev_handle, &cmd, 1, -1);
    if (err != ESP_OK)
    {
        i2c_master_bus_rm_device(dev_handle);
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(80)); // Wait for measurement to complete

    // Read 6 bytes of data
    uint8_t data[6];
    err = i2c_master_receive(dev_handle, data, sizeof(data), -1);
    i2c_master_bus_rm_device(dev_handle);
    if (err != ESP_OK)
        return err;

    // Parse humidity (20-bit)
    uint32_t raw_humidity = ((uint32_t)(data[1]) << 12) |
                            ((uint32_t)(data[2]) << 4) |
                            ((uint32_t)(data[3]) >> 4);

    // Parse temperature (20-bit)
    uint32_t raw_temp = (((uint32_t)(data[3] & 0x0F)) << 16) |
                        ((uint32_t)(data[4]) << 8) |
                        ((uint32_t)(data[5]));

    *humidity = (raw_humidity * 100.0) / 1048576.0;
    *temperature = (raw_temp * 200.0) / 1048576.0 - 50.0;

    return ESP_OK;
}

esp_err_t scd41_read(i2c_master_bus_handle_t bus, float *co2_ppm, float *temperature_c, float *humidity_rh)
{
    i2c_master_dev_handle_t dev_handle;
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SCD41_ADDR,
        .scl_speed_hz = 100000,
    };

    esp_err_t err = i2c_master_bus_add_device(bus, &dev_config, &dev_handle);
    if (err != ESP_OK)
        return err;

    // Start periodic measurement: 0x21B1
    uint8_t start_cmd[2] = {0x21, 0xB1};
    err = i2c_master_transmit(dev_handle, start_cmd, sizeof(start_cmd), -1);
    if (err != ESP_OK)
    {
        i2c_master_bus_rm_device(dev_handle);
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(5000)); // Wait for first measurement (typical: 5s)

    // Read measurement: 0xEC05
    uint8_t read_cmd[2] = {0xEC, 0x05};
    err = i2c_master_transmit(dev_handle, read_cmd, sizeof(read_cmd), -1);
    if (err != ESP_OK)
    {
        i2c_master_bus_rm_device(dev_handle);
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(1)); // Short delay before reading

    uint8_t data[9];
    err = i2c_master_receive(dev_handle, data, sizeof(data), -1);
    i2c_master_bus_rm_device(dev_handle);
    if (err != ESP_OK)
        return err;

    // Parse 16-bit values (ignore CRC bytes)
    uint16_t co2_raw = (data[0] << 8) | data[1];
    uint16_t temp_raw = (data[3] << 8) | data[4];
    uint16_t hum_raw = (data[6] << 8) | data[7];

    *co2_ppm = (float)co2_raw;
    *temperature_c = -45.0f + 175.0f * ((float)temp_raw / 65535.0f);
    *humidity_rh = 100.0f * ((float)hum_raw / 65535.0f);

    return ESP_OK;
}

void app_main(void)
{
    i2c_master_bus_handle_t bus;
    esp_err_t err = ESP_OK;

    uint8_t target_channel = 0;

    float co2 = 0.2f;
    float temp = 0.2f;
    float hum = 0.2f;

    err = i2c_master_init(&bus);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize I2C master: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "I2C master initialized");

    target_channel = 0;
    err = tca9548a_select_channel(bus, target_channel);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to switch TCA9548A channel: %s", esp_err_to_name(err));
    }
    else
    {
        ESP_LOGI(TAG, "Switched to TCA9548A channel %d", target_channel);
    }

    err = aht20_read(bus, &temp, &hum);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "Temperature: %.2f °C, Humidity: %.2f %%", temp, hum);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to read AHT20: %s", esp_err_to_name(err));
    }

    target_channel = 1; // Example: switch to channel 2
    err = tca9548a_select_channel(bus, target_channel);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to switch TCA9548A channel: %s", esp_err_to_name(err));
    }
    else
    {
        ESP_LOGI(TAG, "Switched to TCA9548A channel %d", target_channel);
    }

    err = scd41_read(bus, &co2, &temp, &hum);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "CO₂: %.1f ppm, Temp: %.2f °C, Humidity: %.2f %%", co2, temp, hum);
    }
    else
    {
        ESP_LOGE(TAG, "SCD41 read failed: %s", esp_err_to_name(err));
    }
}