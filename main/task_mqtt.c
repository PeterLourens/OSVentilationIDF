/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

/**
 * @file task_mqtt.c
 * @brief MQTT background task for periodic telemetry and Home Assistant discovery publishing.
 */

#include <esp_attr.h>
#include "task_mqtt.h"

static const char *TAG = "task_mqtt";
EXT_RAM_BSS_ATTR static StackType_t mqtt_stack[8192];
static StaticTask_t mqtt_tcb;
static size_t mqtt_stack_size = 8192; // 8192 words = 32768 KB
static volatile bool mqtt_stop_requested = false;

static bool mqtt_task_enabled(void)
{
    bool enabled = false;

    if (mqtt_config_mutex && xSemaphoreTake(mqtt_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        enabled = strcmp(mqtt_config.enable_mqtt, "On") == 0;
        xSemaphoreGive(mqtt_config_mutex);
    }

    return enabled;
}

void stop_task_mqtt(void)
{
    mqtt_stop_requested = true;

    if (task_mqtt == NULL)
    {
        LOGI(TAG, "MQTT task not running");
        return;
    }

    LOGI(TAG, "Stopping MQTT task");
    TickType_t start = xTaskGetTickCount();
    while (task_mqtt != NULL && (xTaskGetTickCount() - start) < pdMS_TO_TICKS(25000))
    {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void start_task_mqtt(void)
{
    if (!mqtt_task_enabled())
    {
        LOGI(TAG, "MQTT disabled by config; not starting task");
        if (task_mqtt != NULL)
        {
            stop_task_mqtt();
        }
        return;
    }

    if (task_mqtt != NULL)
    {
        LOGI(TAG, "MQTT task already running");
        return;
    }

    mqtt_stop_requested = false;
    task_mqtt = xTaskCreateStatic(task_mqtt_code, "mqtt", mqtt_stack_size, NULL, 9, mqtt_stack, &mqtt_tcb);

    if (task_mqtt == NULL)
    {
        LOGE(TAG, "MQTT task creation failed");
        return;
    }

    LOGI(TAG, "MQTT task started");
}

bool refresh_mqtt_task_state(void)
{
    if (mqtt_task_enabled())
    {
        start_task_mqtt();
        return true;
    }

    stop_task_mqtt();
    return false;
}

void task_mqtt_code(void *pvParameters)
{
    (void)pvParameters;

    // Give network_manager time to bring up interfaces and start MQTT
    for (int i = 0; i < 30 && !mqtt_stop_requested; i++)
        vTaskDelay(pdMS_TO_TICKS(100));

    LOGI(TAG, "MQTT task starting...");

    while (!mqtt_stop_requested)
    {
        if (!mqtt_task_enabled())
        {
            LOGI(TAG, "MQTT disabled by config; task exiting");
            break;
        }

        LOGI(TAG, "MQTT: publish cycle");

        if (!mqtt_is_connected())
        {
            LOGW(TAG, "MQTT: not connected, skipping publish");
            for (int i = 0; i < 50 && !mqtt_stop_requested; i++)
                vTaskDelay(pdMS_TO_TICKS(100));
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

        for (int i = 0; i < 200 && !mqtt_stop_requested; i++)
            vTaskDelay(pdMS_TO_TICKS(100));
    }

    LOGI(TAG, "MQTT task exiting cleanly");
    task_mqtt = NULL;
    vTaskDelete(NULL);
}
