#include "i2c_sensors.h"

static const char *TAG = "i2c sensors";

// FIFO buffer for sensor moving average calculation
static float sensor_fifo[2][8][3][MAX_FIFO_SIZE];
static float sum[SENSOR_AVG_SIZE] = {0.0};
static float avg[SENSOR_AVG_SIZE] = {0.0};
static int fifoSize[SENSOR_AVG_SIZE] = {0}; // Current number of elements in the FIFO.
static int fifoHead[SENSOR_AVG_SIZE] = {0}; // Index of the oldest element in the FIFO.

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
    err = i2c_master_bus_add_device(bus_handle, &aht20_cfg, dev_handle);
    return err;
}

esp_err_t aht20_read(i2c_master_dev_handle_t dev_handle, float *temperature, float *humidity)
{
    esp_err_t err = ESP_OK;

    if (aht20_bus0_handle == NULL)
        return ESP_ERR_INVALID_STATE;

    uint8_t cmd[] = {0xAC, 0x33, 0x00};
    err = i2c_master_transmit(dev_handle, cmd, sizeof(cmd), -1);

    vTaskDelay(pdMS_TO_TICKS(120)); // Wait for measurement

    uint8_t data[7];
    err = i2c_master_receive(aht20_bus0_handle, data, sizeof(data), -1);

    uint32_t raw_humidity = ((uint32_t)(data[1] << 12 | data[2] << 4 | (data[3] >> 4)));
    uint32_t raw_temperature = ((uint32_t)((data[3] & 0x0F) << 16 | data[4] << 8 | data[5]));

    *humidity = ((float)raw_humidity * 100) / 1048576.0;
    *temperature = ((float)raw_temperature * 200 / 1048576.0) - 50;

    return err;
}

esp_err_t i2c_dht20_init(i2c_master_bus_handle_t bus_handle, i2c_master_dev_handle_t *dev_handle)
{
    esp_err_t err = ESP_OK;
    i2c_device_config_t dht20_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = DHT20_I2C_ADDR,
        .scl_speed_hz = 100000,
    };
    err = i2c_master_bus_add_device(bus_handle, &dht20_cfg, dev_handle);

    return err;
}

esp_err_t dht20_read(i2c_master_dev_handle_t dev_handle, float *temperature, float *humidity)
{
    esp_err_t err;
    uint8_t cmd[] = {0xAC, 0x33, 0x00};

    // Wake up the sensor (dummy read)
    uint8_t dummy[1];
    i2c_master_receive(dev_handle, dummy, 1, -1);
    vTaskDelay(pdMS_TO_TICKS(50));

    // Check status register
    uint8_t status[1];
    err = i2c_master_receive(dev_handle, status, 1, -1);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read status: %s", esp_err_to_name(err));
        return err;
    }

    // Bit 3 and 4 must be set (calibrated + ready)
    if ((status[0] & 0x18) != 0x18)
    {
        ESP_LOGW(TAG, "DHT20 not calibrated yet (status=0x%02X)", status[0]);
        // You can retry or send init command here if needed
    }
    // else
    //{
    // ESP_LOGI(TAG, "DHT20 ready (status=0x%02X)", status[0]);
    //}

    // Trigger measurement
    err = i2c_master_transmit(dev_handle, cmd, sizeof(cmd), -1);
    if (err != ESP_OK)
        return err;

    vTaskDelay(pdMS_TO_TICKS(80)); // wait at least 80ms

    uint8_t data[7];
    err = i2c_master_receive(dev_handle, data, sizeof(data), -1);
    if (err != ESP_OK)
        return err;

    // Check busy bit
    if (data[0] & 0x80)
    {
        return ESP_ERR_INVALID_STATE; // still busy
    }

    // Parse values
    uint32_t raw_humidity = ((uint32_t)data[1] << 12) | ((uint32_t)data[2] << 4) | (data[3] >> 4);
    uint32_t raw_temperature = (((uint32_t)data[3] & 0x0F) << 16) | ((uint32_t)data[4] << 8) | data[5];

    *humidity = (raw_humidity * 100.0f) / 1048576.0f;
    *temperature = (raw_temperature * 200.0f / 1048576.0f) - 50.0f;

    return ESP_OK;
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

    // Perform soft reset
    uint8_t reset_cmd[2] = {0x36, 0x32};
    err = i2c_master_transmit(*dev_handle, reset_cmd, 2, pdMS_TO_TICKS(100));
    if (err != ESP_OK)
    {
        ESP_LOGE("SCD41", "Failed to send reset command: %s", esp_err_to_name(err));
        return err;
    }

    // Wait for reset to complete (minimum 2ms)
    vTaskDelay(pdMS_TO_TICKS(20));

    // Stop any ongoing measurement
    uint8_t stop_cmd[2] = {0x3F, 0x86};
    err = i2c_master_transmit(*dev_handle, stop_cmd, 2, pdMS_TO_TICKS(100));
    if (err != ESP_OK)
    {
        ESP_LOGE("SCD41", "Failed to stop measurement: %s", esp_err_to_name(err));
        return err;
    }

    // Wait for stop to complete
    vTaskDelay(pdMS_TO_TICKS(500));

    // Start periodic measurement
    uint8_t start_cmd[2] = {0x21, 0xB1};
    err = i2c_master_transmit(*dev_handle, start_cmd, 2, pdMS_TO_TICKS(100));
    if (err != ESP_OK)
    {
        ESP_LOGE("SCD41", "Failed to start measurement: %s", esp_err_to_name(err));
        return err;
    }

    // Wait for first measurement to be ready (minimum 5 seconds)
    vTaskDelay(pdMS_TO_TICKS(5000));

    ESP_LOGI("SCD41", "SCD41 initialized successfully");
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

