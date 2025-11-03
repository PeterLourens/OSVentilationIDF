#include "i2c_ds3231.h"

static const char *TAG = "DS3231";
static const char *NVS_NS = "ds3231_diag";

esp_err_t i2c_ds3231_init(i2c_master_bus_handle_t bus_handle, i2c_master_dev_handle_t *dev_handle)
{
    i2c_device_config_t ds3231_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = DS3231_I2C_ADDR, // 0x68
        .scl_speed_hz = 100000,
    };

    esp_err_t err = i2c_master_bus_add_device(bus_handle, &ds3231_cfg, dev_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to add DS3231 device: %s", esp_err_to_name(err));
        return err;
    }

    // Check if device responds by reading status register (0x0F)
    uint8_t reg = 0x0F;
    uint8_t status = 0;

    err = i2c_master_transmit_receive(*dev_handle, &reg, 1, &status, 1, pdMS_TO_TICKS(100));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read status register: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "DS3231 initialized successfully, status=0x%02X", status);
    return ESP_OK;
}

esp_err_t i2c_ds3231_read_time(i2c_master_dev_handle_t dev_handle, struct tm *timeinfo)
{
    if (!dev_handle || !timeinfo)
    {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t reg = 0x00; // Start from seconds register
    uint8_t data[7];    // Seconds through Year

    // Send register address
    esp_err_t err = i2c_master_transmit(dev_handle, &reg, 1, pdMS_TO_TICKS(100));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to send register address: %s", esp_err_to_name(err));
        return err;
    }

    // Read 7 bytes of time data
    err = i2c_master_receive(dev_handle, data, sizeof(data), pdMS_TO_TICKS(100));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read time data: %s", esp_err_to_name(err));
        return err;
    }

    // Convert BCD to decimal
    timeinfo->tm_sec = ((data[0] >> 4) * 10) + (data[0] & 0x0F);
    timeinfo->tm_min = ((data[1] >> 4) * 10) + (data[1] & 0x0F);
    timeinfo->tm_hour = ((data[2] >> 4) * 10) + (data[2] & 0x0F); // Assumes 24h mode
    timeinfo->tm_wday = (data[3] & 0x07) - 1;                     // Convert 1-7 to 0-6
    timeinfo->tm_mday = ((data[4] >> 4) * 10) + (data[4] & 0x0F);
    timeinfo->tm_mon = (((data[5] >> 4) * 10) + (data[5] & 0x0F)) - 1;   // Convert 1-12 to 0-11
    timeinfo->tm_year = ((data[6] >> 4) * 10) + (data[6] & 0x0F) + 2000; // Convert to years since 1900

    ESP_LOGD(TAG, "Time read: %02d:%02d:%02d %02d/%02d/%04d",
             timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec,
             timeinfo->tm_mday, timeinfo->tm_mon + 1, timeinfo->tm_year + 1900);

    return ESP_OK;
}

void init_system_time_from_ds3231(i2c_master_dev_handle_t dev_handle)
{
    struct tm rtc_time;
    if (i2c_ds3231_read_time(dev_handle, &rtc_time) == ESP_OK)
    {
        time_t rtc_epoch = mktime(&rtc_time);
        struct timeval now = {
            .tv_sec = rtc_epoch,
            .tv_usec = 0,
        };
        settimeofday(&now, NULL);

        // Print both RTC and system time
        time_t sys_now;
        struct tm sys_time;
        time(&sys_now);
        localtime_r(&sys_now, &sys_time);

        ESP_LOGI(TAG, "RTC time read: %s", asctime(&rtc_time));
        ESP_LOGI(TAG, "System time set: %s", asctime(&sys_time));
    }
    else
    {
        ESP_LOGW(TAG, "Failed to read DS3231 at boot, system time starts at default (1970)");
    }
}

uint8_t bin2bcd(uint8_t val)
{
    return ((val / 10) << 4) | (val % 10);
}

