#include "task_wifi.h"

static const char *TAG = "wifi task";

void start_task_wifi(void)
{
    xTaskCreate(task_wifi_code, "task_wifi", 4096 * 2, NULL, 5, &task_wifi);
}

void task_wifi_code(void *pvParameters)
{
    esp_err_t err;

    // Initialize WiFi
    err = wifi_manager_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize WiFi: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    // Select and start WiFi mode based on configuration
    err = select_wifi_mode();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(err));
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
                ESP_LOGI(TAG, "WiFi Status: Connected, IP: %s", ip);
            }
        }
        else
        {
            ESP_LOGW(TAG, "WiFi Status: Disconnected, attempting reconnect");
            err = select_wifi_mode();
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Reconnection failed: %s", esp_err_to_name(err));
            }
        }

        // Check status every 30 seconds
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}
