#include "task_rgb_leds.h"

static const char *TAG = "task_rgb_leds";

// Local snapshot (safe copy)
//static Statemachine_actuals_t snap;

void start_task_rgb_leds(void)
{
    // Create LCD task
    xTaskCreate(task_rgb_leds_code, "rgb_leds", 4096, NULL, 2, &task_rgb_leds);
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