esp_err_t i2c_ds3231_write_time(i2c_master_dev_handle_t dev_handle, const struct tm *timeinfo)
{
    if (!timeinfo)
        return ESP_ERR_INVALID_ARG;

    // DS3231 expects: seconds, minutes, hours, day-of-week, date, month, year
    uint8_t buffer[8];
    buffer[0] = DS3231_REG_TIME;
    buffer[1] = bin2bcd(timeinfo->tm_sec);
    buffer[2] = bin2bcd(timeinfo->tm_min);
    buffer[3] = bin2bcd(timeinfo->tm_hour);                              // 24-hour mode
    buffer[4] = bin2bcd(timeinfo->tm_wday == 0 ? 7 : timeinfo->tm_wday); // Sunday=7
    buffer[5] = bin2bcd(timeinfo->tm_mday);
    buffer[6] = bin2bcd(timeinfo->tm_mon + 1);    // tm_mon is 0–11
    buffer[7] = bin2bcd(timeinfo->tm_year % 100); // tm_year is years since 1900

    esp_err_t err = i2c_master_transmit(dev_handle, buffer, sizeof(buffer), pdMS_TO_TICKS(100));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to write time to DS3231: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "DS3231 time set: %04d-%02d-%02d %02d:%02d:%02d",
             timeinfo->tm_year + 1900,
             timeinfo->tm_mon + 1,
             timeinfo->tm_mday,
             timeinfo->tm_hour,
             timeinfo->tm_min,
             timeinfo->tm_sec);

    return ESP_OK;
}

void time_sync_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "System time synchronized");

    // Get current system time
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    // Update DS3231 here (replace dev_handle with your actual handle)
    extern i2c_master_dev_handle_t ds3231_bus0_handle;
    i2c_ds3231_write_time(ds3231_bus0_handle, &timeinfo);
}

// Function to check if Wi-Fi has an IP
bool wifi_has_ip(void)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == NULL)
    {
        ESP_LOGW(TAG, "Wi-Fi STA interface not found");
        return false;
    }

    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK)
    {
        return false;
    }
    return ip_info.ip.addr != 0;
}

// Event handler: called when Wi‑Fi station gets an IP
void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    ESP_LOGI(TAG, "Wi‑Fi connected, got IP — starting NTP sync");
    esp_err_t err = sync_system_and_ds3231_with_ntp(ds3231_bus0_handle);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "Initial DS3231 sync triggered after Wi‑Fi connect");
    }
    else
    {
        ESP_LOGW(TAG, "Sync attempt failed: %s", esp_err_to_name(err));
    }
}

// Background task for periodic re‑sync
void ds3231_sync_task(void *pvParameters)
{
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(3600 * 1000)); // wait 1 hour
        if (wifi_has_ip())
        {
            ESP_LOGI(TAG, "Periodic sync triggered");
            sync_system_and_ds3231_with_ntp(ds3231_bus0_handle);
        }
        else
        {
            ESP_LOGW(TAG, "Skipped periodic sync — no Wi‑Fi");
        }
    }
}

esp_err_t sync_system_and_ds3231_with_ntp(i2c_master_dev_handle_t dev_handle)
{
    const char *TAG = "NTP_SYNC";

    // --- Take mutex to safely read config ---
    if (xSemaphoreTake(time_config_mutex, pdMS_TO_TICKS(10)) != pdTRUE)
    {
        ESP_LOGE(TAG, "Failed to acquire time_config mutex");
        return ESP_ERR_TIMEOUT;
    }

    char ntp_server[LARGE_CONFIG_ITEM];
    char timezone[LARGE_CONFIG_ITEM];
    strncpy(ntp_server, time_config.ntp_server, sizeof(ntp_server));
    strncpy(timezone, time_config.timezone, sizeof(timezone));
    xSemaphoreGive(time_config_mutex);

    // --- Check Wi-Fi ---
    if (!wifi_has_ip())
    {
        ESP_LOGE(TAG, "No Wi-Fi connection, cannot sync");
        return ESP_ERR_INVALID_STATE;
    }

    // --- Configure timezone ---
    setenv("TZ", timezone, 1);
    tzset();

    // --- Configure SNTP ---
    esp_sntp_stop(); // reset if already running
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, ntp_server); // use configured server
    esp_sntp_set_time_sync_notification_cb(time_sync_cb);
    esp_sntp_init();

    ESP_LOGI(TAG, "SNTP started with server: %s, TZ: %s", ntp_server, timezone);

    return ESP_OK;
}

