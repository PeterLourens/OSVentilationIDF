#include "i2c_sensors.h"

static const char *TAG = "i2c sensors";

// FIFO buffer for sensor moving average calculation
static float sensor_fifo[MAX_I2C_BUSES][CHANNELS_PER_BUS][3][MAX_FIFO_SIZE];
static float sum[SENSOR_AVG_SIZE] = {0};
static float avg[SENSOR_AVG_SIZE] = {0};
static int fifoSize[SENSOR_AVG_SIZE] = {0};
static int fifoHead[SENSOR_AVG_SIZE] = {0};

// -----------------------------------------------------------------------------
// Sensor slot model (per bus, per channel)
// -----------------------------------------------------------------------------
typedef enum
{
    SENSOR_NONE = 0,
    SENSOR_AHT20,
    SENSOR_DHT20,
    SENSOR_SCD41,
} sensor_type_t;

typedef struct
{
    sensor_type_t type;
    i2c_master_dev_handle_t handle;
} sensor_slot_t;

static sensor_slot_t bus0_sensors[CHANNELS_PER_BUS] = {0};
static sensor_slot_t bus1_sensors[CHANNELS_PER_BUS] = {0};

// -----------------------------------------------------------------------------
// TCA9548A helpers
// -----------------------------------------------------------------------------
esp_err_t i2c_init_tca9548(i2c_master_bus_handle_t bus_handle, i2c_master_dev_handle_t *dev_handle)
{
    if (!bus_handle || !dev_handle)
    {
        return ESP_ERR_INVALID_ARG;
    }

    const i2c_device_config_t tca9548_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TCA9548_I2C_ADDR,
        .scl_speed_hz = 100000,
    };

    esp_err_t err = i2c_master_bus_add_device(bus_handle, &tca9548_cfg, dev_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE("TCA9548A", "Failed to add device: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t i2c_tca9548_set_channel(i2c_master_dev_handle_t dev_handle, uint8_t channel)
{
    if (!dev_handle || channel > 7)
        return ESP_ERR_INVALID_ARG;

    uint8_t mask = (1u << channel);

    esp_err_t err = i2c_master_transmit(dev_handle, &mask, 1, -1);
    if (err != ESP_OK)
    {
        ESP_LOGE("TCA9548A", "Failed to select channel %u (mask=0x%02X): %s", channel, mask, esp_err_to_name(err));
    }
    return err;
}

// -----------------------------------------------------------------------------
// Sensor type helpers
// -----------------------------------------------------------------------------
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

static sensor_type_t sensor_type_from_str(const char *type_str)
{
    if (!type_str)
        return SENSOR_NONE;

    if (is_aht20(type_str))
        return SENSOR_AHT20;
    if (is_dht20(type_str))
        return SENSOR_DHT20;
    if (is_scd4x(type_str))
        return SENSOR_SCD41;
    if (strcmp(type_str, "None") == 0)
        return SENSOR_NONE;

    return SENSOR_NONE;
}

// -----------------------------------------------------------------------------
// Pre-load sensor type so sensors only once
// -----------------------------------------------------------------------------
void preload_sensor_types_from_config(void)
{
    for (int ch = 0; ch < CHANNELS_PER_BUS; ch++)
    {
        bus0_sensors[ch].type = sensor_type_from_str(bus0_sensor_settings[ch].type);
        bus1_sensors[ch].type = sensor_type_from_str(bus1_sensor_settings[ch].type);
    }
}

// -----------------------------------------------------------------------------
// Sensor init helpers (per type)
// -----------------------------------------------------------------------------
esp_err_t i2c_aht20_init(i2c_master_bus_handle_t bus_handle, i2c_master_dev_handle_t *dev_handle)
{
    if (!bus_handle || !dev_handle)
        return ESP_ERR_INVALID_ARG;

    const i2c_device_config_t aht20_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AHT20_I2C_ADDR,
        .scl_speed_hz = 100000,
    };
    return i2c_master_bus_add_device(bus_handle, &aht20_cfg, dev_handle);
}

esp_err_t aht20_read(i2c_master_dev_handle_t dev_handle, float *temperature, float *humidity)
{
    if (!dev_handle || !temperature || !humidity)
        return ESP_ERR_INVALID_ARG;

    esp_err_t err;
    uint8_t cmd[] = {0xAC, 0x33, 0x00};

    // Trigger measurement
    err = i2c_master_transmit(dev_handle, cmd, sizeof(cmd), -1);
    if (err != ESP_OK)
        return err;

    vTaskDelay(pdMS_TO_TICKS(120)); // Wait for measurement

    uint8_t data[7];
    err = i2c_master_receive(dev_handle, data, sizeof(data), -1);
    if (err != ESP_OK)
        return err;

    uint32_t raw_humidity = ((uint32_t)(data[1] << 12 | data[2] << 4 | (data[3] >> 4)));
    uint32_t raw_temperature = ((uint32_t)((data[3] & 0x0F) << 16 | data[4] << 8 | data[5]));

    *humidity = ((float)raw_humidity * 100.0f) / 1048576.0f;
    *temperature = ((float)raw_temperature * 200.0f / 1048576.0f) - 50.0f;

    return ESP_OK;
}

esp_err_t i2c_dht20_init(i2c_master_bus_handle_t bus_handle, i2c_master_dev_handle_t *dev_handle)
{
    if (!bus_handle || !dev_handle)
        return ESP_ERR_INVALID_ARG;

    const i2c_device_config_t dht20_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = DHT20_I2C_ADDR,
        .scl_speed_hz = 100000,
    };
    return i2c_master_bus_add_device(bus_handle, &dht20_cfg, dev_handle);
}

