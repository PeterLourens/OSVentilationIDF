/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#ifndef RGB_LEDS_H
#define RGB_LEDS_H

#include <driver/i2c_master.h>
#include <esp_log.h>
#include <led_strip.h>

#include "globals.h"
#include "logging.h"

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

typedef struct
{
    int priority;
    bool (*condition)(const Statemachine_actuals_t *sm);
    void (*action)(const Statemachine_actuals_t *sm, uint8_t brightness);
} LedRule_t;

typedef enum
{
    COLOR_RED,
    COLOR_GREEN,
    COLOR_BLUE,
    COLOR_YELLOW,
    COLOR_MAGENTA,
    COLOR_CYAN,
    COLOR_WHITE,
    COLOR_ORANGE,
    COLOR_PURPLE,
    COLOR_DEEP_ORANGE,
    COLOR_AMBER,
    COLOR_GOLD,
    COLOR_HOT_PINK,
    COLOR_DEEP_PINK,
    COLOR_SKY_BLUE,
    COLOR_TEAL,
    COLOR_DARK_GREEN,
    COLOR_INDIGO,
    COLOR_LIGHT_BLUE,
    COLOR_MINT,
    COLOR_CHOCOLATE,
    COLOR_SEA_GREEN,
    COLOR_DEEP_SKY_BLUE,
    COLOR_MEDIUM_SLATE_BLUE,
    COLOR_LIGHT_CORAL,
    COLOR_COUNT
} ColorName_t;

typedef struct
{
    bool enabled;
    bool led_on;
    TickType_t next_toggle;
    uint32_t on_ms;
    uint32_t off_ms;
    ColorName_t color;
    uint8_t brightness;
} BlinkState_t;

extern const Color_t COLORS[COLOR_COUNT];

// Function declarations

esp_err_t init_rgb_led(void);
void set_led_brightness(int, ColorName_t, uint8_t);
void set_led(int, ColorName_t);
void set_off(int);
void leds_set_snapshot(const Statemachine_actuals_t *);
void leds_notify_update(void);
void leds_set_brightness(uint8_t);
void leds_update(const Statemachine_actuals_t *);

/*uint8_t apply_brightness(uint8_t, uint8_t);
void set_single_led_color_with_brightness(int, uint8_t, uint8_t, uint8_t, uint8_t);
void set_led_color(int, ColorName_t, uint8_t);
void blink_led(int, ColorName_t, uint8_t, uint32_t, uint32_t);
void blink_slow(int, ColorName_t, uint8_t);
void blink_fast(int, ColorName_t, uint8_t);
void blink_stop(int);
bool sm_get_snapshot(Statemachine_actuals_t *);
void rgb_leds_update(void);
void update_status_leds(const Statemachine_actuals_t *, uint8_t);*/

#endif