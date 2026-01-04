#include "rgb_leds.h"

static const char *TAG = "RGB LED";

// RGB LED handle
led_strip_handle_t rgb_leds = NULL;

static BlinkState_t blink_state[RGB_LED_NUM] = {0};

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
    led_strip_config_t rgb_led_config = {
        .strip_gpio_num = GPIO_RGB_LED,
        .max_leds = RGB_LED_NUM,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10 MHz
    };

    esp_err_t err = led_strip_new_rmt_device(&rgb_led_config, &rmt_config, &rgb_leds);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "LED strip init failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "LED strip initialized");
    return ESP_OK;
}

uint8_t apply_brightness(uint8_t color, uint8_t brightness)
{
    return (uint8_t)((color * brightness) / 255);
}

// Low-level: set raw RGB for a single LED, no refresh
void set_single_led_raw(int led_num, uint8_t r, uint8_t g, uint8_t b)
{
    if (led_num < 0 || led_num >= RGB_LED_NUM)
    {
        ESP_LOGW(TAG, "Invalid LED index: %d", led_num);
        return;
    }

    led_strip_set_pixel(rgb_leds, led_num, r, g, b);
}

// Public: set single LED by RGB with brightness, no refresh
void set_single_led_color_with_brightness(int led_num, uint8_t r, uint8_t g, uint8_t b, uint8_t brightness)
{
    set_single_led_raw(
        led_num,
        apply_brightness(r, brightness),
        apply_brightness(g, brightness),
        apply_brightness(b, brightness));
}

// Public: set LED by palette index, no refresh
void set_led_color(int led_num, ColorName_t color, uint8_t brightness)
{
    if (color < 0 || color >= COLOR_COUNT)
    {
        ESP_LOGW(TAG, "Invalid color index: %d", color);
        return;
    }

    const Color_t *c = &COLORS[color];
    set_single_led_color_with_brightness(led_num, c->r, c->g, c->b, brightness);
}

// Public: turn a single LED off, no refresh
void set_led_off(int led_num)
{
    set_single_led_raw(led_num, 0, 0, 0);
}

// Public: turn all LEDs off, with single refresh
void clear_all_leds(void)
{
    for (int i = 0; i < RGB_LED_NUM; ++i)
    {
        set_led_off(i);
    }
    led_strip_refresh(rgb_leds);
}

// Public: explicit refresh (caller decides when)
void rgb_leds_refresh(void)
{
    led_strip_refresh(rgb_leds);
}

// Blocking blink helper
void blink_led(int led_index, ColorName_t color,
               uint8_t brightness, uint32_t on_ms, uint32_t off_ms)
{
    // ON
    set_led_color(led_index, color, brightness);
    rgb_leds_refresh();
    vTaskDelay(pdMS_TO_TICKS(on_ms));

    // OFF
    set_led_off(led_index);
    rgb_leds_refresh();
    vTaskDelay(pdMS_TO_TICKS(off_ms));
}

void blink_start(int led, ColorName_t color, uint8_t brightness,
                 uint32_t on_ms, uint32_t off_ms)
{
    if (led < 0 || led >= RGB_LED_NUM)
        return;

    blink_state[led].enabled = true;
    blink_state[led].led_on = false;
    blink_state[led].next_toggle = xTaskGetTickCount();
    blink_state[led].on_ms = on_ms;
    blink_state[led].off_ms = off_ms;
    blink_state[led].color = color;
    blink_state[led].brightness = brightness;
}

void blink_slow(int led, ColorName_t color, uint8_t brightness)
{
    blink_start(led, color, brightness, 500, 500);
}

void blink_fast(int led, ColorName_t color, uint8_t brightness)
{
    blink_start(led, color, brightness, 100, 100);
}

void blink_stop(int led)
{
    if (led < 0 || led >= RGB_LED_NUM)
        return;

    blink_state[led].enabled = false;
    set_led_off(led);
}

bool sm_get_snapshot(Statemachine_actuals_t *out)
{
    if (xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(5)) != pdTRUE)
        return false;

    *out = sm_actuals; // atomic copy
    xSemaphoreGive(statemachine_mutex);
    return true;
}

void rgb_leds_update(void)
{
    TickType_t now = xTaskGetTickCount();

    for (int i = 0; i < RGB_LED_NUM; i++)
    {
        if (!blink_state[i].enabled)
            continue;

        if (now >= blink_state[i].next_toggle)
        {
            if (blink_state[i].led_on)
            {
                // turn OFF
                set_led_off(i);
                blink_state[i].led_on = false;
                blink_state[i].next_toggle = now + pdMS_TO_TICKS(blink_state[i].off_ms);
            }
            else
            {
                // turn ON
                set_led_color(i, blink_state[i].color, blink_state[i].brightness);
                blink_state[i].led_on = true;
                blink_state[i].next_toggle = now + pdMS_TO_TICKS(blink_state[i].on_ms);
            }
        }
    }

    // One refresh for all LEDs
    rgb_leds_refresh();
}

void update_status_leds(const Statemachine_actuals_t *sm, uint8_t brightness)
{
    int state = sm->state;
    int fanspeed = sm->fanspeed;
    bool valve_lock = sm->valve_lock;

    // ---------- LED 0: STATE MACHINE ----------
    if (state == 0)
    {
        // State 0 → fast red blink
        blink_fast(0, COLOR_RED, brightness);
    }
    else if (valve_lock)
    {
        // Valves moving → slow blink in state color
        blink_slow(0, state_to_color(state), brightness);
    }
    else
    {
        // Normal steady color
        blink_stop(0);
        set_led_color(0, state_to_color(state), brightness);
    }

    // ---------- LED 1: FAN SPEED ----------
    blink_stop(1); // fan LED never blinks
    set_led_color(1, fan_to_color(fanspeed), brightness);
}
