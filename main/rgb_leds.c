#include "rgb_leds.h"

static rmt_channel_handle_t rmt_tx_channel = NULL;
static rmt_encoder_handle_t ws_encoder = NULL;

ws2812_color_t rgb_leds;

void ws2812_init()
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
}
