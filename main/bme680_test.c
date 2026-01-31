/* main/bme680_profiles_test.c
 *
 * Test task adapted to your driver implementation (uses bme680_driver.h).
 * Drop into main/ and add to CMakeLists.txt.
 */

#include "bme680_test.h"

static const char *TAG = "bme680_test";

typedef struct
{
    i2c_master_bus_handle_t bus;        /* optional */
    i2c_master_dev_handle_t sensor_dev; /* BME680 device handle (dev handle) */
    i2c_master_dev_handle_t mux_dev;    /* TCA9548A device handle (or NULL) */
    int mux_channel;                    /* 0..7 or -1 if no mux */
    SemaphoreHandle_t i2c_mutex;        /* optional global I2C mutex (can be NULL) */
} bme680_test_arg_t;

/* Helper: select mux channel (no-op if mux_dev==NULL or channel<0) */
static esp_err_t select_mux_channel(i2c_master_dev_handle_t mux_dev, int channel)
{
    if (!mux_dev || channel < 0)
        return ESP_OK;
    return i2c_tca9548_set_channel(mux_dev, (uint8_t)channel);
}

/* Helper: deselect all channels */
static esp_err_t deselect_mux(i2c_master_dev_handle_t mux_dev)
{
    if (!mux_dev)
        return ESP_OK;
    uint8_t mask = 0x00;
    return i2c_master_transmit(mux_dev, &mask, 1, pdMS_TO_TICKS(1000));
}

/* Run one profile (heatr_temp in °C, heatr_dur in ms), return pass/fail */
static bool run_profile(i2c_master_dev_handle_t sensor_dev, int heatr_temp, int heatr_dur_ms)
{
    struct bme68x_conf conf = {0};
    struct bme68x_heatr_conf heatr = {0};
    struct bme68x_data data = {0};
    uint8_t n_fields = 0;
    int8_t rs;

    /* common oversampling/filter (match your driver) */
    conf.os_hum = BME68X_OS_2X;
    conf.os_pres = BME68X_OS_4X;
    conf.os_temp = BME68X_OS_8X;
    conf.filter = BME68X_FILTER_OFF;
    bme68x_set_conf(&conf, &bme68x_g);

    /* program heater */
    heatr.enable = BME68X_ENABLE;
    heatr.heatr_temp = (uint16_t)heatr_temp;  /* degrees C */
    heatr.heatr_dur = (uint16_t)heatr_dur_ms; /* ms */
    bme68x_set_heatr_conf(BME68X_FORCED_MODE, &heatr, &bme68x_g);

    /* start forced measurement */
    bme68x_set_op_mode(BME68X_FORCED_MODE, &bme68x_g);

    /* compute expected duration (microseconds) and wait + margin */
    uint32_t dur_us = bme68x_get_meas_dur(BME68X_FORCED_MODE, &conf, &bme68x_g);
    /* convert to ms and add margin */
    uint32_t wait_ms = (dur_us + 999) / 1000; /* ceil to ms */
    wait_ms += 200;                           /* margin used in your driver */
    vTaskDelay(pdMS_TO_TICKS(wait_ms));

    /* read using retry helper (your driver provides bme68x_get_data_retry) */
    rs = bme68x_get_data_retry(BME68X_FORCED_MODE, &data, &n_fields, 5);
    if (rs != BME68X_OK)
    {
        ESP_LOGW(TAG, "get_data_retry returned %d n_fields=%u", rs, (unsigned)n_fields);
    }

    ESP_LOGI(TAG, "Profile test: T=%.2f H=%.2f P=%.2fhPa Gas=%.0fΩ idac=0x%02X status=0x%02X n_fields=%u",
             data.temperature, data.humidity, data.pressure / 100.0f,
             (float)data.gas_resistance, data.idac, data.status, (unsigned)n_fields);

    bool pass = (data.idac != 0) && (n_fields > 0);
    if (pass)
    {
        ESP_LOGI(TAG, "Profile (temp=%d dur=%dms) PASS", heatr_temp, heatr_dur_ms);
    }
    else
    {
        ESP_LOGW(TAG, "Profile (temp=%d dur=%dms) FAIL", heatr_temp, heatr_dur_ms);
    }
    return pass;
}