esp_err_t dht20_read(i2c_master_dev_handle_t dev_handle, float *temperature, float *humidity)
{
    if (!dev_handle || !temperature || !humidity)
        return ESP_ERR_INVALID_ARG;

    esp_err_t err;
    uint8_t cmd[] = {0xAC, 0x33, 0x00};

    // Wake up sensor (dummy read)
    uint8_t dummy[1];
    (void)i2c_master_receive(dev_handle, dummy, 1, -1);
    vTaskDelay(pdMS_TO_TICKS(50));

    // Read status
    uint8_t status[1];
    err = i2c_master_receive(dev_handle, status, 1, -1);
    if (err != ESP_OK)
        return err;

    // Trigger measurement
    err = i2c_master_transmit(dev_handle, cmd, sizeof(cmd), -1);
    if (err != ESP_OK)
        return err;

    vTaskDelay(pdMS_TO_TICKS(80));

    uint8_t data[7];
    err = i2c_master_receive(dev_handle, data, sizeof(data), -1);
    if (err != ESP_OK)
        return err;

    if (data[0] & 0x80)
        return ESP_ERR_INVALID_STATE; // still busy

    uint32_t raw_humidity = ((uint32_t)data[1] << 12) | ((uint32_t)data[2] << 4) | (data[3] >> 4);
    uint32_t raw_temperature = (((uint32_t)data[3] & 0x0F) << 16) | ((uint32_t)data[4] << 8) | data[5];

    *humidity = (raw_humidity * 100.0f) / 1048576.0f;
    *temperature = (raw_temperature * 200.0f / 1048576.0f) - 50.0f;

    return ESP_OK;
}

esp_err_t i2c_init_scd41(i2c_master_bus_handle_t bus_handle, i2c_master_dev_handle_t *dev_handle)
{
    if (!bus_handle || !dev_handle)
        return ESP_ERR_INVALID_ARG;

    const i2c_device_config_t scd41_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = SCD41_I2C_ADDR,
        .scl_speed_hz = 100000,
    };

    esp_err_t err = i2c_master_bus_add_device(bus_handle, &scd41_cfg, dev_handle);
    if (err != ESP_OK)
        return err;

    // Optional but recommended: stop any previous measurement
    uint8_t stop_cmd[2] = {0x3F, 0x86};
    i2c_master_transmit(*dev_handle, stop_cmd, 2, pdMS_TO_TICKS(100));
    vTaskDelay(pdMS_TO_TICKS(50));

    // Reset
    uint8_t reset_cmd[2] = {0x36, 0x32};
    err = i2c_master_transmit(*dev_handle, reset_cmd, 2, pdMS_TO_TICKS(100));
    if (err != ESP_OK)
        return err;

    // REQUIRED: wait 1 second AFTER reset
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Start periodic measurement
    uint8_t start_cmd[2] = {0x21, 0xB1};
    err = i2c_master_transmit(*dev_handle, start_cmd, 2, pdMS_TO_TICKS(100));
    if (err != ESP_OK)
        return err;

    // Warm-up
    vTaskDelay(pdMS_TO_TICKS(5000));

    ESP_LOGI("SCD41", "SCD41 initialized successfully");
    return ESP_OK;
}