// --- Low-level helpers ---
esp_err_t ds3231_read_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(dev, &reg, 1, val, 1, pdMS_TO_TICKS(100));
}

esp_err_t ds3231_write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(dev, buf, 2, pdMS_TO_TICKS(100));
}

// --- Temperature ---
esp_err_t ds3231_read_temperature(i2c_master_dev_handle_t dev, float *temp_c)
{
    uint8_t reg = DS3231_REG_TEMP_MSB;
    uint8_t buf[2];
    esp_err_t err = i2c_master_transmit_receive(dev, &reg, 1, buf, 2, pdMS_TO_TICKS(100));
    if (err != ESP_OK)
        return err;

    int8_t msb = buf[0];       // signed
    uint8_t lsb = buf[1] >> 6; // upper 2 bits
    *temp_c = msb + (lsb * 0.25f);
    return ESP_OK;
}

// --- Status + health check ---
esp_err_t ds3231_check_status(i2c_master_dev_handle_t dev, char *buf, size_t bufsize)
{
    uint8_t status;
    esp_err_t err = ds3231_read_reg(dev, DS3231_REG_STATUS, &status);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read status: %s", esp_err_to_name(err));
        return err;
    }

    bool osf = status & 0x80;  // Oscillator Stop Flag
    bool busy = status & 0x04; // Busy flag (temp conversion)
    bool alarm1 = status & 0x01;
    bool alarm2 = status & 0x02;

    if (osf)
    {
        ESP_LOGW(TAG, "Oscillator Stop Flag set — RTC may have lost power!");
        ds3231_write_reg(dev, DS3231_REG_STATUS, status & ~0x80); // Clear OSF
    }
    // else
    //{
    //     ESP_LOGI(TAG, "Oscillator running normally");
    // }

    snprintf(buf, bufsize, "OSF=%d Busy=%d\nAl1=%d Al2=%d", osf, busy, alarm1, alarm2);

    // ESP_LOGI(TAG, "Status bits: OSF=%d, Busy=%d, Alarm1=%d, Alarm2=%d", osf, busy, alarm1, alarm2);

    return ESP_OK;
}

// --- Drift diagnostics ---
// Keep last sync time in static storage
static time_t last_check_epoch = 0;
static double last_drift_sec = 0.0;

time_t tm_to_epoch_utc(const struct tm *tm)
{
    int year = tm->tm_year + 1900;
    int month = tm->tm_mon + 1; /* 1..12 */
    int day = tm->tm_mday;
    int hour = tm->tm_hour;
    int min = tm->tm_min;
    int sec = tm->tm_sec;

    if (month <= 2)
    {
        year--;
        month += 12;
    }

    /* Algorithm from civil to days (Howard Hinnant) */
    int64_t era = year / 400;
    int64_t yoe = year - era * 400;                      // year of era
    int64_t doy = (153 * (month - 3) + 2) / 5 + day - 1; // day of year
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy; // day of era
    int64_t days = era * 146097 + doe - 719468;          // days since 1970-01-01

    int64_t secs = days * 86400 + (int64_t)hour * 3600 + (int64_t)min * 60 + sec;
    return (time_t)secs;
}

