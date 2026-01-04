#include "rgb_leds.h"

static const char *TAG = "RGB LED";

// RGB LED handle
led_strip_handle_t rgb_leds = NULL;

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
        return COLOR_TEAL; // Manual fan high speed
    case 12:
        return COLOR_MEDIUM_SLATE_BLUE; // Stopped
    case 13:
        return COLOR_INDIGO; // Temp
    case 14:
        return COLOR_WHITE; // Not used
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
        return COLOR_CYAN; // High
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

    led_strip_rmt_config_t rmt = {
        .resolution_hz = 10 * 1000 * 1000,
    };

    esp_err_t err = led_strip_new_rmt_device(&cfg, &rmt, &rgb_leds);
    ESP_LOGI(TAG, "LED strip initialized");
    return err;
}

uint8_t apply_brightness(uint8_t c, uint8_t b)
{
    return (c * b) / 255;
}

void set_raw(int led, uint8_t r, uint8_t g, uint8_t b)
{
    if (led < 0 || led >= RGB_LED_NUM)
        return;
    led_strip_set_pixel(rgb_leds, led, r, g, b);
}

void set_led_color(int led, ColorName_t color, uint8_t brightness)
{
    const Color_t *c = &COLORS[color];
    set_raw(led,
            apply_brightness(c->r, brightness),
            apply_brightness(c->g, brightness),
            apply_brightness(c->b, brightness));
}

void set_led_off(int led)
{
    set_raw(led, 0, 0, 0);
}

void rgb_leds_refresh(void)
{
    led_strip_refresh(rgb_leds);
}

// ---------------------------------------------------------------------------
// NON-BLOCKING BLINK ENGINE
// ---------------------------------------------------------------------------
static BlinkState_t blink_state[RGB_LED_NUM] = {0};

void blink_start(int led, ColorName_t color, uint8_t brightness, uint32_t on_ms, uint32_t off_ms)
{
    blink_state[led] = (BlinkState_t){
        .enabled = true,
        .led_on = false,
        .next_toggle = xTaskGetTickCount(),
        .on_ms = on_ms,
        .off_ms = off_ms,
        .color = color,
        .brightness = brightness};
}

void blink_stop(int led)
{
    blink_state[led].enabled = false;
    set_led_off(led);
}

void blink_slow(int led, ColorName_t color, uint8_t brightness)
{
    blink_start(led, color, brightness, 500, 500);
}

void blink_fast(int led, ColorName_t color, uint8_t brightness)
{
    blink_start(led, color, brightness, 100, 100);
}

void rgb_leds_update(void)
{
    TickType_t now = xTaskGetTickCount();

    for (int i = 0; i < RGB_LED_NUM; i++)
    {
        BlinkState_t *bs = &blink_state[i];
        if (!bs->enabled)
            continue;

        if (now >= bs->next_toggle)
        {
            if (bs->led_on)
            {
                set_led_off(i);
                bs->led_on = false;
                bs->next_toggle = now + pdMS_TO_TICKS(bs->off_ms);
            }
            else
            {
                set_led_color(i, bs->color, bs->brightness);
                bs->led_on = true;
                bs->next_toggle = now + pdMS_TO_TICKS(bs->on_ms);
            }
        }
    }

    rgb_leds_refresh();
}

// ---------------------------------------------------------------------------
// BREATHING EFFECT (for WiFi AP mode)
// ---------------------------------------------------------------------------
static uint8_t breathe_level = 0;
static int breathe_dir = 1;

void led_breathe(int led, ColorName_t color)
{
    breathe_level += breathe_dir * 5;
    if (breathe_level >= 255)
    {
        breathe_level = 255;
        breathe_dir = -1;
    }
    if (breathe_level <= 10)
    {
        breathe_level = 10;
        breathe_dir = 1;
    }

    set_led_color(led, color, breathe_level);
}

// ---------------------------------------------------------------------------
// LED0 — STATE MACHINE
// ---------------------------------------------------------------------------
void update_led0(const Statemachine_actuals_t *sm, uint8_t brightness)
{
    // ESP_LOGI("LED0", "state=%d valve_lock=%d", sm->state, sm->valve_lock);

    if (sm->state == 0)
    {
        blink_fast(0, COLOR_RED, brightness);
        return;
    }

    if (sm->valve_lock)
    {
        ESP_LOGI("LED0", "VALVE LOCK → blink_slow()");
        blink_slow(0, state_to_color(sm->state), brightness);
        return;
    }

    blink_stop(0);
    set_led_color(0, state_to_color(sm->state), brightness);
}

// ---------------------------------------------------------------------------
// LED1 — WIFI (OVERRIDES FAN SPEED)
// ---------------------------------------------------------------------------
void update_led1(const Statemachine_actuals_t *sm, uint8_t brightness)
{
    // WiFi ERROR → slow red blink
    if (sm->wifi_error)
    {
        blink_slow(1, COLOR_RED, brightness);
        return;
    }

    // WiFi AP MODE → breathing yellow
    if (sm->wifi_ap_mode)
    {
        blink_stop(1);
        led_breathe(1, COLOR_YELLOW);
        return;
    }

    // WiFi NOT CONNECTED → fast cyan blink
    if (!sm->wifi_connected)
    {
        blink_fast(1, COLOR_CYAN, brightness);
        return;
    }

    // WiFi OK → FAN SPEED (steady)
    blink_stop(1);
    set_led_color(1, fan_to_color(sm->fanspeed), brightness);
}

void update_status_leds(const Statemachine_actuals_t *sm, uint8_t brightness)
{
    update_led0(sm, brightness);
    update_led1(sm, brightness);
}

bool sm_get_snapshot(Statemachine_actuals_t *out)
{
    if (xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(5)) != pdTRUE)
        return false;

    *out = sm_actuals;
    xSemaphoreGive(statemachine_mutex);
    return true;
}