esp_err_t scd41_data_ready(i2c_master_dev_handle_t dev_handle, bool *data_ready)
{
    if (!dev_handle || !data_ready)
        return ESP_ERR_INVALID_ARG;

    uint8_t cmd[2] = {0xE4, 0xB8}; // Get data ready status
    uint8_t response[3];           // MSB, LSB, CRC

    esp_err_t err = i2c_master_transmit(dev_handle, cmd, sizeof(cmd), pdMS_TO_TICKS(100));
    if (err != ESP_OK)
    {
        ESP_LOGE("SCD41", "Transmit failed: %s", esp_err_to_name(err));
        return err;
    }

    err = i2c_master_receive(dev_handle, response, sizeof(response), pdMS_TO_TICKS(100));
    if (err != ESP_OK)
    {
        ESP_LOGE("SCD41", "Receive failed: %s", esp_err_to_name(err));
        return err;
    }

    // CRC check on first two data bytes
    uint8_t crc = scd41_crc8(response, 2);
    if (crc != response[2])
    {
        ESP_LOGE("SCD41", "CRC mismatch: got 0x%02X, expected 0x%02X", response[2], crc);
        return ESP_ERR_INVALID_CRC;
    }

    // Combine into 16-bit status word (MSB first)
    uint16_t status = ((uint16_t)response[0] << 8) | response[1];

    // Bit 15 (MSB) indicates data ready: 1 = ready
    *data_ready = (status & 0x8000) != 0;

    // ESP_LOGD("SCD41", "Status raw: MSB=0x%02X LSB=0x%02X (0x%04X), ready=%d", response[0], response[1], status, *data_ready);

    return ESP_OK;
}

esp_err_t scd41_read(i2c_master_dev_handle_t dev_handle, uint16_t *co2, float *temp, float *rh)
{
    if (!dev_handle || !co2 || !temp || !rh)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t cmd[2] = {0xEC, 0x05}; // read measurement command
    uint8_t response[9];           // 3 words (2 bytes + CRC each)

    // Send read measurement command
    esp_err_t err = i2c_master_transmit(dev_handle, cmd, sizeof(cmd), pdMS_TO_TICKS(100));
    if (err != ESP_OK)
    {
        ESP_LOGE("SCD41", "Failed to send read command: %s", esp_err_to_name(err));
        return err;
    }

    // Add delay between send and receive (minimum 1ms according to datasheet)
    vTaskDelay(pdMS_TO_TICKS(5));

    // Read 9 bytes with timeout
    err = i2c_master_receive(dev_handle, response, sizeof(response), pdMS_TO_TICKS(100));
    if (err != ESP_OK)
    {
        ESP_LOGE("SCD41", "Failed to read measurement data: %s", esp_err_to_name(err));
        return err;
    }

    // Parse CO₂ with CRC check
    if (scd41_crc8(response, 2) != response[2])
    {
        ESP_LOGE("SCD41", "CO2 CRC check failed");
        return ESP_ERR_INVALID_CRC;
    }
    *co2 = ((uint16_t)response[0] << 8) | response[1];

    // Parse Temperature with CRC check
    if (scd41_crc8(response + 3, 2) != response[5])
    {
        ESP_LOGE("SCD41", "Temperature CRC check failed");
        return ESP_ERR_INVALID_CRC;
    }
    uint16_t raw_temp = ((uint16_t)response[3] << 8) | response[4];
    *temp = -45.0f + 175.0f * ((float)raw_temp / 65535.0f);

    // Parse RH with CRC check
    if (scd41_crc8(response + 6, 2) != response[8])
    {
        ESP_LOGE("SCD41", "Humidity CRC check failed");
        return ESP_ERR_INVALID_CRC;
    }
    uint16_t raw_rh = ((uint16_t)response[6] << 8) | response[7];
    *rh = 100.0f * ((float)raw_rh / 65535.0f);

    return ESP_OK;
}

