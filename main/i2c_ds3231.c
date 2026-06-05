/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

/**
 * @file i2c_ds3231.c
 * @brief DS3231 RTC device initialization, time synchronization, and diagnostics.
 */

#include "i2c_ds3231.h"

static const char *TAG = "i2c_DS3231";

/**
 * @brief Initialize the DS3231 on the specified bus.
 */
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
        LOGE(TAG, "Failed to add DS3231 device: %s", esp_err_to_name(err));
        return err;
    }

    // Check if device responds by reading status register (0x0F)
    uint8_t reg = 0x0F;
    uint8_t status = 0;

    err = i2c_master_transmit_receive(*dev_handle, &reg, 1, &status, 1, pdMS_TO_TICKS(100));
    if (err != ESP_OK)
    {
        LOGE(TAG, "Failed to read status register: %s", esp_err_to_name(err));
        return err;
    }

    LOGI(TAG, "DS3231 initialized successfully, status=0x%02X", status);
    return ESP_OK;
}

/**
 * @brief Initialize the system clock from the DS3231 Real Time Clock.
 */
void init_system_time_from_ds3231(i2c_master_dev_handle_t dev_handle)
{
    struct tm rtc_time;
    time_t sys_now;
    struct tm sys_time;

    if (ds3231_get_time(dev_handle, &rtc_time) == ESP_OK)
    {
        time_t rtc_epoch = mktime(&rtc_time);
        struct timeval now = {
            .tv_sec = rtc_epoch,
            .tv_usec = 0,
        };

        settimeofday(&now, NULL);

        // Print both RTC and system time
        time(&sys_now);
        localtime_r(&sys_now, &sys_time);

        LOGD(TAG, "RTC time read: %s", asctime(&rtc_time));
        LOGD(TAG, "System time set: %s", asctime(&sys_time));
    }
    else
    {
        LOGE(TAG, "Failed to read DS3231 at boot, system time starts at default (1970)");
    }
}

/**
 * @brief Convert a decimal integer into BCD format.
 */
static uint8_t dec_to_bcd(int val)
{
    return (uint8_t)((val / 10 * 16) + (val % 10));
}

/**
 * @brief Convert a BCD-encoded byte into a decimal integer.
 */
static int bcd_to_dec(uint8_t val)
{
    return (val / 16 * 10) + (val % 16);
}

/**
 * @brief Set the DS3231 time register from a struct tm.
 */
esp_err_t ds3231_set_time(i2c_master_dev_handle_t dev, const struct tm *timeinfo)
{
    uint8_t buf[8];
    buf[0] = 0x00; // start writing at register 0x00 (seconds)

    buf[1] = dec_to_bcd(timeinfo->tm_sec);
    buf[2] = dec_to_bcd(timeinfo->tm_min);
    buf[3] = dec_to_bcd(timeinfo->tm_hour);
    buf[4] = dec_to_bcd(timeinfo->tm_wday ? timeinfo->tm_wday : 7);
    buf[5] = dec_to_bcd(timeinfo->tm_mday);
    buf[6] = dec_to_bcd(timeinfo->tm_mon + 1);
    buf[7] = dec_to_bcd(timeinfo->tm_year % 100); // 2000–2099

    return i2c_master_transmit(dev, buf, sizeof(buf), -1);
}

/**
 * @brief Read the current time from the DS3231 into a struct tm.
 */
esp_err_t ds3231_get_time(i2c_master_dev_handle_t dev, struct tm *timeinfo)
{
    uint8_t reg = 0x00;
    uint8_t buf[7];
    esp_err_t err = i2c_master_transmit_receive(dev, &reg, 1, buf, sizeof(buf), -1);
    if (err != ESP_OK)
        return err;

    timeinfo->tm_sec = bcd_to_dec(buf[0] & 0x7F);
    timeinfo->tm_min = bcd_to_dec(buf[1] & 0x7F);
    timeinfo->tm_hour = bcd_to_dec(buf[2] & 0x3F);
    timeinfo->tm_wday = bcd_to_dec(buf[3] & 0x07);
    timeinfo->tm_mday = bcd_to_dec(buf[4] & 0x3F);
    timeinfo->tm_mon = bcd_to_dec(buf[5] & 0x1F) - 1;

    uint8_t year_bcd = buf[6] & 0x7F;
    uint8_t century = (buf[6] & 0x80) ? 1 : 0;

    int year = bcd_to_dec(year_bcd); // 0–99

    if (century == 0)
        year += 2000;
    else
        year += 2100;

    timeinfo->tm_year = year - 1900; // struct tm format

    return ESP_OK;
}

/**
 * @brief Synchronize the system time and DS3231 RTC with NTP when possible.
 */
