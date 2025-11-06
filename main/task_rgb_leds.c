#include "task_rgb_leds.h"

static const char *TAG = "RGB Leds";

void start_task_rgb_leds(void)
{
    // Create LCD task
    xTaskCreate(task_rgb_leds_code, "rgb_leds", 4096, NULL, 2, &task_rgb_leds);
}

void task_rgb_leds_code(void *pvParameters)
{
    u_int8_t brightness = 32;

    esp_err_t err = init_rgb_led();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialise RGB LEDs");
    }

    while (1)
    {
        ESP_LOGI(TAG, "Update RGB LEDs");

        // Turn on LED 0 with red color
        set_single_led_color_with_brightness(0, 255, 0, 0, brightness);
        led_strip_refresh(rgb_leds);

        vTaskDelay(pdMS_TO_TICKS(1000));

        // Turn on LED 1 with blue color
        set_single_led_color_with_brightness(1, 0, 255, 0, brightness);
        led_strip_refresh(rgb_leds);

        vTaskDelay(pdMS_TO_TICKS(1000));

        set_single_led_color_with_brightness(0, 0, 255, 0, brightness);
        led_strip_refresh(rgb_leds);

        vTaskDelay(pdMS_TO_TICKS(1000));

        // Turn on LED 1 with blue color
        set_single_led_color_with_brightness(1, 0, 0, 255, brightness);
        led_strip_refresh(rgb_leds);

        vTaskDelay(pdMS_TO_TICKS(1000));

        set_single_led_color_with_brightness(0, 0, 0, 255, brightness);
        led_strip_refresh(rgb_leds);

        vTaskDelay(pdMS_TO_TICKS(1000));

        // Turn on LED 1 with blue color
        set_single_led_color_with_brightness(1, 255, 0, 0, brightness);
        led_strip_refresh(rgb_leds);

        vTaskDelay(pdMS_TO_TICKS(1000));

        set_single_led_color_with_brightness(0, 255, 0, 255, brightness);
        led_strip_refresh(rgb_leds);

        vTaskDelay(pdMS_TO_TICKS(1000));

        // Turn on LED 1 with blue color
        set_single_led_color_with_brightness(1, 0, 255, 255, brightness);
        led_strip_refresh(rgb_leds);

        vTaskDelay(pdMS_TO_TICKS(1000));

        // Turn off the LEDs
        led_strip_clear(rgb_leds);
        led_strip_refresh(rgb_leds);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}