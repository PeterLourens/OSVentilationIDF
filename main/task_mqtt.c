#include "task_mqtt.h"

static const char *TAG = "MQTT Task";

void start_task_mqtt(void)
{
    // Create LCD task
    xTaskCreate(task_mqtt_code, "mqtt", 10000, NULL, 6, &task_mqtt);
}

void task_mqtt_code(void *pvParameters)
{
    ESP_LOGI(TAG, "MQTT task started...");

    // Initialize MQTT client
    //mqtt_client_init();

    while (1)
    {
        ESP_LOGI(TAG, "MQTT task running...");
        // Here you can add code to publish/subscribe to topics, handle messages, etc.

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}