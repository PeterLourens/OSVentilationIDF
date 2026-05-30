/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include <esp_attr.h>
#include "task_statemachine.h"

static const char *TAG = "task_sm";
EXT_RAM_BSS_ATTR static StackType_t sm_stack[8192]; // stack buffer
static StaticTask_t sm_tcb;                         // TCB buffer
static size_t sm_stack_size = 8192;                 // 4096 words = 16 KB

// Functions for the task statemachine would go here
void start_task_statemachine(void)
{
    // Create statemachine task
    task_statemachine = xTaskCreateStatic(task_statemachine_code, "statemach", sm_stack_size, NULL, 12, sm_stack, &sm_tcb);

    if (task_statemachine == NULL)
    {
        LOGE(TAG, "Statemachine task creation failed");
    }
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