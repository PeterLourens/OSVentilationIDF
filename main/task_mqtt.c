#include "task_mqtt.h"
#include "mqtt.h"
#include "network_functions.h"

// desired stack in bytes
#define MQTT_STACK_BYTES (9 * 1024) // try 9 KB first

/* compute words for StackType_t */
#define MQTT_STACK_WORDS (MQTT_STACK_BYTES / sizeof(StackType_t))

// static StackType_t mqtt_stack[MQTT_STACK_WORDS];
// static StaticTask_t mqtt_tcb;
// static const uint32_t mqtt_stack_size = MQTT_STACK_WORDS;

static const char *TAG = "task_MQTT";
static StackType_t mqtt_stack[7168];
static StaticTask_t mqtt_tcb;
static size_t mqtt_stack_size = 7168;

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
    // Give network_manager time to bring up interfaces
    vTaskDelay(pdMS_TO_TICKS(3000));

    LOGI(TAG, "MQTT task starting...");

    // Start MQTT client using current config
    if (mqtt_start(&mqtt_config) != ESP_OK)
    {
        LOGE(TAG, "MQTT: failed to start client");
        vTaskDelete(NULL);
        return;
    }

    // Wait for MQTT connection (non-blocking)
    for (int i = 0; i < 20; i++)
    {
        if (mqtt_is_connected())
        {
            LOGI(TAG, "MQTT: connected");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    while (true)
    {
        LOGI(TAG, "MQTT: publish cycle");

        // If not connected, skip this cycle
        if (!mqtt_is_connected())

        {
            LOGW(TAG, "MQTT: not connected, skipping publish");
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        mqtt_publish_sensor_values();
        mqtt_publish_valve_positions();
        mqtt_publish_state_and_fanspeed();

        vTaskDelay(pdMS_TO_TICKS(20000));
    }
}
