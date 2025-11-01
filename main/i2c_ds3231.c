#include "i2c_ds3231.h"

static const char *TAG = "DS3231";

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
    timeinfo->tm_mon = (((data[5] >> 4) * 10) + (data[5] & 0x0F)) - 1;          // Convert 1-12 to 0-11
    timeinfo->tm_year = ((data[6] >> 4) * 10) + (data[6] & 0x0F) + 2000 - 1900; // Convert to years since 1900

    ESP_LOGD(TAG, "Time read: %02d:%02d:%02d %02d/%02d/%04d",
             timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec,
             timeinfo->tm_mday, timeinfo->tm_mon + 1, timeinfo->tm_year + 1900);

    return ESP_OK;
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

// Minimal sync function
esp_err_t sync_system_and_ds3231_with_ntp(i2c_master_dev_handle_t dev_handle)
{
    if (!wifi_has_ip())
    {
        ESP_LOGE(TAG, "No Wi-Fi connection, cannot sync");
        return ESP_ERR_INVALID_STATE;
    }

    // Configure SNTP
    esp_sntp_stop(); // reset if already running
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "129.6.15.28"); // numeric IP (NIST)
    esp_sntp_set_time_sync_notification_cb(time_sync_cb);
    esp_sntp_init();

    ESP_LOGI(TAG, "SNTP started, waiting for callback...");
    return ESP_OK;
}

/*esp_err_t sync_system_and_ds3231_with_ntp(i2c_master_dev_handle_t dev_handle)
{
    esp_err_t err = ESP_OK;

    // Take mutex to safely read config
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

    // Configure timezone
    err = setenv("TZ", timezone, 1);
    if (err != 0)
    {
        ESP_LOGE(TAG, "Failed to set TZ environment variable");
        return ESP_FAIL;
    }
    tzset();

    // Configure SNTP
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, ntp_server);
    esp_sntp_init();

    ESP_LOGI(TAG, "SNTP initialized with server: %s, waiting for sync...", ntp_server);

    // Wait for sync
    int retry = 0;
    const int retry_count = 60;
    while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED && ++retry < retry_count)
    {
        ESP_LOGI(TAG, "Waiting for NTP sync... (%d/%d)", retry, retry_count);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    if (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED)
    {
        ESP_LOGE(TAG, "Failed to sync with NTP server");
        return ESP_ERR_TIMEOUT;
    }

    // Get current system time
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    // Write to DS3231
    err = i2c_ds3231_write_time(dev_handle, &timeinfo);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to write time to DS3231: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "System and DS3231 synchronized. Time: %s, TZ: %s",
             asctime(&timeinfo), timezone);

    return ESP_OK;
}
*/