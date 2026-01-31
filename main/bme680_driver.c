#include "bme680_driver.h"

static const char *NVS_NAMESPACE = "bsec";
static const char *NVS_KEY_STATE = "bsec_state";

static const char *TAG = "bme680_driver";
struct bme68x_dev bme68x_g = {0};

/* Atomic counters using C11 atomics */
static atomic_int bme680_profile_success = 0;
static atomic_int bme680_profile_fail = 0;

typedef struct
{
    i2c_master_bus_handle_t bus;        /* optional, keep if needed */
    i2c_master_dev_handle_t sensor_dev; /* BME680 device handle */
    i2c_master_dev_handle_t mux_dev;    /* TCA9548A device handle (must be dev handle) */
    int mux_channel;                    /* 0..7 or -1 if no mux */
} profile_arg_t;

/* Helper: deselect all channels on TCA9548A by writing mask 0 */
static esp_err_t tca9548_deselect_all_dev(i2c_master_dev_handle_t mux_dev)
{
    if (!mux_dev)
        return ESP_ERR_INVALID_ARG;
    uint8_t mask = 0x00;
    return i2c_master_transmit(mux_dev, &mask, 1, 1000);
}

/* Platform callbacks using your i2c_master API */
static int8_t platform_read(uint8_t reg_addr, uint8_t *data, uint32_t len, void *intf_ptr)
{
    i2c_master_dev_handle_t h = (i2c_master_dev_handle_t)intf_ptr;
    if (!h)
        return BME68X_E_COM_FAIL;
    if (i2c_master_transmit(h, &reg_addr, 1, pdMS_TO_TICKS(100)) != ESP_OK)
        return BME68X_E_COM_FAIL;
    return (i2c_master_receive(h, data, len, pdMS_TO_TICKS(200)) == ESP_OK) ? BME68X_OK : BME68X_E_COM_FAIL;
}

static int8_t platform_write(uint8_t reg_addr, const uint8_t *data, uint32_t len, void *intf_ptr)
{
    i2c_master_dev_handle_t h = (i2c_master_dev_handle_t)intf_ptr;
    if (!h)
        return BME68X_E_COM_FAIL;
    uint8_t buf[256];
    if (len + 1 > sizeof(buf))
        return BME68X_E_COM_FAIL;
    buf[0] = reg_addr;
    memcpy(buf + 1, data, len);
    return (i2c_master_transmit(h, buf, len + 1, pdMS_TO_TICKS(200)) == ESP_OK) ? BME68X_OK : BME68X_E_COM_FAIL;
}

static void platform_delay_us(uint32_t us, void *intf_ptr)
{
    (void)intf_ptr;
    if (us >= 1000)
        vTaskDelay(pdMS_TO_TICKS(us / 1000));
    else
        esp_rom_delay_us(us);
}

/* Initialize the BME680 driver for a device handle (selected mux channel) */
esp_err_t bme680_driver_init(i2c_master_dev_handle_t dev_handle)
{
    memset(&bme68x_g, 0, sizeof(bme68x_g));
    bme68x_g.intf = BME68X_I2C_INTF;
    bme68x_g.intf_ptr = dev_handle;
    bme68x_g.read = platform_read;
    bme68x_g.write = platform_write;
    bme68x_g.delay_us = platform_delay_us;
    bme68x_g.amb_temp = 25;

    int8_t rs = bme68x_init(&bme68x_g);
    if (rs != BME68X_OK)
    {
        LOGE(TAG, "bme68x_init failed: %d", rs);
        return ESP_FAIL;
    }
    LOGI(TAG, "bme68x_init OK");
    return ESP_OK;
}