// -----------------------------------------------------------------------------
// SCD41 helpers
// -----------------------------------------------------------------------------
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

    uint8_t cmd[2] = {0xE4, 0xB8};
    uint8_t response[3];

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

    uint8_t crc = scd41_crc8(response, 2);
    if (crc != response[2])
    {
        ESP_LOGE("SCD41", "CRC mismatch: got 0x%02X, expected 0x%02X", response[2], crc);
        return ESP_ERR_INVALID_CRC;
    }

    uint16_t status = ((uint16_t)response[0] << 8) | response[1];
    *data_ready = (status & 0x8000) != 0;

    return ESP_OK;
}

esp_err_t scd41_read(i2c_master_dev_handle_t dev_handle, uint16_t *co2, float *temp, float *rh)
{
    if (!dev_handle || !co2 || !temp || !rh)
        return ESP_ERR_INVALID_ARG;

    uint8_t cmd[2] = {0xEC, 0x05};
    uint8_t response[9];

    esp_err_t err = i2c_master_transmit(dev_handle, cmd, sizeof(cmd), pdMS_TO_TICKS(100));
    if (err != ESP_OK)
        return err;

    vTaskDelay(pdMS_TO_TICKS(5));

    err = i2c_master_receive(dev_handle, response, sizeof(response), pdMS_TO_TICKS(100));
    if (err != ESP_OK)
        return err;

    // CO₂
    if (scd41_crc8(response, 2) != response[2])
        return ESP_ERR_INVALID_CRC;
    *co2 = ((uint16_t)response[0] << 8) | response[1];

    // Temperature
    if (scd41_crc8(response + 3, 2) != response[5])
        return ESP_ERR_INVALID_CRC;
    uint16_t raw_temp = ((uint16_t)response[3] << 8) | response[4];
    *temp = -45.0f + 175.0f * ((float)raw_temp / 65535.0f);

    // Humidity
    if (scd41_crc8(response + 6, 2) != response[8])
        return ESP_ERR_INVALID_CRC;
    uint16_t raw_rh = ((uint16_t)response[6] << 8) | response[7];
    *rh = 100.0f * ((float)raw_rh / 65535.0f);

    return ESP_OK;
}

