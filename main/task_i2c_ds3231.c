#include "task_i2c_ds3231.h"

const char *TAG = "DS3231_SYNC_TASK";

void start_task_i2c_ds3231(void)
{
    xTaskCreate(task_i2c_ds3231_code, "i2c_ds3231", 10000, NULL, 6, &task_i2c_ds3231);
}

void task_i2c_ds3231_code(void *pvParameters)
{
    esp_err_t err = ESP_OK;

    err = i2c_ds3231_init(i2c_bus0_handle, &ds3231_bus1_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize DS3231 on bus 1: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }
    else
    {
        ESP_LOGI(TAG, "DS3231 initialized on bus 1");
    }

    while (1)
    {

        esp_err_t err = sync_system_and_ds3231_with_ntp(ds3231_bus1_handle);

        if (err == ESP_OK)
        {
            ESP_LOGI(TAG, "DS3231 successfully synced with NTP");
        }
        else
        {
            ESP_LOGW(TAG, "Sync attempt failed: %s", esp_err_to_name(err));
        }

        // Sleep for one hour before next sync
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}