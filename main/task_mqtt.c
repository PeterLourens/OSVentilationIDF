#include "task_mqtt.h"

static const char *TAG = "task_MQTT";

void start_task_mqtt(void)
{
    // Create LCD task
    BaseType_t created = xTaskCreate(task_mqtt_code, "mqtt", 6144, NULL, 6, &task_mqtt);

    if (created != pdPASS)
    {
        LOGE(TAG, "Failed to create MQTT task");
    }
}

void task_mqtt_code(void *pvParameters)
{
    // Some delay a boot
    vTaskDelay(pdMS_TO_TICKS(10000));
    mqtt_start_client();
    vTaskDelay(pdMS_TO_TICKS(2000)); // publish every 10s

    while (true)
    {
        LOGI(TAG, "Update MQTT...");
        mqtt_apply_config_change();
        LOGD(TAG, "Publish sensors...");
        mqtt_publish_sensor_values();
        LOGD(TAG, "Publish valve positions...");
        mqtt_publish_valve_positions();
        LOGD(TAG, "Publish state and fanspeed...");
        mqtt_publish_state_and_fanspeed();
        vTaskDelay(pdMS_TO_TICKS(20000)); // publish every 10s
    }
}