/* Forced-mode read wrapper */
esp_err_t bme680_forced_read(float *temperature, float *humidity, float *pressure_hpa, float *gas_resistance)
{
    if (!temperature || !humidity || !pressure_hpa || !gas_resistance)
        return ESP_ERR_INVALID_ARG;

    struct bme68x_conf conf = {0};
    struct bme68x_heatr_conf heatr = {0};

    conf.os_hum = BME68X_OS_2X;
    conf.os_pres = BME68X_OS_4X;
    conf.os_temp = BME68X_OS_8X;
    conf.filter = BME68X_FILTER_OFF;
    bme68x_set_conf(&conf, &bme68x_g);

    heatr.enable = BME68X_ENABLE;
    heatr.heatr_temp = 320;
    heatr.heatr_dur = 150;
    bme68x_set_heatr_conf(BME68X_FORCED_MODE, &heatr, &bme68x_g);

    bme68x_set_op_mode(BME68X_FORCED_MODE, &bme68x_g);

    uint32_t dur_us = bme68x_get_meas_dur(BME68X_FORCED_MODE, &conf, &bme68x_g);
    platform_delay_us(dur_us, NULL);

    struct bme68x_data data;
    uint8_t n_fields = 0;
    int8_t rs = bme68x_get_data(BME68X_FORCED_MODE, &data, &n_fields, &bme68x_g);
    if (rs != BME68X_OK || n_fields == 0)
    {
        LOGW(TAG, "bme68x_get_data failed: %d n=%u", rs, n_fields);
        return ESP_FAIL;
    }

    *temperature = data.temperature;
    *humidity = data.humidity;
    *pressure_hpa = data.pressure / 100.0f;
    *gas_resistance = data.gas_resistance;
    return ESP_OK;
}

