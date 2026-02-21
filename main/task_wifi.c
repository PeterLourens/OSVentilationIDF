/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include "task_wifi.h"

static const char *TAG = "task_wifi";
static StackType_t wifi_stack[6144];  // stack buffer
static StaticTask_t wifi_tcb;         // TCB buffer
static size_t wifi_stack_size = 6144; // 4096 words = 16 KB

void start_task_wifi(void)
{
    // xTaskCreate(task_wifi_code, "task_wifi", 6144, NULL, 5, &task_wifi);
    task_wifi = xTaskCreateStatic(task_wifi_code, "task_wifi", wifi_stack_size, NULL, 9, wifi_stack, &wifi_tcb);

    if (task_wifi == NULL)
    {
        LOGE(TAG, "Wifi task creation failed");
    }
}

void task_wifi_code(void *pvParameters)
{
    esp_err_t err;

    // Check config first
    if (xSemaphoreTake(network_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        if (strcmp(wifi_config.enable_wifi, "Off") == 0)
        {
            xSemaphoreGive(network_config_mutex);
            LOGW(TAG, "WiFi disabled in config; skipping WiFi initialization");
            vTaskDelete(NULL); // stop WiFi task completely
            return;
        }
        xSemaphoreGive(network_config_mutex);
    }

    // Initialize WiFi
    err = wifi_manager_init();
    if (err != ESP_OK)
    {
        LOGE(TAG, "Failed to initialize WiFi: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    // Select and start WiFi mode based on configuration
    err = select_wifi_mode();
    if (err != ESP_OK)
    {
        LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    // Main task loop
    while (1)
    {
        if (wifi_manager_is_connected())
        {
            char ip[16];
            if (wifi_manager_get_ip_info(ip, sizeof(ip)) == ESP_OK)
            {
                LOGI(TAG, "WiFi Status: Connected, IP: %s", ip);
            }
        }
        else
        {
            LOGW(TAG, "WiFi Status: Disconnected, attempting reconnect");
            err = select_wifi_mode();
            if (err != ESP_OK)
            {
                LOGE(TAG, "Reconnection failed: %s", esp_err_to_name(err));
            }
        }

        // Check status every 30 seconds
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}
