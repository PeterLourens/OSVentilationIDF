#include "task_i2c_ds3231.h"

const char *TAG = "DS3231 Sync task";

void start_task_i2c_ds3231(void)
{
    xTaskCreate(task_i2c_ds3231_code, "i2c_ds3231", 4096, NULL, 6, &task_i2c_ds3231);
}

void task_i2c_ds3231_code(void *pvParameters)
{
    esp_err_t err = ESP_OK;

    err = i2c_ds3231_init(i2c_bus0_handle, &ds3231_bus0_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize DS3231 on bus 0: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }
    else
    {
        ESP_LOGI(TAG, "DS3231 initialized on bus 0");
    }

    while (1)
    {
        if (!wifi_has_ip())
        {
            ESP_LOGW(TAG, "Wi-Fi not ready, retrying...");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        // Start SNTP if not already running
        if (!esp_sntp_enabled())
        {
            esp_sntp_stop(); // reset if needed
            esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
            esp_sntp_setservername(0, "129.6.15.28"); // numeric IP (NIST)
            esp_sntp_set_time_sync_notification_cb(time_sync_cb);
            esp_sntp_init();
            ESP_LOGI(TAG, "SNTP started, waiting for sync...");
        }

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}