bool is_scd4x(const char *type)
{
    return (strcmp(type, "SCD41") == 0 || strcmp(type, "SCD40") == 0);
}

bool is_aht20(const char *type)
{
    return (strcmp(type, "AHT20") == 0);
}

bool is_dht20(const char *type)
{
    return (strcmp(type, "DHT20") == 0);
}

esp_err_t init_sensors_on_mux_channels(void)
{
    esp_err_t err;

    // --- Bus0 ---
    for (uint8_t ch = 0; ch < CHANNELS_PER_BUS; ch++)
    {
        // ESP_LOGI(TAG, "Checking sensor config on bus 0 channel %d: %s", ch, bus0_sensor_settings[ch].type);
        if (strcmp(bus0_sensor_settings[ch].type, "None") == 0)
        {
            ESP_LOGI(TAG, "No sensor configured on bus 0 channel %d", ch);
            continue;
        }

        uint8_t mask = (0x01 << ch);
        err = i2c_tca9548_set_channel(tca9548_bus0_handle, mask);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to set channel %d on bus 0: %s", ch, esp_err_to_name(err));
            continue;
        }
        else
        {
            ESP_LOGI(TAG, "Channel %d on bus 0 set successfully", ch);
        }
        // if (err != ESP_OK)
        // continue;

        if (is_aht20(bus0_sensor_settings[ch].type))
        {

            err = i2c_aht20_init(i2c_bus0_handle, &aht20_bus0_handle);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to initialize AHT20 on bus 0 channel %d: %s", ch, esp_err_to_name(err));
            }
            else
            {
                ESP_LOGI(TAG, "AHT20 initialized on bus 0 channel %d", ch);
            }
        }
        else if (is_dht20(bus0_sensor_settings[ch].type))
        {
            err = i2c_dht20_init(i2c_bus0_handle, &dht20_bus0_handle);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to initialize DHT20 on bus 0 channel %d: %s", ch, esp_err_to_name(err));
            }
            else
            {
                ESP_LOGI(TAG, "DHT20 initialized on bus 0 channel %d", ch);
            }
        }
        else if (is_scd4x(bus0_sensor_settings[ch].type))
        {
            err = i2c_init_scd41(i2c_bus0_handle, &scd41_bus0_handle);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to initialize SCD4x on bus 0 channel %d: %s", ch, esp_err_to_name(err));
            }
            else
            {
                ESP_LOGI(TAG, "SCD4x initialized on bus 0 channel %d", ch);
            }
        }
    }

    // --- Bus1 ---
    for (uint8_t ch = 0; ch < CHANNELS_PER_BUS; ch++)
    {
        // ESP_LOGI(TAG, "Checking sensor config on bus 1 channel %d: %s", ch, bus1_sensor_settings[ch].type);
        if (strcmp(bus1_sensor_settings[ch].type, "None") == 0)
        {
            ESP_LOGI(TAG, "No sensor configured on bus 1 channel %d", ch);
            continue;
        }

        uint8_t mask = (0x01 << ch);
        err = i2c_tca9548_set_channel(tca9548_bus1_handle, mask);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to set channel %d on bus 1: %s", ch, esp_err_to_name(err));
            continue;
        }
        // else
        // {
        //     ESP_LOGI(TAG, "Channel %d on bus 1 set successfully", ch);
        // }
        // if (err != ESP_OK)
        // continue;

        if (is_aht20(bus1_sensor_settings[ch].type))
        {
            err = i2c_aht20_init(i2c_bus1_handle, &aht20_bus1_handle);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to initialize AHT20 on bus 1 channel %d: %s", ch, esp_err_to_name(err));
            }
            else
            {
                ESP_LOGI(TAG, "AHT20 initialized on bus 1 channel %d", ch);
            }
        }
        else if (is_dht20(bus1_sensor_settings[ch].type))
        {
            err = i2c_dht20_init(i2c_bus1_handle, &dht20_bus1_handle);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to initialize DHT20 on bus 1 channel %d: %s", ch, esp_err_to_name(err));
            }
            else
            {
                ESP_LOGI(TAG, "DHT20 initialized on bus 1 channel %d", ch);
            }
        }
        else if (is_scd4x(bus1_sensor_settings[ch].type))
        {
            err = i2c_init_scd41(i2c_bus1_handle, &scd41_bus1_handle);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to initialize SCD4x on bus 1 channel %d: %s", ch, esp_err_to_name(err));
            }
            else
            {
                ESP_LOGI(TAG, "SCD4x initialized on bus 1 channel %d", ch);
            }
        }
    }

    return ESP_OK;
}

