#include "task_rgb_leds.h"

static const char *TAG = "RGB Leds";

void start_task_rgb_leds(void)
{
    // Create LCD task
    xTaskCreate(task_rgb_leds_code, "rgb_leds", 4096, NULL, 2, &task_rgb_leds);
}

void task_rgb_leds_code(void *pvParameters)
{
    u_int8_t brightness = 16;
    Statemachine_actuals_t snap;

    esp_err_t err = init_rgb_led();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialise RGB LEDs");
    }

    // blink_slow(0, COLOR_RED, brightness);
    // blink_fast(1, COLOR_BLUE, brightness);

    while (1)
    {
        while (1)
        {
            if (sm_get_snapshot(&snap))
            {
                // ESP_LOGI("SNAP", "snap.valve_lock=%d", snap.valve_lock);
                update_status_leds(&snap, brightness);
            }

            rgb_leds_update(); // non-blocking blink engine
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}