#ifndef RGB_LEDS_H
#define RGB_LEDS_H

#include <stdio.h>

#include "globals.h"
#include "wifi_functions.h"

#define IO_PIN_WS2812B 42         // The ESP32 pin GPIO10 connected to WS2812B
#define LED_NUM 2                 // The number of LEDs (pixels) on WS2812B LED strip
#define RMT_RESOLUTION_HZ 8000000 // 8 MHz

typedef struct
{
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} ws2812_color_t;

// Function declarations
void ws2812_init(void);
void ws2812_send(ws2812_color_t *, size_t, float);

#endif