// -----------------------------------------------------------------------------
// Per-bus channel configuration
// -----------------------------------------------------------------------------
/*static esp_err_t configure_bus_channel(sensor_slot_t *slots, i2c_master_bus_handle_t bus_handle, i2c_master_dev_handle_t tca_handle, uint8_t ch, const char *type_str)
{
    if (!slots || !bus_handle || !tca_handle || ch >= CHANNELS_PER_BUS)
    {
        return ESP_ERR_INVALID_ARG;
    }

    sensor_type_t new_type = sensor_type_from_str(type_str);

    if (new_type == SENSOR_NONE)
    {
        ESP_LOGI(TAG, "No sensor configured on bus channel %u", ch);

        if (slots[ch].handle != NULL)
        {
            i2c_master_bus_rm_device(slots[ch].handle);
            slots[ch].handle = NULL;
            slots[ch].type = SENSOR_NONE;
        }
        return ESP_OK;
    }

    esp_err_t err = i2c_tca9548_set_channel(tca_handle, ch);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set channel %u: %s", ch, esp_err_to_name(err));
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(3));

    if (slots[ch].handle != NULL)
    {
        i2c_master_bus_rm_device(slots[ch].handle);
        slots[ch].handle = NULL;
        slots[ch].type = SENSOR_NONE;
    }

    i2c_master_dev_handle_t h = NULL;

    switch (new_type)
    {
    case SENSOR_AHT20:
        err = i2c_aht20_init(bus_handle, &h);
        break;
    case SENSOR_DHT20:
        err = i2c_dht20_init(bus_handle, &h);
        break;
    case SENSOR_SCD41:
        err = i2c_init_scd41(bus_handle, &h);
        break;
    default:
        return ESP_ERR_INVALID_ARG;
    }

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize %s on channel %u: %s", type_str, ch, esp_err_to_name(err));
        return err;
    }

    slots[ch].handle = h;
    slots[ch].type = new_type;

    ESP_LOGI(TAG, "%s initialized on channel %u", type_str, ch);
    return ESP_OK;
}*/
static esp_err_t configure_bus_channel(sensor_slot_t *slots, i2c_master_bus_handle_t bus_handle, i2c_master_dev_handle_t tca_handle, uint8_t ch, const char *type_str)
{
    if (!slots || !bus_handle || !tca_handle || ch >= CHANNELS_PER_BUS)
        return ESP_ERR_INVALID_ARG;

    sensor_type_t new_type = sensor_type_from_str(type_str);
    sensor_type_t old_type = slots[ch].type;

    // ---------------------------------------------------------------------
    // CASE 1: No sensor configured
    // ---------------------------------------------------------------------
    if (new_type == SENSOR_NONE)
    {
        if (slots[ch].handle != NULL)
        {
            i2c_master_bus_rm_device(slots[ch].handle);
            slots[ch].handle = NULL;
        }
        slots[ch].type = SENSOR_NONE;
        ESP_LOGI(TAG, "No sensor configured on bus channel %u", ch);
        return ESP_OK;
    }

    // ---------------------------------------------------------------------
    // CASE 2: Sensor type unchanged → DO NOT re-init
    // ---------------------------------------------------------------------
    if (old_type == new_type && slots[ch].handle != NULL)
    {
        ESP_LOGI(TAG, "Bus %u channel %u: type unchanged (%s), skipping re-init", (bus_handle == i2c_bus0_handle ? 0 : 1), ch, type_str);
        return ESP_OK;
    }

    // ---------------------------------------------------------------------
    // CASE 3: Type changed → re-init required
    // ---------------------------------------------------------------------
    ESP_LOGI(TAG, "Bus %u channel %u: type changed (%d → %d), re-initializing", (bus_handle == i2c_bus0_handle ? 0 : 1), ch, old_type, new_type);

    // Remove old device if present
    if (slots[ch].handle != NULL)
    {
        i2c_master_bus_rm_device(slots[ch].handle);
        slots[ch].handle = NULL;
    }

    // Select TCA channel
    esp_err_t err = i2c_tca9548_set_channel(tca_handle, ch);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set channel %u: %s",
                 ch, esp_err_to_name(err));
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(3));

    // Init new sensor
    i2c_master_dev_handle_t h = NULL;

    switch (new_type)
    {
    case SENSOR_AHT20:
        err = i2c_aht20_init(bus_handle, &h);
        break;
    case SENSOR_DHT20:
        err = i2c_dht20_init(bus_handle, &h);
        break;
    case SENSOR_SCD41:
        err = i2c_init_scd41(bus_handle, &h);
        break;
    default:
        return ESP_ERR_INVALID_ARG;
    }

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize %s on channel %u: %s", type_str, ch, esp_err_to_name(err));
        return err;
    }

    slots[ch].handle = h;
    slots[ch].type = new_type;

    ESP_LOGI(TAG, "%s initialized on channel %u", type_str, ch);
    return ESP_OK;
}

// -----------------------------------------------------------------------------
// Public: init_sensors_on_mux_channels
// -----------------------------------------------------------------------------
esp_err_t init_sensors_on_mux_channels(void)
{
    esp_err_t err;

    // --- Bus0 ---
    for (uint8_t ch = 0; ch < CHANNELS_PER_BUS; ch++)
    {
        const char *type_str = bus0_sensor_settings[ch].type;
        // ESP_LOGI(TAG, "Configuring bus 0 channel %u as '%s'", ch, type_str);
        err = configure_bus_channel(bus0_sensors, i2c_bus0_handle, tca9548_bus0_handle, ch, type_str);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "Bus 0 channel %u configuration failed: %s", ch, esp_err_to_name(err));
        }
    }

    // --- Bus1 ---
    for (uint8_t ch = 0; ch < CHANNELS_PER_BUS; ch++)
    {
        const char *type_str = bus1_sensor_settings[ch].type;
        // ESP_LOGI(TAG, "Configuring bus 1 channel %u as '%s'", ch, type_str);
        err = configure_bus_channel(bus1_sensors, i2c_bus1_handle, tca9548_bus1_handle, ch, type_str);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "Bus 1 channel %u configuration failed: %s", ch, esp_err_to_name(err));
        }
    }

    return ESP_OK;
}

