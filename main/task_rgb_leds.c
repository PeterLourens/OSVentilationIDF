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

    esp_err_t err = init_rgb_led();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialise RGB LEDs");
    }

    blink_slow(0, COLOR_RED, brightness);
    blink_fast(1, COLOR_BLUE, brightness);

    while (1)
    {
        // ESP_LOGI(TAG, "Update RGB LEDs");

        /*set_led_color(0, COLOR_RED, brightness); // LED 0 → red @ 50% brightness
        set_led_color(1, COLOR_RED, brightness); // LED 0 → red @ 50% brightness
        led_strip_refresh(rgb_leds);
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Turn off the LEDs
        led_strip_clear(rgb_leds);
        led_strip_refresh(rgb_leds);*/

        update_status_leds(&sm_actuals, brightness);

        rgb_leds_update(); // non-blocking blink engine
        vTaskDelay(pdMS_TO_TICKS(10));

        /*static int color = 0;

        ESP_LOGI(TAG, "LEDs → color index %d", color);

        set_led_color(0, color, brightness);
        set_led_color(1, color, brightness);

        led_strip_refresh(rgb_leds);

        color = (color + 1) % COLOR_COUNT;

        vTaskDelay(pdMS_TO_TICKS(1000));*/

        // blink_slow(0, COLOR_RED, brightness);
        // vTaskDelay(pdMS_TO_TICKS(4000));
        // blink_fast(1, COLOR_BLUE, brightness);
        // vTaskDelay(pdMS_TO_TICKS(4000));
    }
}