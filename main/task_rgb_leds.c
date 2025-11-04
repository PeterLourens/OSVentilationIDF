#include "task_rgb_leds.h"

static const char *TAG = "RGB Leds";

void start_task_np(void)
{
    // Create LCD task
    xTaskCreate(task_rgb_leds_code, "RGB_LED", 4096, NULL, 6, &task_rgb_leds);
}

void task_rgb_leds_code(void *pvParameters)
{
}