esp_err_t sync_system_and_ds3231_with_ntp(i2c_master_dev_handle_t dev_handle)
{
    char ntp_server[64];
    char timezone[64];

    // Read config under mutex
    if (xSemaphoreTake(time_config_mutex, pdMS_TO_TICKS(20)) != pdTRUE)
    {
        LOGE(TAG, "Failed to acquire time_config mutex");
        return ESP_ERR_TIMEOUT;
    }

    strncpy(ntp_server, time_config.ntp_server, sizeof(ntp_server));
    ntp_server[sizeof(ntp_server) - 1] = '\0';
    strncpy(timezone, time_config.timezone, sizeof(timezone));
    timezone[sizeof(timezone) - 1] = '\0';
    xSemaphoreGive(time_config_mutex);

    // Check network availability (Ethernet preferred, Wi-Fi fallback)
    if (!network_has_ip())
    {
        LOGW(TAG, "No network available, fallback to DS3231");

        struct tm rtc_time;
        if (ds3231_get_time(dev_handle, &rtc_time) == ESP_OK)
        {
            struct timeval tv = {.tv_sec = mktime(&rtc_time), .tv_usec = 0};
            settimeofday(&tv, NULL);
            LOGI(TAG, "System time set from DS3231");
            return ESP_OK;
        }

        LOGE(TAG, "Failed to read DS3231");
        return ESP_FAIL;
    }

    // Configure timezone
    // Set at start, not here
    // setenv("TZ", timezone, 1);
    // tzset();

    // Configure SNTP
    esp_sntp_stop();
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, ntp_server);
    esp_sntp_init();

    LOGI(TAG, "SNTP started with server: %s, TZ: %s", ntp_server, timezone);

    // Wait for sync
    int retry = 0;
    const int retry_count = 10;
    while (retry < retry_count)
    {
        time_t now = time(NULL);
        if (now > 1600000000)
            break; // sanity check: year > 2020
        LOGI(TAG, "Waiting for NTP time... (%d/%d)", retry + 1, retry_count);
        vTaskDelay(pdMS_TO_TICKS(2000));
        retry++;
    }

    if (time(NULL) < 1600000000)
    {
        LOGE(TAG, "NTP sync failed");
        return ESP_FAIL;
    }

    // Log current time
    time_t now;
    struct tm timeinfo;
    time(&now);

    // Write UTC to DS3231, not localtime
    // localtime_r(&now, &timeinfo);
    gmtime_r(&now, &timeinfo);

    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    LOGI(TAG, "System time synchronized (UTC): %s", buf);

    // Update DS3231
    esp_err_t err = ds3231_set_time(dev_handle, &timeinfo);
    if (err == ESP_OK)
    {
        LOGI(TAG, "DS3231 updated successfully");
    }
    else
    {
        LOGE(TAG, "Failed to update DS3231: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

// Low-level helpers
/**
 * @brief Read a single register from the DS3231.
 */
esp_err_t ds3231_read_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(dev, &reg, 1, val, 1, pdMS_TO_TICKS(100));
}

/**
 * @brief Write a single register of the DS3231.
 */
esp_err_t ds3231_write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(dev, buf, 2, pdMS_TO_TICKS(100));
}

// Temperature
/**
 * @brief Read the temperature sensor from the DS3231.
 */
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

// Status + health check
/**
 * @brief Check and format the DS3231 status register.
 */
esp_err_t ds3231_check_status(i2c_master_dev_handle_t dev, char *buf, size_t bufsize)
{
    uint8_t status;
    esp_err_t err = ds3231_read_reg(dev, DS3231_REG_STATUS, &status);
    if (err != ESP_OK)
    {
        LOGE(TAG, "Failed to read status: %s", esp_err_to_name(err));
        return err;
    }

    bool osf = status & 0x80;  // Oscillator Stop Flag
    bool busy = status & 0x04; // Busy flag (temp conversion)
    bool alarm1 = status & 0x01;
    bool alarm2 = status & 0x02;

    if (osf)
    {
        LOGW(TAG, "Oscillator Stop Flag set — RTC may have lost power!");
        ds3231_write_reg(dev, DS3231_REG_STATUS, status & ~0x80); // Clear OSF
    }
    else
    {
        LOGD(TAG, "Oscillator running normally");
    }

    snprintf(buf, bufsize, "OSF=%d Busy=%d\nAl1=%d Al2=%d", osf, busy, alarm1, alarm2);

    LOGD(TAG, "Status bits: OSF=%d, Busy=%d, Alarm1=%d, Alarm2=%d", osf, busy, alarm1, alarm2);

    return ESP_OK;
}

/**
 * @brief Perform a combined DS3231 health check.
 */
void ds3231_health_monitor(i2c_master_dev_handle_t dev)
{
    float temp_c = 0;
    char ds3231_status[30];

    ds3231_check_status(dev, ds3231_status, sizeof(ds3231_status));

    if (ds3231_read_temperature(dev, &temp_c) != ESP_OK)
    {
        LOGE(TAG, "Unable to read temperature from DS3231");
    }
}

/**
 * @brief Save DS3231 diagnostics to NVS storage.
 */
void save_ds3231_diagnostics(float temp_c)
{
    nvs_handle_t handle;

    esp_err_t err = nvs_open(TAG, NVS_READWRITE, &handle);
    if (err != ESP_OK)
    {
        LOGE(TAG, "Error opening NVS: %s", esp_err_to_name(err));
        return;
    }

    nvs_set_i32(handle, "temp_c", (int32_t)(temp_c * 100));

    // Store current epoch time
    time_t now;
    time(&now);
    nvs_set_i64(handle, "last_check", (int64_t)now);

    nvs_commit(handle);
    nvs_close(handle);

    LOGD(TAG, "Diagnostics saved: temp=%.2f °C", temp_c);
}

/**
 * @brief Read DS3231 diagnostics from NVS storage.
 */
void read_ds3231_diagnostics(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(TAG, NVS_READONLY, &handle);

    if (err != ESP_OK)
    {
        LOGW(TAG, "No diagnostics stored yet");
        return;
    }

    int32_t temp100;
    int64_t last_check;

    if (nvs_get_i32(handle, "temp_c", &temp100) == ESP_OK && nvs_get_i64(handle, "last_check", &last_check) == ESP_OK)
    {
        struct tm tm;

        localtime_r((time_t *)&last_check, &tm);
        LOGD(TAG, "Last temp: %.2f °C", temp100 / 100.0f);
        LOGD(TAG, "Last check: %s", asctime(&tm));
    }
    else
    {
        LOGW(TAG, "Diagnostics keys not found");
    }

    nvs_close(handle);
}