#include "task_i2c_ds3231.h"

const char *TAG = "DS3231 Sync task";

void start_task_i2c_ds3231(void)
{
    // Register Wi‑Fi event handler
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_got_ip, NULL));

    // Create DS3231 sync task
    xTaskCreate(task_i2c_ds3231_code, "i2c_ds3231", 4096, NULL, 6, &task_i2c_ds3231);
}

void task_i2c_ds3231_code(void *pvParameters)
{
    // esp_err_t err = ESP_OK;

    /*err = i2c_ds3231_init(i2c_bus0_handle, &ds3231_bus0_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize DS3231 on bus 0: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }
    else
    {
        ESP_LOGI(TAG, "DS3231 initialized on bus 0");
    }*/

    while (1)
    {
        if (!wifi_has_ip())
        {
            ESP_LOGW(TAG, "Wi-Fi not ready, retrying...");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        esp_err_t err = sync_system_and_ds3231_with_ntp(ds3231_bus0_handle);
        if (err == ESP_OK)
        {
            ESP_LOGI(TAG, "DS3231 successfully synced with NTP");
        }
        else
        {
            ESP_LOGW(TAG, "Sync attempt failed: %s", esp_err_to_name(err));
        }

        // Sleep for one hour before next sync
        vTaskDelay(pdMS_TO_TICKS(3600 * 1000));
    }
}