/* call after bme680_driver_init succeeded */
void bme680_run_profile_sequence(void)
{
    struct bme68x_conf conf = {0};
    struct bme68x_heatr_conf heatr = {0};
    struct bme68x_data data = {0};
    uint8_t n_fields = 0;
    int8_t rs;

    // common oversampling/filter for both profiles
    conf.os_hum = BME68X_OS_2X;
    conf.os_pres = BME68X_OS_4X;
    conf.os_temp = BME68X_OS_8X;
    conf.filter = BME68X_FILTER_OFF;

    rs = bme68x_set_conf(&conf, &bme68x_g);
    if (rs != BME68X_OK)
    {
        LOGW(TAG, "set_conf failed: %d", rs);
        return;
    }

    // -------------------------
    // Profile A: high temp, short duration
    // -------------------------
    heatr.enable = BME68X_ENABLE;
    heatr.heatr_temp = 320; // °C
    heatr.heatr_dur = 150;  // ms

    rs = bme68x_set_heatr_conf(BME68X_FORCED_MODE, &heatr, &bme68x_g);
    LOGI(TAG, "Profile A: set_heatr_conf rs=%d temp=%u dur=%u", rs, heatr.heatr_temp, heatr.heatr_dur);
    if (rs != BME68X_OK)
    {
        LOGW(TAG, "Profile A: set_heatr_conf error %d", rs);
        // continue to attempt measurement, but expect failure */
    }

    rs = bme68x_set_op_mode(BME68X_FORCED_MODE, &bme68x_g);
    LOGI(TAG, "Profile A: set_op_mode rs=%d", rs);
    if (rs != BME68X_OK)
    {
        LOGW(TAG, "Profile A: set_op_mode error %d", rs);
    }

    uint32_t durA_us = bme68x_get_meas_dur(BME68X_FORCED_MODE, &conf, &bme68x_g);
    LOGI(TAG, "Profile A: meas_dur_us=%lu (+200ms margin)", durA_us);
    platform_delay_us(durA_us + 200000, NULL); /* +200 ms margin */

    /* use retry helper to tolerate timing */
    n_fields = 0;
    rs = bme68x_get_data_retry(BME68X_FORCED_MODE, &data, &n_fields, 6);
    if (rs == BME68X_OK && n_fields > 0)
    {
        LOGI(TAG, "A T=%.2f H=%.2f P=%.2fhPa Gas=%.0fΩ idac=0x%02X status=0x%02X",
             data.temperature, data.humidity, data.pressure / 100.0f,
             (float)data.gas_resistance, data.idac, data.status);
    }
    else
    {
        LOGW(TAG, "Profile A read failed r=%d n=%u idac=0x%02X status=0x%02X", rs, (unsigned)n_fields, data.idac, data.status);
    }

    // allow sensor and bus to settle before next profile
    vTaskDelay(pdMS_TO_TICKS(400)); // increased gap

    // -------------------------
    // Profile B: lower temp, longer duration
    // -------------------------
    heatr.heatr_temp = 200; // °C
    heatr.heatr_dur = 300;  // ms

    rs = bme68x_set_heatr_conf(BME68X_FORCED_MODE, &heatr, &bme68x_g);
    LOGI(TAG, "Profile B: set_heatr_conf rs=%d temp=%u dur=%u", rs, heatr.heatr_temp, heatr.heatr_dur);
    if (rs != BME68X_OK)
    {
        LOGW(TAG, "Profile B: set_heatr_conf error %d", rs);
    }

    rs = bme68x_set_op_mode(BME68X_FORCED_MODE, &bme68x_g);
    LOGI(TAG, "Profile B: set_op_mode rs=%d", rs);
    if (rs != BME68X_OK)
    {
        LOGW(TAG, "Profile B: set_op_mode error %d", rs);
    }

    uint32_t durB_us = bme68x_get_meas_dur(BME68X_FORCED_MODE, &conf, &bme68x_g);
    LOGI(TAG, "Profile B: meas_dur_us=%lu (+300ms margin)", durB_us);
    // slightly larger margin for the longer profile
    platform_delay_us(durB_us + 300000, NULL); // +300 ms margin

    // read with retries and longer backoff inside helper
    n_fields = 0;
    rs = bme68x_get_data_retry(BME68X_FORCED_MODE, &data, &n_fields, 8);
    if (rs == BME68X_OK && n_fields > 0)
    {
        LOGI(TAG, "B T=%.2f H=%.2f P=%.2fhPa Gas=%.0fΩ idac=0x%02X status=0x%02X",
             data.temperature, data.humidity, data.pressure / 100.0f,
             (float)data.gas_resistance, data.idac, data.status);
    }
    else
    {
        LOGW(TAG, "Profile B read failed r=%d n=%u idac=0x%02X status=0x%02X",
             rs, (unsigned)n_fields, data.idac, data.status);
    }

    // Restore predicatble sensor state
    // struct bme68x_conf normal_conf = {0};
    // struct bme68x_heatr_conf heatr_off = {0};
    // normal_conf.os_hum = BME68X_OS_2X;
    // normal_conf.os_pres = BME68X_OS_4X;
    // normal_conf.os_temp = BME68X_OS_8X;
    // normal_conf.filter = BME68X_FILTER_OFF;
    // bme68x_set_conf(&normal_conf, &bme68x_g);

    /* disable heater */
    // heatr_off.enable = BME68X_DISABLE;
    // bme68x_set_heatr_conf(BME68X_FORCED_MODE, &heatr_off, &bme68x_g);

    /* set op mode to sleep or forced as your normal read expects */
    // bme68x_set_op_mode(BME68X_SLEEP_MODE, &bme68x_g);
}

