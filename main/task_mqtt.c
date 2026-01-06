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
    ESP_LOGI(TAG, "MQTT task started...");

    ESP_LOGI(TAG, "MQTT task running");

    mqtt_start_client();

    while (true)
    {
        mqtt_apply_config_change();
        mqtt_publish_sensor_values();
        vTaskDelay(pdMS_TO_TICKS(10000)); // publish every 10s
    }
}