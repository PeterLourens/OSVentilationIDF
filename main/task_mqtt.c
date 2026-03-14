/*
    task_mqtt.c — MQTT task for OSVentilationIDF
*/

#include "task_mqtt.h"

// desired stack in bytes
#define MQTT_STACK_BYTES (32 * 1024) // try 16 KB first

/* compute words for StackType_t */
#define MQTT_STACK_WORDS (MQTT_STACK_BYTES / sizeof(StackType_t))

static StackType_t mqtt_stack[MQTT_STACK_WORDS];
static StaticTask_t mqtt_tcb;
static const uint32_t mqtt_stack_size = MQTT_STACK_WORDS;

static const char *TAG = "task_MQTT";

void start_task_mqtt(void)
{
    /* Create MQTT task */
    task_mqtt = xTaskCreateStatic(task_mqtt_code, "mqtt", mqtt_stack_size, NULL, 9, mqtt_stack, &mqtt_tcb);

    if (task_mqtt == NULL)
    {
        LOGE(TAG, "MQTT task creation failed");
    }
}

void task_mqtt_code(void *pvParameters)
{
    /* Initial delay to allow network stack to come up */
    vTaskDelay(pdMS_TO_TICKS(10000));

    /* Start client (this will create the connection semaphore internally) */
    mqtt_start_client();

    /* Wait up to 10 seconds for a connection before first publish cycle.
       This avoids the initial race where the client is started but not yet connected. */
    if (mqtt_wait_for_connection(pdMS_TO_TICKS(10000)))
    {
        LOGI(TAG, "MQTT: connected, proceeding with initial publishes");
    }
    else
    {
        LOGW(TAG, "MQTT: did not connect within timeout; will continue and retry publishes");
    }

    while (true)
    {
        LOGI(TAG, "Update MQTT...");

        mqtt_apply_config_change();

        LOGD(TAG, "Publish sensors...");
        // If not connected, try waiting briefly for a connection before skipping publishes
        if (!mqtt_wait_for_connection(0))
        {
            // immediate check failed — try a short timed wait
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
