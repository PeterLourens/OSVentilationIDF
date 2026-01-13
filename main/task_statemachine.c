#include "task_statemachine.h"

static const char *TAG = "Statemachine";

// Functions for the task statemachine would go here
void start_task_statemachine(void)
{
    // Create statemachine task
    xTaskCreate(task_statemachine_code, "statemach", 8192, NULL, 6, &task_statemachine);
}

void task_statemachine_code(void *pvParameters)
{
    init_statemachine();

    for (;;)
    {
        // Run the state machine
        ESP_LOGI(TAG, "Running statemachine...");
        run_statemachine();

        // Update LEDs
        ESP_LOGI(TAG, "Update LEDs...");
        xTaskNotifyGive(task_rgb_leds);

        // Run every 30 secs
        vTaskDelay(30000);
    }
}