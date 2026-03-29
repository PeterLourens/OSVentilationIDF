/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include "task_rgb_leds.h"

static const char *TAG = "task_rgb_leds";
static StackType_t rgb_leds_stack[5120];  // stack buffer
static StaticTask_t rgb_leds_tcb;         // TCB buffer
static size_t rgb_leds_stack_size = 5120; // 5120 words = 20 KB

void start_task_rgb_leds(void)
{
    // Create LCD task
    // xTaskCreate(task_rgb_leds_code, "rgb_leds", 4096, NULL, 2, &task_rgb_leds);
    task_rgb_leds = xTaskCreateStatic(task_rgb_leds_code, "rgb_leds", rgb_leds_stack_size, NULL, 9, rgb_leds_stack, &rgb_leds_tcb);

    if (task_rgb_leds == NULL)
    {
        LOGE(TAG, "RGB LEDs task creation failed");
    }
}

void task_rgb_leds_code(void *pvParameters)
{
    Statemachine_actuals_t snap;

    esp_err_t err = init_rgb_led();
    if (err != ESP_OK)
    {
        LOGE(TAG, "Failed to initialise RGB LEDs");
    }

    while (1)
    {
        // Wait for notify OR timeout (for blinking)
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(200));

        // Safe snapshot
        if (xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(5)) == pdTRUE)
        {
            snap = sm_actuals;
            xSemaphoreGive(statemachine_mutex);
        }

        // Update LEDs
        leds_update(&snap);
    }
}