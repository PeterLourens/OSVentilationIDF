#include "rgb_leds.h"

static const char *TAG = "RGB LED";

esp_err_t init_rgb_led(void)
{
    esp_err_t err= ESP_OK;
    
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

    led_strip_set_pixel(rgb_leds, led_num,
                                        apply_brightness(r, brightness),
                                        apply_brightness(g, brightness),
                                        apply_brightness(b, brightness));

    led_strip_refresh(rgb_leds);
}

/*void ws2812_init()
{
    // Create RMT TX channel
    rmt_tx_channel_config_t tx_config = {
        .gpio_num = IO_PIN_WS2812B,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
        .flags.with_dma = false,
    };
    rmt_new_tx_channel(&tx_config, &rmt_tx_channel);
    rmt_enable(rmt_tx_channel);

    // Create bytes encoder (empty config in ESP-IDF 5.5.1)
    rmt_bytes_encoder_config_t encoder_config = {};
    rmt_new_bytes_encoder(&encoder_config, &ws_encoder);
}

void ws2812_send(ws2812_color_t *leds, size_t num_leds, float brightness)
{
    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
        .flags.eot_level = 1,
    };

    uint8_t buffer[LED_NUM * 3]; // GRB format
    for (size_t i = 0; i < num_leds; i++)
    {
        buffer[i * 3 + 0] = (uint8_t)(leds[i].green * brightness);
        buffer[i * 3 + 1] = (uint8_t)(leds[i].red * brightness);
        buffer[i * 3 + 2] = (uint8_t)(leds[i].blue * brightness);
    }

    rmt_transmit(rmt_tx_channel, ws_encoder, buffer, sizeof(buffer), &tx_config);
    rmt_tx_wait_all_done(rmt_tx_channel, -1);
}*/