esp_err_t read_sensors_into_queue(void)
{
    esp_err_t err;
    double snapshot[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS] = {0};

    // --- Bus0 ---
    for (uint8_t ch = 0; ch < CHANNELS_PER_BUS; ch++)
    {
        snapshot[0][ch][4] = 0; // valid = false

        if (strcmp(bus0_sensor_settings[ch].type, "None") == 0)
            continue;

        uint8_t mask = (0x01 << ch);
        err = i2c_tca9548_set_channel(tca9548_bus0_handle, mask);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to set channel %d on bus 0: %s", ch, esp_err_to_name(err));
            continue;
        }

        snapshot[0][ch][3] = (double)esp_timer_get_time(); // timestamp

        if (is_aht20(bus0_sensor_settings[ch].type))
        {
            float temp, rh;
            if (aht20_read(aht20_bus0_handle, &temp, &rh) == ESP_OK)
            {
                snapshot[0][ch][0] = temp;
                snapshot[0][ch][1] = rh;
                snapshot[0][ch][4] = 1; // valid
            }
        }
        if (is_dht20(bus0_sensor_settings[ch].type))
        {
            float temp, rh;
            if (dht20_read(dht20_bus0_handle, &temp, &rh) == ESP_OK)
            {
                snapshot[0][ch][0] = temp;
                snapshot[0][ch][1] = rh;
                snapshot[0][ch][4] = 1; // valid
            }
        }
        else if (is_scd4x(bus0_sensor_settings[ch].type))
        {
            uint16_t co2;
            float temp, rh;
            bool data_ready = false;

            // Check if data is ready
            esp_err_t err = scd41_data_ready(scd41_bus0_handle, &data_ready);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to check data ready status on channel %d: %s", ch, esp_err_to_name(err));
                continue;
            }

            if (!data_ready)
            {
                ESP_LOGW(TAG, "Data not ready on channel %d, waiting for next cycle", ch);
                continue;
            }

            // Read data only when ready
            err = scd41_read(scd41_bus0_handle, &co2, &temp, &rh);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to read SCD41 on channel %d: %s", ch, esp_err_to_name(err));
                continue;
            }

            snapshot[0][ch][0] = temp;
            snapshot[0][ch][1] = rh;
            snapshot[0][ch][2] = co2;
            snapshot[0][ch][4] = 1; // Mark as valid

            // ESP_LOGI(TAG, "SCD41 read success - CO2: %d ppm, Temp: %.2f°C, RH: %.2f%%",co2, temp, rh);
        }
    }

    // --- Bus1 ---
    for (uint8_t ch = 0; ch < CHANNELS_PER_BUS; ch++)
    {
        snapshot[1][ch][4] = 0;

        if (strcmp(bus1_sensor_settings[ch].type, "None") == 0)
            continue;

        uint8_t mask = (1 << ch);
        err = i2c_tca9548_set_channel(tca9548_bus1_handle, mask);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to set channel %d on bus 1: %s", ch, esp_err_to_name(err));
            continue;
        }
        // else
        // {
        //     ESP_LOGI(TAG, "Channel %d on bus 0 set successfully", ch);
        // }
        // if (err != ESP_OK)
        // continue;

        snapshot[1][ch][3] = (double)esp_timer_get_time();

        if (is_aht20(bus1_sensor_settings[ch].type))
        {
            float temp, rh;
            if (aht20_read(aht20_bus1_handle, &temp, &rh) == ESP_OK)
            {
                snapshot[1][ch][0] = temp;
                snapshot[1][ch][1] = rh;
                snapshot[1][ch][4] = 1;
            }
            else
            {
                ESP_LOGE(TAG, "Failed to read AHT20 on bus 1 channel %d", ch);
            }
        }
        else if (is_dht20(bus1_sensor_settings[ch].type))
        {
            float temp, rh;
            if (dht20_read(dht20_bus1_handle, &temp, &rh) == ESP_OK)
            {
                snapshot[1][ch][0] = temp;
                snapshot[1][ch][1] = rh;
                snapshot[1][ch][4] = 1;
            }
            else
            {
                ESP_LOGE(TAG, "Failed to read AHT20 on bus 1 channel %d", ch);
            }
        }
        else if (is_scd4x(bus1_sensor_settings[ch].type))
        {
            uint16_t co2;
            float temp, rh;
            if (scd41_read(scd41_bus1_handle, &co2, &temp, &rh) == ESP_OK)
            {
                snapshot[1][ch][0] = temp;
                snapshot[1][ch][1] = rh;
                snapshot[1][ch][2] = co2;
                snapshot[1][ch][4] = 1;
            }
            else
            {
                ESP_LOGE(TAG, "Failed to read SCD4x on bus 1 channel %d", ch);
            }
        }
    }

    // For debug
    /*for (int bus = 0; bus < MAX_I2C_BUSES; bus++)
    {
        for (int ch = 0; ch < CHANNELS_PER_BUS; ch++)
        {
            if (snapshot[bus][ch][IDX_VALID])
            {
                ESP_LOGI(TAG, "Bus %d, CH%d: Temp=%.2f °C, RH=%.2f %%, CO₂=%.0f ppm", bus, ch, snapshot[bus][ch][IDX_TEMP], snapshot[bus][ch][IDX_HUMIDITY], snapshot[bus][ch][IDX_CO2]);
            }
        }
    }*/

    // Overwrite the queue with the full snapshot
    xQueueOverwrite(sensor_queue, snapshot);

    return ESP_OK;
}

