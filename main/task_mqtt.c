/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include "task_mqtt.h"

static const char *TAG = "task_MQTT";
static StackType_t mqtt_stack[6144];  // stack buffer
static StaticTask_t mqtt_tcb;         // TCB buffer
static size_t mqtt_stack_size = 6144; // 4096 words = 16 KB

void start_task_mqtt(void)
{
    // Create MQTT task
    // BaseType_t created = xTaskCreate(task_mqtt_code, "mqtt", 6144, NULL, 6, &task_mqtt);
    task_mqtt = xTaskCreateStatic(task_mqtt_code, "mqtt", mqtt_stack_size, NULL, 9, mqtt_stack, &mqtt_tcb);

    if (task_mqtt == NULL)
    {
        LOGE(TAG, "MQTT task creation failed");
    }
}

void task_mqtt_code(void *pvParameters)
{
    // Give the system some time to bring up netifs
    vTaskDelay(pdMS_TO_TICKS(5000));

    // Wait until Ethernet or Wi-Fi has an IP
    while (!network_has_ip())
    {
        LOGW(TAG, "MQTT: No network available, waiting...");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    LOGI(TAG, "MQTT: Network ready, starting client");
    mqtt_start_client();

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

        vTaskDelay(pdMS_TO_TICKS(20000));
    }
}

/*void task_mqtt_code(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(10000));
    mqtt_start_client();

    while (true)
    {
        mqtt_apply_config_change();
        mqtt_publish_sensor_values();
        mqtt_publish_valve_positions();
        mqtt_publish_state_and_fanspeed();
        vTaskDelay(pdMS_TO_TICKS(20000));
    }
}*/