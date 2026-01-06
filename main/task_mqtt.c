#include "task_mqtt.h"

static const char *TAG = "MQTT Task";

void start_task_mqtt(void)
{
    // Create LCD task
    BaseType_t created = xTaskCreate(task_mqtt_code, "mqtt", 10000, NULL, 6, &task_mqtt);

    if (created != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create MQTT task");
    }
}

void task_mqtt_code(void *pvParameters)
{

    const MQTT_config_t *cfg = (const MQTT_config_t *)pvParameters;
    ESP_LOGI(TAG, "MQTT task started...");

    // Start MQTT client here (moved from start_task_mqtt)
    esp_err_t start_err = mqtt_start(cfg);
    if (start_err != ESP_OK)
    {
        ESP_LOGE(TAG, "mqtt_start failed: %s", esp_err_to_name(start_err));
    }

    // Simple reconnect/backoff loop variables
    const TickType_t publish_period = pdMS_TO_TICKS(10000); // 10s publish
    const TickType_t retry_min = pdMS_TO_TICKS(2000);       // 2s min retry
    const TickType_t retry_max = pdMS_TO_TICKS(30000);      // 30s max retry
    TickType_t retry_delay = retry_min;

    while (1)
    {
        if (!mqtt_is_connected())
        {
            ESP_LOGW(TAG, "MQTT not connected; attempting (re)start...");
            // Try to (re)start the client
            esp_err_t err = mqtt_start(cfg);
            if (err == ESP_OK && mqtt_is_connected())
            {
                ESP_LOGI(TAG, "MQTT connected");
                retry_delay = retry_min; // reset backoff after success
            }
            else
            {
                ESP_LOGW(TAG, "MQTT start/connect failed, retry in %d ms",
                        (int)(retry_delay * portTICK_PERIOD_MS));
                vTaskDelay(retry_delay);
                // Exponential backoff (cap at retry_max)
                retry_delay = (retry_delay < retry_max) ? (retry_delay * 2) : retry_max;
                continue; // try again
            }
        }

        // Connected: publish sensor values
        mqtt_publish_sensor_values(cfg);

        vTaskDelay(publish_period);
    }
}