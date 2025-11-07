#include "rgb_leds.h"

static const char *TAG = "RGB LED";

esp_err_t init_rgb_led(void)
{
    esp_err_t err = ESP_OK;

    // Configure the LED strip
    led_strip_config_t rgb_led_config = {
        .strip_gpio_num = GPIO_RGB_LED,
        .max_leds = RGB_LED_NUM,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
    };

    err = led_strip_new_rmt_device(&rgb_led_config, &rmt_config, &rgb_leds);
    ESP_LOGI(TAG, "LED strip initialized");

    return err;
}

uint8_t apply_brightness(uint8_t color, uint8_t brightness)
{
    return (color * brightness) / 255;
}

void set_single_led_color_with_brightness(int led_num, uint8_t r, uint8_t g, uint8_t b, uint8_t brightness)
{
    if (led_num < 0 || led_num >= RGB_LED_NUM)
    {
        ESP_LOGW(TAG, "Invalid LED number: %d", led_num);
        return;
    }

    led_strip_set_pixel(rgb_leds, led_num, apply_brightness(r, brightness), apply_brightness(g, brightness), apply_brightness(b, brightness));
    led_strip_refresh(rgb_leds);
}