// -----------------------------------------------------------------------------
// Reading sensors into queue (using per-channel slots)
// -----------------------------------------------------------------------------
esp_err_t read_sensors_into_queue(void)
{
    float snapshot[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS] = {0};

    for (uint8_t bus = 0; bus < MAX_I2C_BUSES; bus++)
    {
        sensor_slot_t *slots = (bus == 0) ? bus0_sensors : bus1_sensors;

        i2c_master_dev_handle_t tca = (bus == 0) ? tca9548_bus0_handle : tca9548_bus1_handle;

        for (uint8_t ch = 0; ch < CHANNELS_PER_BUS; ch++)
        {
            sensor_slot_t *slot = &slots[ch];

            // Default: invalid
            snapshot[bus][ch][IDX_VALID] = 0.0f;

            if (slot->type == SENSOR_NONE || slot->handle == NULL)
                continue;

            // Select channel
            esp_err_t err = i2c_tca9548_set_channel(tca, ch);
            if (err != ESP_OK)
                continue;

            // Allow mux to settle
            vTaskDelay(pdMS_TO_TICKS(3));

            float temp = 0, rh = 0;
            uint16_t co2 = 0;

            bool ok = false;

            switch (slot->type)
            {
            case SENSOR_AHT20:
                ok = (aht20_read(slot->handle, &temp, &rh) == ESP_OK);
                break;

            case SENSOR_DHT20:
                ok = (dht20_read(slot->handle, &temp, &rh) == ESP_OK);
                break;

            case SENSOR_SCD41:
                ok = (scd41_read(slot->handle, &co2, &temp, &rh) == ESP_OK);
                break;

            default:
                ok = false;
                break;
            }

            if (!ok)
                continue;

            // Write snapshot only on success
            snapshot[bus][ch][IDX_TEMP] = temp;
            snapshot[bus][ch][IDX_HUMIDITY] = rh;
            snapshot[bus][ch][IDX_CO2] = (float)co2;
            snapshot[bus][ch][IDX_TIMESTAMP] = (float)esp_timer_get_time();
            snapshot[bus][ch][IDX_VALID] = 1.0f;
        }
    }

    xQueueOverwrite(sensor_queue, snapshot);
    return ESP_OK;
}

// -----------------------------------------------------------------------------
// Averaging logic (unchanged except for using same snapshot layout)
// -----------------------------------------------------------------------------
void fifoPush(int bus, int slot, int measurement, float value, int pos);

void sensor_data_average(void)
{
    float sensor_data[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS];
    float sensor_avg_data[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS] = {0};

    if (xQueuePeek(sensor_queue, &sensor_data, 0) != pdTRUE)
    {
        ESP_LOGW("AVG", "sensor_data_average: sensor_queue empty");
        return;
    }

    int pos = 0;

    for (int bus = 0; bus < MAX_I2C_BUSES; bus++)
    {
        for (int slot = 0; slot < CHANNELS_PER_BUS; slot++)
        {
            for (int m = 0; m < 3; m++)
            {
                fifoPush(bus, slot, m, sensor_data[bus][slot][m], pos);
                sensor_avg_data[bus][slot][m] = avg[pos];
                pos++;
            }

            sensor_avg_data[bus][slot][3] = sensor_data[bus][slot][3];
            sensor_avg_data[bus][slot][4] = sensor_data[bus][slot][4];
        }
    }

    xQueueOverwrite(sensor_avg_queue, sensor_avg_data);
    // ESP_LOGI("AVG", "sensor_data_average: wrote avg data to queue");
}

void fifoPush(int bus, int slot, int measurement, float value, int pos)
{
    if (fifoSize[pos] < MAX_FIFO_SIZE)
    {
        sensor_fifo[bus][slot][measurement][(fifoHead[pos] + fifoSize[pos]) % MAX_FIFO_SIZE] = value;

        sum[pos] += value;
        fifoSize[pos]++;
    }
    else
    {
        sum[pos] -= sensor_fifo[bus][slot][measurement][fifoHead[pos]];
        sensor_fifo[bus][slot][measurement][fifoHead[pos]] = value;
        sum[pos] += value;

        fifoHead[pos] = (fifoHead[pos] + 1) % MAX_FIFO_SIZE;
    }

    avg[pos] = sum[pos] / fifoSize[pos];
}