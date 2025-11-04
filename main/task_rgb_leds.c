#include "task_rgb_leds.h"

static const char *TAG = "RGB Leds";

void start_task_rgb_leds(void)
{
    // Create LCD task
    xTaskCreate(task_rgb_leds_code, "rgb_leds", 4096, NULL, 6, &task_rgb_leds);
}

void task_rgb_leds_code(void *pvParameters)
{
    ws2812_init();

    ws2812_color_t leds[LED_NUM] = {
        {.red = 255, .green = 0, .blue = 0},
        {.red = 0, .green = 255, .blue = 0}};

    float brightness = 0.3f; // 30% brightness

    while (true)
    {
        ws2812_send(leds, 1, brightness);
        ws2812_send(leds, 2, brightness);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}