esp_err_t ds3231_drift_check(i2c_master_dev_handle_t dev)
{
    struct tm rtc_tm;
    if (i2c_ds3231_read_time(dev, &rtc_tm) != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read RTC time");
        return ESP_FAIL;
    }

    /* Convert RTC (assumed UTC) to epoch without using mktime/timezone */
    time_t rtc_epoch = tm_to_epoch_utc(&rtc_tm);

    /* System time (epoch seconds) */
    time_t sys_now;
    time(&sys_now);

    /* Drift in seconds (system - rtc). Use whole seconds because DS3231 has 1s resolution */
    double drift_sec = difftime(sys_now, rtc_epoch);

    ESP_LOGI(TAG,
             "RTC vs System drift: %.0f sec | RTC=%04d-%02d-%02d %02d:%02d:%02d UTC",
             drift_sec,
             rtc_tm.tm_year + 1900, rtc_tm.tm_mon + 1, rtc_tm.tm_mday,
             rtc_tm.tm_hour, rtc_tm.tm_min, rtc_tm.tm_sec);

    /* If we have a previous measurement, compute average drift rate in ppm */
    if (last_check_epoch != 0)
    {
        double elapsed_seconds = difftime(sys_now, last_check_epoch);
        if (elapsed_seconds > 0.0)
        {
            double drift_change = drift_sec - last_drift_sec;
            double ppm = (drift_change / elapsed_seconds) * 1e6;

            double elapsed_days = elapsed_seconds / 86400.0;
            ESP_LOGI(TAG, "Elapsed: %.2f days | Drift change: %.0f sec | Avg drift rate: %.2f ppm", elapsed_days, drift_change, ppm);
        }
        else
        {
            ESP_LOGW(TAG, "Elapsed time is zero or negative; cannot compute ppm");
        }
    }

    /* Update stored values */
    last_check_epoch = sys_now;
    last_drift_sec = drift_sec;

    return ESP_OK;
}

// --- Combined health check ---
void ds3231_health_monitor(i2c_master_dev_handle_t dev)
{
    float temp_c = 0;
    char ds3231_status[30];

    ds3231_check_status(dev, ds3231_status, sizeof(ds3231_status));

    if (ds3231_read_temperature(dev, &temp_c) == ESP_OK)
    {
        ESP_LOGI(TAG, "DS3231 internal temperature: %.2f °C", temp_c);
    }

    ds3231_drift_check(dev);
}

void save_ds3231_diagnostics(double drift_sec, float temp_c)
{
    nvs_handle_t handle;

    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGE("NVS", "Error opening NVS: %s", esp_err_to_name(err));
        return;
    }

    // Store drift as integer seconds
    nvs_set_i32(handle, "drift", (int32_t)drift_sec);

    // Store temperature as integer *100 (to keep decimals)
    nvs_set_i32(handle, "temp_c", (int32_t)(temp_c * 100));

    // Store current epoch time
    time_t now;
    time(&now);
    nvs_set_i64(handle, "last_check", (int64_t)now);

    nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI("NVS", "Diagnostics saved: drift=%.0f sec, temp=%.2f °C", drift_sec, temp_c);
}

void read_ds3231_diagnostics(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGW("NVS", "No diagnostics stored yet");
        return;
    }

    int32_t drift, temp100;
    int64_t last_check;

    if (nvs_get_i32(handle, "drift", &drift) == ESP_OK &&
        nvs_get_i32(handle, "temp_c", &temp100) == ESP_OK &&
        nvs_get_i64(handle, "last_check", &last_check) == ESP_OK)
    {

        struct tm tm;
        localtime_r((time_t *)&last_check, &tm);

        ESP_LOGI("NVS", "Last drift: %d sec", drift);
        ESP_LOGI("NVS", "Last temp: %.2f °C", temp100 / 100.0f);
        ESP_LOGI("NVS", "Last check: %s", asctime(&tm));
    }
    else
    {
        ESP_LOGW("NVS", "Diagnostics keys not found");
    }

    nvs_close(handle);
}