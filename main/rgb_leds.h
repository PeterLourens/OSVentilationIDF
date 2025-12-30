#ifndef RGB_LEDS_H
#define RGB_LEDS_H

#include <driver/i2c_master.h>
#include <esp_log.h>
#include <led_strip.h>

//#include "globals.h"

#define GPIO_RGB_LED 42           // The ESP32 pin GPIO10 connected to WS2812B
#define RGB_LED_NUM 2             // The number of LEDs (pixels) on WS2812B LED strip
#define RMT_RESOLUTION_HZ 8000000 // 8 MHz

// RGB LED handle
extern led_strip_handle_t rgb_leds;

typedef struct
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
} Color_t;

typedef enum
{
    COLOR_RED,
    COLOR_GREEN,
    COLOR_BLUE,
    COLOR_YELLOW,
    COLOR_MAGENTA,
    COLOR_CYAN,
    COLOR_WHITE,
    COLOR_GRAY,
    COLOR_ORANGE,
    COLOR_PURPLE,
    COLOR_COUNT
} ColorName_t;

extern const Color_t COLORS[COLOR_COUNT];

// Function declarations
esp_err_t init_rgb_led(void);
uint8_t apply_brightness(uint8_t, uint8_t);
void set_single_led_color_with_brightness(int, uint8_t, uint8_t, uint8_t, uint8_t);
void set_led_color(int, ColorName_t, uint8_t);

#endif