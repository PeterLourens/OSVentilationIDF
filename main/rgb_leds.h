#ifndef RGB_LEDS_H
#define RGB_LEDS_H

#include <stdio.h>

#include "globals.h"


#define GPIO_RGB_LED 42         // The ESP32 pin GPIO10 connected to WS2812B
#define RGB_LED_NUM 2                 // The number of LEDs (pixels) on WS2812B LED strip
#define RMT_RESOLUTION_HZ 8000000 // 8 MHz

// Function declarations
esp_err_t init_rgb_led(void);
uint8_t apply_brightness(uint8_t, uint8_t);
void set_single_led_color_with_brightness(int, uint8_t, uint8_t, uint8_t, uint8_t);

#endif