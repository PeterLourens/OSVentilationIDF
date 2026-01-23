#include "task_statemachine.h"

static const char *TAG = "task_SM";

// Functions for the task statemachine would go here
void start_task_statemachine(void)
{
    // Create statemachine task
    xTaskCreate(task_statemachine_code, "statemach", 8192, NULL, 6, &task_statemachine);
}

void task_statemachine_code(void *pvParameters)
{
    init_statemachine();

    // Set name temp to state number 13 otherwise function state name cannot be found from state number
    if (state_config_mutex && xSemaphoreTake(state_config_mutex, (TickType_t)10) == pdTRUE)
    {
        strncpy(statemachine_config[STATE_NR_TEMP].state_name, "temp", sizeof(statemachine_config[STATE_NR_TEMP].state_name));
        xSemaphoreGive(state_config_mutex);
    }

    for (;;)
    {
        // Run the state machine
        LOGI(TAG, "Running statemachine...");
        run_statemachine();

        // Update LEDs
        LOGI(TAG, "Update LEDs...");
        xTaskNotifyGive(task_rgb_leds);

        // Run every 30 secs
        vTaskDelay(30000);
    }
}