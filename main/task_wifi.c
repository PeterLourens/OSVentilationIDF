#include "task_wifi.h"

static const char *WIFI_TASK_TAG = "wifi task";

void start_task_wifi(void)
{
    xTaskCreate(task_wifi_code, "task_wifi", 10000, NULL, 6, &task_wifi);
}

void task_wifi_code(void *pvParameters)
{
    esp_err_t err = ESP_OK;

    // Initialize WiFi manager
    err = wifi_manager_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(WIFI_TASK_TAG, "Failed to initialize WiFi manager");
        vTaskDelete(NULL);
        return;
    }

    // Configure WiFi settings
    Network_settings wifi_settings = {
        .enable_dhcp = "true", // Set to "false" to enable AP mode
        .ssid = "DIRK3",
        .wifi_password = "095679706460482465742852",
        .ip_address = "",
        .subnet_mask = "",
        .gateway = "",
        .primary_dns = "",
        .secondary_dns = ""};

    // Connect to WiFi or start AP
    err = wifi_manager_connect(&wifi_settings);
    if (err != ESP_OK)
    {
        ESP_LOGE(WIFI_TASK_TAG, "Failed to connect to WiFi or start AP");
        vTaskDelete(NULL);
        return;
    }

    // Main task loop
    while (1)
    {
        // Check WiFi status
        /*if (wifi_manager_is_connected())
        {
            char ip[16];
            if (wifi_manager_get_ip_info(ip, sizeof(ip)) == ESP_OK)
            {
                ESP_LOGI(WIFI_TASK_TAG, "Connected with IP: %s", ip);
            }
        }
        else
        {
            ESP_LOGW(WIFI_TASK_TAG, "WiFi connection lost, attempting reconnect");
            wifi_manager_connect(&wifi_settings);
        }*/

        vTaskDelay(pdMS_TO_TICKS(5000)); // Check every 5 seconds
    }
}