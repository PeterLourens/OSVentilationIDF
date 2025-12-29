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

    while (1)
    {
        ESP_LOGI(TAG, "Update RGB LEDs");

        set_led_color(0, COLOR_RED, brightness); // LED 0 → red @ 50% brightness
        set_led_color(1, COLOR_RED, brightness); // LED 0 → red @ 50% brightness
        led_strip_refresh(rgb_leds);
        vTaskDelay(pdMS_TO_TICKS(1000));

        set_led_color(0, COLOR_GREEN, brightness); // LED 0 → red @ 50% brightness
        set_led_color(1, COLOR_GREEN, brightness); // LED 0 → red @ 50% brightness
        led_strip_refresh(rgb_leds);
        vTaskDelay(pdMS_TO_TICKS(1000));

        set_led_color(0, COLOR_BLUE, brightness); // LED 0 → red @ 50% brightness
        set_led_color(1, COLOR_BLUE, brightness); // LED 0 → red @ 50% brightness
        led_strip_refresh(rgb_leds);
        vTaskDelay(pdMS_TO_TICKS(1000));

        set_led_color(0, COLOR_YELLOW, brightness); // LED 0 → red @ 50% brightness
        set_led_color(1, COLOR_YELLOW, brightness); // LED 0 → red @ 50% brightness
        led_strip_refresh(rgb_leds);
        vTaskDelay(pdMS_TO_TICKS(1000));

        set_led_color(0, COLOR_MAGENTA, brightness); // LED 0 → red @ 50% brightness
        set_led_color(1, COLOR_MAGENTA, brightness); // LED 0 → red @ 50% brightness
        led_strip_refresh(rgb_leds);
        vTaskDelay(pdMS_TO_TICKS(1000));

        set_led_color(0, COLOR_CYAN, brightness); // LED 0 → red @ 50% brightness
        set_led_color(1, COLOR_CYAN, brightness); // LED 0 → red @ 50% brightness
        led_strip_refresh(rgb_leds);
        vTaskDelay(pdMS_TO_TICKS(1000));

        set_led_color(0, COLOR_WHITE, brightness); // LED 0 → red @ 50% brightness
        set_led_color(1, COLOR_WHITE, brightness); // LED 0 → red @ 50% brightness
        led_strip_refresh(rgb_leds);
        vTaskDelay(pdMS_TO_TICKS(1000));

        set_led_color(0, COLOR_GRAY, brightness); // LED 0 → red @ 50% brightness
        set_led_color(1, COLOR_GRAY, brightness); // LED 0 → red @ 50% brightness
        led_strip_refresh(rgb_leds);
        vTaskDelay(pdMS_TO_TICKS(1000));

        set_led_color(0, COLOR_ORANGE, brightness); // LED 0 → red @ 50% brightness
        set_led_color(1, COLOR_ORANGE, brightness); // LED 0 → red @ 50% brightness
        led_strip_refresh(rgb_leds);
        vTaskDelay(pdMS_TO_TICKS(1000));

        set_led_color(0, COLOR_PURPLE, brightness); // LED 0 → red @ 50% brightness
        set_led_color(1, COLOR_PURPLE, brightness); // LED 0 → red @ 50% brightness
        led_strip_refresh(rgb_leds);
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Turn off the LEDs
        led_strip_clear(rgb_leds);
        led_strip_refresh(rgb_leds);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}