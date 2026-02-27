/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include "rgb_leds.h"

static const char *TAG = "RGB_LED";

// RGB LED handle
led_strip_handle_t rgb_leds = NULL;

static uint8_t led_brightness = 16;

// Local snapshot (copied by LED task)
static Statemachine_actuals_t sm_snap;

static bool blink0 = false;
// static bool blink1 = false;

const Color_t COLORS[] = {
    {255, 0, 0},     // RED
    {0, 255, 0},     // GREEN
    {0, 0, 255},     // BLUE
    {255, 255, 0},   // YELLOW
    {255, 0, 255},   // MAGENTA
    {0, 255, 255},   // CYAN
    {255, 255, 255}, // WHITE
    {255, 128, 0},   // ORANGE
    {128, 0, 128},   // PURPLE

    // --- Expanded palette ---
    {255, 64, 0},    // DEEP ORANGE
    {255, 165, 0},   // AMBER
    {255, 215, 0},   // GOLD
    {255, 105, 180}, // HOT PINK
    {255, 20, 147},  // DEEP PINK
    {135, 206, 235}, // SKY BLUE
    {0, 128, 128},   // TEAL
    {0, 100, 0},     // DARK GREEN
    {75, 0, 130},    // INDIGO
    {173, 216, 230}, // LIGHT BLUE
    {152, 255, 152}, // MINT
    {210, 105, 30},  // CHOCOLATE
    {46, 139, 87},   // SEA GREEN
    {0, 191, 255},   // DEEP SKY BLUE
    {123, 104, 238}, // MEDIUM SLATE BLUE
    {240, 128, 128}, // LIGHT CORAL
};

ColorName_t state_to_color(int state)
{
    switch (state)
    {
    case 0:
        return COLOR_RED; // Error
    case 1:
        return COLOR_GREEN; // Init
    case 2:
        return COLOR_YELLOW; // Day
    case 3:
        return COLOR_BLUE; // Night
    case 4:
        return COLOR_DEEP_SKY_BLUE; // High CO2 day
    case 5:
        return COLOR_SEA_GREEN; // High CO2 night
    case 6:
        return COLOR_DEEP_SKY_BLUE; // High RH day
    case 7:
        return COLOR_CHOCOLATE; // High RH night
    case 8:
        return COLOR_GOLD; // Cooking
    case 9:
        return COLOR_DEEP_ORANGE; // Cycling day
    case 10:
        return COLOR_HOT_PINK; // Cycling night
    case 11:
        return COLOR_INDIGO; // Manual fan high speed
    case 12:
        return COLOR_MEDIUM_SLATE_BLUE; // Stopped
    case 13:
        return COLOR_WHITE; // Temp
    case 14:
        return COLOR_TEAL; // Not used
    case 15:
        return COLOR_MAGENTA; // Not used
    default:
        return COLOR_PURPLE; // fallback
    }
}

ColorName_t fan_to_color(int fanspeed)
{
    switch (fanspeed)
    {
    case 0:
        return COLOR_RED; // Error
    case 1:
        return COLOR_GREEN; // Low
    case 2:
        return COLOR_YELLOW; // Medium
    case 3:
        return COLOR_DEEP_ORANGE; // High
    default:
        return COLOR_RED; // invalid
    }
}

esp_err_t init_rgb_led(void)
{
    led_strip_config_t cfg = {
        .strip_gpio_num = GPIO_RGB_LED,
        .max_leds = RGB_LED_NUM,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt = {.resolution_hz = 10 * 1000 * 1000};

    esp_err_t err = led_strip_new_rmt_device(&cfg, &rmt, &rgb_leds);
    LOGI(TAG, "LED strip initialized");
    return err;
}

// ---------------------------------------------------------------------------
// BRIGHTNESS HELPERS
// ---------------------------------------------------------------------------
static uint8_t apply_brightness(uint8_t c, uint8_t b)
{
    return (c * b) / 255;
}

void leds_set_brightness(uint8_t b)
{
    led_brightness = b;
}

// ---------------------------------------------------------------------------
// BASIC LED SET
// ---------------------------------------------------------------------------
void set_led_brightness(int led, ColorName_t color, uint8_t brightness)
{
    const Color_t *c = &COLORS[color];

    led_strip_set_pixel(
        rgb_leds,
        led,
        apply_brightness(c->r, brightness),
        apply_brightness(c->g, brightness),
        apply_brightness(c->b, brightness));
}

void set_led(int led, ColorName_t color)
{
    set_led_brightness(led, color, led_brightness);
}

void set_off(int led)
{
    led_strip_set_pixel(rgb_leds, led, 0, 0, 0);
}

// ---------------------------------------------------------------------------
// BLINK TOGGLE STATE
// ---------------------------------------------------------------------------
void toggle_led(int led, ColorName_t color, bool *state)
{
    *state = !*state;

    if (*state)
        set_led_brightness(led, color, led_brightness);
    else
        set_off(led);
}

// ---------------------------------------------------------------------------
// MAIN LED UPDATE (called by LED task)
// ---------------------------------------------------------------------------
void leds_update(const Statemachine_actuals_t *sm)
{
    // Copy snapshot
    sm_snap = *sm;

    // ---------------- LED0: STATE ----------------
    if (sm_snap.state == 0)
    {
        toggle_led(0, COLOR_RED, &blink0);
    }
    else if (sm_snap.valve_lock)
    {
        toggle_led(0, state_to_color(sm_snap.state), &blink0);
    }
    else
    {
        blink0 = false;
        set_led(0, state_to_color(sm_snap.state));
    }

    // --- LED1: Network status at startup, fanspeed with periodic blink when running ---
    static bool startup_mode = true;
    static bool blink_override = false;
    static bool net_toggle = false;

    static int startup_counter = 0;

    static uint32_t blink_start_ms = 0;
    static uint32_t last_blink_ms = 0;

    // Detect end of startup: after 5 seconds or both network interfaces checked
    if (startup_mode)
    {
        startup_counter++;
        if (startup_counter > 25)
        { // ~5s if leds_update called every 200ms
            startup_mode = false;
        }
    }

    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000); // ms

    ColorName_t net_color;

    if (sm_snap.eth_connected && sm_snap.wifi_connected)
    {
        // Alternate between Ethernet and WiFi color
        net_toggle = !net_toggle;
        net_color = net_toggle ? COLOR_MAGENTA : COLOR_BLUE;
    }
    else if (sm_snap.eth_connected)
    {
        net_color = COLOR_MAGENTA;
    }
    else if (sm_snap.wifi_connected)
    {
        net_color = COLOR_BLUE;
    }
    else
    {
        net_color = COLOR_RED;
    }

    if (startup_mode)
    {
        // Blink network status on LED1
        static bool blink_on = false;
        static uint32_t last_toggle = 0;
        if (now - last_toggle > 300)
        { // 300ms period
            blink_on = !blink_on;
            last_toggle = now;
        }
        if (blink_on)
            set_led(1, net_color);
        else
            set_off(1);
    }
    else
    {
        // Normal: fanspeed color, but blink network status every 30s
        if (!blink_override && (now - last_blink_ms > 30000))
        {
            blink_override = true;
            blink_start_ms = now;
            last_blink_ms = now;
        }
        if (blink_override)
        {
            set_led(1, net_color);
            if (now - blink_start_ms > 120)
            { // 120ms blink
                blink_override = false;
            }
        }
        else
        {
            set_led(1, fan_to_color(sm_snap.fanspeed));
        }
    }

    led_strip_refresh(rgb_leds);
}
