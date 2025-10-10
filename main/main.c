#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_err.h>
#include <aht.h>
#include <tca9548.h>
#include <scd4x.h>

#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_SDA_IO 39
#define I2C_MASTER_SCL_IO 38
#define I2C_MASTER_FREQ_HZ 100000

#define TCA9548_ADDR 0x70 // Default address
#define AHT20_ADDR 0x38
#define SCD41_ADDR 0x62
#define AHT_TYPE AHT_TYPE_AHT20

// Master bus device
static i2c_dev_t dev = {0};

// sensors
static aht_t aht20 = {0};
static i2c_dev_t scd41 = {0};

// I2c multiplexer
static i2c_dev_t i2c_switch = {0};

static const char *TAG = "i2c test";

void scan_i2c_bus(i2c_port_t port)
{
    ESP_LOGI("i2c-scan", "Scanning I2C bus on port %d...", port);
    for (uint8_t addr = 1; addr < 127; addr++)
    {
        i2c_dev_t dev = {
            .port = port,
            .addr = addr,
            .cfg = {
                .sda_io_num = I2C_MASTER_SDA_IO,
                .scl_io_num = I2C_MASTER_SCL_IO,
                .sda_pullup_en = GPIO_PULLUP_ENABLE,
                .scl_pullup_en = GPIO_PULLUP_ENABLE,
                .master.clk_speed = I2C_MASTER_FREQ_HZ,
            }};
        if (i2c_dev_probe(&dev, 1000) == ESP_OK)
        {
            ESP_LOGI("i2c-scan", "Found device at 0x%02X", addr);
        }
    }
}

void app_main(void)
{
    float temperature = 0;
    float humidity = 0;
    uint16_t co2 = 0;

    // I2C master descriptor
    dev.port = I2C_MASTER_NUM;
    dev.cfg.sda_io_num = I2C_MASTER_SDA_IO;
    dev.cfg.scl_io_num = I2C_MASTER_SCL_IO;
    dev.cfg.sda_pullup_en = GPIO_PULLUP_ENABLE;
    dev.cfg.scl_pullup_en = GPIO_PULLUP_ENABLE;
    dev.cfg.master.clk_speed = I2C_MASTER_FREQ_HZ;
    // dev.cfg.master.clk_speed = CONFIG_EXAMPLE_I2C_CLOCK_HZ;

    aht20.mode = AHT_MODE_NORMAL;
    aht20.type = AHT_TYPE;

    // Initialize the I2C bus
    ESP_ERROR_CHECK(i2cdev_init());

    ESP_ERROR_CHECK(tca9548_init_desc(&i2c_switch, TCA9548_ADDR, I2C_MASTER_NUM, I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO));
    ESP_ERROR_CHECK(aht_init_desc(&aht20, AHT20_ADDR, I2C_MASTER_NUM, I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO));
    ESP_ERROR_CHECK(scd4x_init_desc(&scd41, I2C_MASTER_NUM, I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO));

    // Switch to channel 0 and initialize AHT20
    ESP_ERROR_CHECK(tca9548_set_channels(&i2c_switch, 0x01)); // Select channel 0
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_ERROR_CHECK(aht_init(&aht20));

    // Switch to channel 1 and initialize SCD41
    ESP_ERROR_CHECK(tca9548_set_channels(&i2c_switch, 0x02)); // Select channel 1
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGI(TAG, "Initializing sensor...");
    ESP_ERROR_CHECK(scd4x_wake_up(&scd41));
    ESP_ERROR_CHECK(scd4x_stop_periodic_measurement(&scd41));
    ESP_ERROR_CHECK(scd4x_reinit(&scd41));
    ESP_LOGI(TAG, "Sensor initialized");
    ESP_ERROR_CHECK(scd4x_start_periodic_measurement(&scd41));
    ESP_LOGI(TAG, "Periodic measurements started");

    vTaskDelay(pdMS_TO_TICKS(5100)); // Wait for sensor to be ready

    while (1)
    {
        // Read AHT20 data
        ESP_ERROR_CHECK(tca9548_set_channels(&i2c_switch, 0x01)); // Select channel 0
        ESP_ERROR_CHECK(aht_get_data(&aht20, &temperature, &humidity));
        printf("AHT20 on TCA9548A Channel 0 → Temp: %.2f °C, Humidity: %.2f %%\n", temperature, humidity);

        vTaskDelay(pdMS_TO_TICKS(1000)); // Delay between readings

        // Read SCD41 data
        ESP_ERROR_CHECK(tca9548_set_channels(&i2c_switch, 0x02)); // Select channel 1
        ESP_ERROR_CHECK(scd4x_read_measurement(&scd41, &co2, &temperature, &humidity));
        printf("SCD41 on TCA9548A Channel 1 → CO2: %.2d ppm, Temp: %.2f °C, Humidity: %.2f %%\n", co2, temperature, humidity);

        vTaskDelay(pdMS_TO_TICKS(5000)); // Delay after readings
    }
}