/*void bme680_run_profile_sequence(void)
{
    struct bme68x_conf conf = {0};
    struct bme68x_heatr_conf heatr = {0};
    struct bme68x_data data;
    uint8_t n_fields = 0;
    int8_t rs;

    // common oversampling/filter for both profiles
    conf.os_hum = BME68X_OS_2X;
    conf.os_pres = BME68X_OS_4X;
    conf.os_temp = BME68X_OS_8X;
    conf.filter = BME68X_FILTER_OFF;
    bme68x_set_conf(&conf, &bme68x_g);

    // Profile A: high temp, short duration (probe more reactive VOCs)
    heatr.enable = BME68X_ENABLE;
    heatr.heatr_temp = 320; // degrees C
    heatr.heatr_dur = 150;  // ms
    bme68x_set_heatr_conf(BME68X_FORCED_MODE, &heatr, &bme68x_g);
    bme68x_set_op_mode(BME68X_FORCED_MODE, &bme68x_g);
    uint32_t durA = bme68x_get_meas_dur(BME68X_FORCED_MODE, &conf, &bme68x_g);
    platform_delay_us(durA + 200000, NULL); // +200ms margin

    rs = bme68x_get_data(BME68X_FORCED_MODE, &data, &n_fields, &bme68x_g);
    if (rs == BME68X_OK && n_fields > 0)
    {
        LOGI(TAG, "A T=%.2f H=%.2f P=%.2fhPa Gas=%.0fΩ idac=0x%02X status=0x%02X", data.temperature, data.humidity, data.pressure / 100.0f, (float)data.gas_resistance, data.idac, data.status);
    }
    else
    {
        LOGW(TAG, "Profile A read failed r=%d n=%u", rs, n_fields);
    }

    vTaskDelay(pdMS_TO_TICKS(200)); // small gap

    // Profile B: lower temp, longer duration (probe other VOCs)
    heatr.heatr_temp = 200;
    heatr.heatr_dur = 300;
    bme68x_set_heatr_conf(BME68X_FORCED_MODE, &heatr, &bme68x_g);
    bme68x_set_op_mode(BME68X_FORCED_MODE, &bme68x_g);
    uint32_t durB = bme68x_get_meas_dur(BME68X_FORCED_MODE, &conf, &bme68x_g);
    platform_delay_us(durB + 200000, NULL);

    rs = bme68x_get_data(BME68X_FORCED_MODE, &data, &n_fields, &bme68x_g);
    if (rs == BME68X_OK && n_fields > 0)
    {
        LOGI(TAG, "B T=%.2f H=%.2f P=%.2fhPa Gas=%.0fΩ idac=0x%02X status=0x%02X", data.temperature, data.humidity, data.pressure / 100.0f, (float)data.gas_resistance, data.idac, data.status);
    }
    else
    {
        LOGW(TAG, "Profile B read failed r=%d n=%u", rs, n_fields);
    }
}*/

int8_t bme68x_get_data_retry(uint8_t op_mode, struct bme68x_data *data, uint8_t *n_fields, int retries)
{
    for (int i = 0; i <= retries; ++i)
    {
        int8_t r = bme68x_get_data(op_mode, data, n_fields, &bme68x_g);
        if (r == BME68X_OK && *n_fields > 0)
            return r;
        if (r == BME68X_W_NO_NEW_DATA)
            vTaskDelay(pdMS_TO_TICKS(300));
        else
            return r;
    }
    return BME68X_W_NO_NEW_DATA;
}

static void bme680_profile_task(void *arg)
{
    profile_arg_t *parg = (profile_arg_t *)arg;
    if (!parg)
    {
        LOGW(TAG, "profile_task: null arg");
        vTaskDelete(NULL);
        return;
    }

    /* Ensure mutex exists */
    if (!i2c_bus_mutex)
    {
        i2c_bus_mutex = xSemaphoreCreateMutex();
        configASSERT(i2c_bus_mutex);
    }

    /* Acquire I2C mutex to protect mux selection and transactions */
    if (xSemaphoreTake(i2c_bus_mutex, pdMS_TO_TICKS(500)) != pdTRUE)
    {
        LOGW(TAG, "profile_task: failed to take i2c mutex");
        atomic_fetch_add(&bme680_profile_fail, 1);
        vPortFree(parg);
        vTaskDelete(NULL);
        return;
    }

    /* Select mux channel if used */
    if (parg->mux_channel >= 0 && parg->mux_dev)
    {
        esp_err_t r = i2c_tca9548_set_channel(parg->mux_dev, (uint8_t)parg->mux_channel);
        if (r != ESP_OK)
        {
            LOGW(TAG, "profile_task: mux select failed: %s", esp_err_to_name(r));
            atomic_fetch_add(&bme680_profile_fail, 1);
            xSemaphoreGive(i2c_bus_mutex);
            vPortFree(parg);
            vTaskDelete(NULL);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(3)); /* settle */
    }

    /* Run the diagnostic sequence (driver-level function) */
    bme680_run_profile_sequence();

    /* Mark success */
    atomic_fetch_add(&bme680_profile_success, 1);

    /* Deselect mux if needed */
    if (parg->mux_channel >= 0 && parg->mux_dev)
    {
        tca9548_deselect_all_dev(parg->mux_dev);
    }

    xSemaphoreGive(i2c_bus_mutex);
    vPortFree(parg);
    vTaskDelete(NULL);
}