/* Test task: runs Profile A and Profile B and prints summary */
static void bme680_profiles_test_task(void *arg)
{
    bme680_test_arg_t *t = (bme680_test_arg_t *)arg;
    if (!t)
    {
        vTaskDelete(NULL);
        return;
    }

    /* ensure mutex exists */
    if (!t->i2c_mutex)
    {
        t->i2c_mutex = xSemaphoreCreateMutex();
        configASSERT(t->i2c_mutex);
    }

    /* take mutex for mux selection + init + measurements */
    // if (xSemaphoreTake(t->i2c_mutex, pdMS_TO_TICKS(500)) != pdTRUE)
    if (xSemaphoreTake(t->i2c_mutex, portMAX_DELAY))
    {
        ESP_LOGW(TAG, "test_task: failed to take i2c mutex");
        vPortFree(t);
        vTaskDelete(NULL);
        return;
    }

    /* select mux channel if used */
    if (t->mux_channel >= 0 && t->mux_dev)
    {
        if (select_mux_channel(t->mux_dev, t->mux_channel) != ESP_OK)
        {
            ESP_LOGW(TAG, "test_task: mux select failed");
            xSemaphoreGive(t->i2c_mutex);
            vPortFree(t);
            vTaskDelete(NULL);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(3)); /* settle */
    }

    /* initialize driver for this device handle (sets bme68x_g.intf_ptr etc) */
    if (bme680_driver_init(t->sensor_dev) != ESP_OK)
    {
        ESP_LOGW(TAG, "bme680_driver_init failed for device");
        if (t->mux_channel >= 0 && t->mux_dev)
            deselect_mux(t->mux_dev);
        xSemaphoreGive(t->i2c_mutex);
        vPortFree(t);
        vTaskDelete(NULL);
        return;
    }

    /* Run Profile A (matches your bme680_run_profile_sequence): high temp, short dur */
    bool passA = run_profile(t->sensor_dev, 320, 150);

    vTaskDelay(pdMS_TO_TICKS(200)); /* small gap */

    /* Run Profile B: lower temp, longer dur */
    bool passB = run_profile(t->sensor_dev, 200, 300);

    /* deselect mux if used */
    if (t->mux_channel >= 0 && t->mux_dev)
    {
        deselect_mux(t->mux_dev);
    }

    xSemaphoreGive(t->i2c_mutex);

    ESP_LOGI(TAG, "BME680 profile test summary: ProfileA=%s ProfileB=%s",
             passA ? "PASS" : "FAIL",
             passB ? "PASS" : "FAIL");

    vPortFree(t);
    vTaskDelete(NULL);
}

/* Public starter: call this after sensor device handle and mux (if any) are available */
void start_bme680_profiles_test(i2c_master_bus_handle_t bus,
                                i2c_master_dev_handle_t sensor_dev,
                                i2c_master_dev_handle_t mux_dev,
                                int mux_channel,
                                SemaphoreHandle_t i2c_mutex)
{
    bme680_test_arg_t *arg = pvPortMalloc(sizeof(bme680_test_arg_t));
    if (!arg)
    {
        ESP_LOGW(TAG, "start_test: malloc failed");
        return;
    }
    arg->bus = bus;
    arg->sensor_dev = sensor_dev;
    arg->mux_dev = mux_dev;
    arg->mux_channel = mux_channel;
    arg->i2c_mutex = i2c_mutex;

    BaseType_t r = xTaskCreate(bme680_profiles_test_task, "bme680_prof_test", 4096 / sizeof(StackType_t), arg, tskIDLE_PRIORITY + 1, NULL);
    if (r != pdPASS)
    {
        ESP_LOGW(TAG, "start_test: task create failed");
        vPortFree(arg);
    }
}
