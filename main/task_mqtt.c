/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include "task_mqtt.h"

static const char *TAG = "task_MQTT";
static StackType_t mqtt_stack[8192];
static StaticTask_t mqtt_tcb;
static size_t mqtt_stack_size = 8192;

void start_task_mqtt(void)
{
    task_mqtt = xTaskCreateStatic(task_mqtt_code, "mqtt", mqtt_stack_size, NULL, 9, mqtt_stack, &mqtt_tcb);

    if (task_mqtt == NULL)
    {
        LOGE(TAG, "MQTT task creation failed");
    }
}

void task_mqtt_code(void *pvParameters)
{
    (void)pvParameters;

    // Give network_manager time to bring up interfaces and start MQTT
    vTaskDelay(pdMS_TO_TICKS(3000));

    LOGI(TAG, "MQTT task starting...");

    while (true)
    {
        LOGI(TAG, "MQTT: publish cycle");

        if (!mqtt_is_connected())
        {
            LOGW(TAG, "MQTT: not connected, skipping publish");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        // Handle deferred HA discovery here (MQTT task context)
        if (mqtt_discovery_requested() && sensors_ready())
        {
            mqtt_clear_discovery_request();
            mqtt_publish_homeassistant_discovery();
        }

        mqtt_publish_sensor_values();
        mqtt_publish_valve_positions();
        mqtt_publish_state_and_fanspeed();

        vTaskDelay(pdMS_TO_TICKS(20000));
    }
}