void sensor_data_average(void)
{
    static const char *TAG = "Sensor avg";

    int pos;
    float sensor_data[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS] = {0};
    float sensor_avg_data[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS] = {0};

    if (xQueuePeek(sensor_queue, sensor_data, 0) == pdTRUE)
    {
        pos = 0;
        for (int bus = 0; bus < 2; bus++)
        {
            for (int slot = 0; slot < 8; slot++)
            {
                for (int measurement = 0; measurement < 3; measurement++)
                {
                    fifoPush(bus, slot, measurement, sensor_data[bus][slot][measurement], pos);
                    sensor_avg_data[bus][slot][measurement] = avg[pos];
                    pos++; // counter for fifoSize and fifoHead
                }
            }
        }

        for (int bus = 0; bus < MAX_I2C_BUSES; bus++)
        {
            for (int ch = 0; ch < CHANNELS_PER_BUS; ch++)
            {
                if (sensor_avg_data[bus][ch][IDX_VALID])
                {
                    ESP_LOGI(TAG, "Bus %d, CH%d: Temp=%.2f °C, RH=%.2f %%, CO₂=%.0f ppm", bus, ch, sensor_avg_data[bus][ch][IDX_TEMP], sensor_avg_data[bus][ch][IDX_HUMIDITY], sensor_avg_data[bus][ch][IDX_CO2]);
                }
            }
        }

        if (sensor_avg_queue != NULL)
        {
            xQueueOverwrite(sensor_avg_queue, sensor_avg_data);
        }
        else
        {
            ESP_LOGE(TAG, "Average sensor data queue handle is NULL.");
        }
    }
}

// FIFO push function to add a new value and update sum and average required for sensor moving average calculation
void fifoPush(int bus, int slot, int measurement, float value, int pos)
{
    if (fifoSize[pos] < MAX_FIFO_SIZE)
    {
        // Add the value to the next available position.
        sensor_fifo[bus][slot][measurement][(fifoHead[pos] + fifoSize[pos]) % MAX_FIFO_SIZE] = value;
        sum[pos] += value;
        fifoSize[pos] = fifoSize[pos] + 1;
    }
    else
    {
        // Overwrite the oldest value and move the head forward.
        sum[pos] -= sensor_fifo[bus][slot][measurement][fifoHead[pos]];
        sensor_fifo[bus][slot][measurement][fifoHead[pos]] = value;
        sum[pos] += value;
        fifoHead[pos] = (fifoHead[pos] + 1) % MAX_FIFO_SIZE;
    }
    avg[pos] = sum[pos] / fifoSize[pos];
}