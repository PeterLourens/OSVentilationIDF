/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include "task_i2c_ds3231.h"

static const char *TAG = "task_DS3231_sync";
static StackType_t i2c_ds3231_stack[4096];  // stack buffer
static StaticTask_t i2c_ds3231_tcb;         // TCB buffer
static size_t i2c_ds3231_stack_size = 4096; // 4096 words = 16 KB

void start_task_i2c_ds3231(void)
{
    // xTaskCreate(task_i2c_ds3231_code, "i2c_ds3231", 4096, NULL, 8, &task_i2c_ds3231);
    task_i2c_ds3231 = xTaskCreateStatic(task_i2c_ds3231_code, "i2c_ds3231", i2c_ds3231_stack_size, NULL, 8, i2c_ds3231_stack, &i2c_ds3231_tcb);

    if (task_i2c_ds3231 == NULL)
    {
        LOGE(TAG, "I2C DS3231 task creation failed");
    }
}

void task_i2c_ds3231_code(void *pvParameters)
{
    esp_err_t err = ESP_OK;

    err = i2c_ds3231_init(i2c_bus0_handle, &ds3231_bus0_handle);
    if (err != ESP_OK)
    {
        LOGE(TAG, "Failed to initialize DS3231 on bus 0: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }
    else
    {
        LOGD(TAG, "DS3231 initialized on bus 0");
    }

    while (1)
    {
        if (!wifi_has_ip())
        {
            LOGW(TAG, "Wi-Fi not ready, retrying...");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        ds3231_health_monitor(ds3231_bus0_handle);

        esp_err_t err = sync_system_and_ds3231_with_ntp(ds3231_bus0_handle);
        if (err == ESP_OK)
        {
            LOGI(TAG, "DS3231 successfully synced with NTP");
        }
        else
        {
            LOGW(TAG, "Sync attempt failed: %s", esp_err_to_name(err));
        }

        // Sleep for one hour before next sync
        vTaskDelay(3600 * 1000);
    }
}