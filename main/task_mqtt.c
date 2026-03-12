/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include "task_mqtt.h"
#include "mqtt.h"

static const char *TAG = "task_MQTT";
static StackType_t mqtt_stack[7168];  // stack buffer
static StaticTask_t mqtt_tcb;         // TCB buffer
static size_t mqtt_stack_size = 7168; // 7168 words = 28.672 KB

void start_task_mqtt(void)
{
    // Create staticMQTT task
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
    if (mqtt_start_client() == ESP_OK)
    {
        // Wait up to 10 seconds for a connection before first publish cycle.
        // This avoids the initial race where the client is started but not yet connected.
        if (mqtt_wait_for_connection(pdMS_TO_TICKS(10000)))
        {
            LOGI(TAG, "MQTT: connected, proceeding with initial publishes");
        }
        else
        {
            LOGW(TAG, "MQTT: did not connect within timeout; will retry publishes later");
        }
    }

    while (true)
    {
        LOGI(TAG, "Update MQTT...");

        mqtt_apply_config_change();

        LOGD(TAG, "Publish sensors...");
        // If not connected, wait a short time for connection before skipping publishes
        if (!mqtt_wait_for_connection(0))
        {
            // mqtt_wait_for_connection(0) returns immediately; use a timed wait to attempt connection
            if (!mqtt_wait_for_connection(pdMS_TO_TICKS(5000)))
            {
                LOGW(TAG, "Still not connected; skipping this publish cycle");
                vTaskDelay(pdMS_TO_TICKS(20000));
                continue;
            }
        }

        mqtt_publish_sensor_values();

        LOGD(TAG, "Publish valve positions...");
        mqtt_publish_valve_positions();

        LOGD(TAG, "Publish state and fanspeed...");
        mqtt_publish_state_and_fanspeed();

        vTaskDelay(pdMS_TO_TICKS(20000));
    }
}