/* Schedule the profile task once (call after first successful BME680 read) */
void schedule_bme680_profile_once(i2c_master_bus_handle_t bus, i2c_master_dev_handle_t sensor_dev, i2c_master_dev_handle_t mux_dev, int mux_channel)
{
#ifdef BME680_PROFILE_SEQUENCE
    static bool profile_scheduled = false;
    if (profile_scheduled)
        return;
    profile_scheduled = true;

    /* Ensure mutex exists */
    if (!i2c_bus_mutex)
    {
        i2c_bus_mutex = xSemaphoreCreateMutex();
        configASSERT(i2c_bus_mutex);
    }

    profile_arg_t *arg = pvPortMalloc(sizeof(profile_arg_t));
    if (!arg)
    {
        LOGW(TAG, "schedule: malloc failed");
        return;
    }
    arg->bus = bus;
    arg->sensor_dev = sensor_dev;
    arg->mux_dev = mux_dev;
    arg->mux_channel = mux_channel;

    BaseType_t r = xTaskCreate(bme680_profile_task, "bme680_prof", 4096 / sizeof(StackType_t), /* 4 KB stack */ arg, tskIDLE_PRIORITY + 1, NULL);
    if (r != pdPASS)
    {
        LOGW(TAG, "schedule: task create failed");
        vPortFree(arg);
    }
#endif
}

/* Telemetry helpers */
int get_bme680_profile_success_count(void)
{
    return atomic_load(&bme680_profile_success);
}

int get_bme680_profile_fail_count(void)
{
    return atomic_load(&bme680_profile_fail);
}

// Save BSEC state blob to NVS
esp_err_t bsec_state_save_to_nvs(const uint8_t *state_blob, size_t state_len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK)
        return err;
    err = nvs_set_blob(h, NVS_KEY_STATE, state_blob, state_len);
    if (err == ESP_OK)
        err = nvs_commit(h);
    nvs_close(h);
    if (err == ESP_OK)
        ESP_LOGI(TAG, "BSEC state saved (%u bytes)", (unsigned)state_len);
    return err;
}

// Restore BSEC state blob from NVS into provided buffer. If buffer is NULL, function returns required size in *state_len.
esp_err_t bsec_state_restore_from_nvs(uint8_t *state_blob, size_t *state_len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK)
        return err;
    size_t required = 0;
    err = nvs_get_blob(h, NVS_KEY_STATE, NULL, &required);
    if (err == ESP_OK && state_blob && *state_len >= required)
    {
        err = nvs_get_blob(h, NVS_KEY_STATE, state_blob, &required);
        if (err == ESP_OK)
            ESP_LOGI(TAG, "BSEC state restored (%u bytes)", (unsigned)required);
    }
    else if (err == ESP_OK)
    {
        *state_len = required;       // caller can allocate and call again
        err = ESP_ERR_NVS_NOT_FOUND; // indicate caller must allocate
    }
    nvs_close(h);
    return err;
}