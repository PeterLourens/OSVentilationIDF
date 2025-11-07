#include "valvecontrol.h"

/* Patterns (as provided) */
static const uint8_t pattern1[4] = {0b00000101, 0b00001001, 0b00001010, 0b00000110};
static const uint8_t pattern2[4] = {0b01010000, 0b10010000, 0b10100000, 0b01100000};

static const char *TAG = "Valvecontrol";

/* ---------------------- Generic helpers ---------------------- */

/* Configure three GPIOs as outputs (idempotent). Returns ESP_OK or error. */
esp_err_t gpio_setup_3pins(int dataPin, int clockPin, int latchPin)
{
    if (dataPin < 0 || clockPin < 0 || latchPin < 0)
        return ESP_ERR_INVALID_ARG;

    gpio_config_t cfg = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << dataPin) | (1ULL << clockPin) | (1ULL << latchPin),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    return gpio_config(&cfg);
}

/* short microsecond delay using ROM API */
void short_delay_us(int us)
{
    if (us > 0)
        esp_rom_delay_us(us);
}

/* Shift out one byte MSB-first (equivalent to Arduino shiftOut MSBFIRST) */
void shift_out_msbfist(int dataPin, int clockPin, uint8_t val)
{
    for (int bit = 7; bit >= 0; --bit)
    {
        int bitval = (val >> bit) & 1;
        gpio_set_level(dataPin, bitval);
        short_delay_us(1);
        gpio_set_level(clockPin, 1);
        short_delay_us(1);
        gpio_set_level(clockPin, 0);
        short_delay_us(1);
    }
}

/* Latch pulse: generate a short RCLK rising edge (caller may set latch low beforehand) */
void latch_pulse(int latchPin)
{
    gpio_set_level(latchPin, 1);
    short_delay_us(1);
    gpio_set_level(latchPin, 0);
    short_delay_us(1);
}

/* ---------------------- all_outputs_off ---------------------- */

/* Shift three zero bytes (nearest first) and latch — mirrors Arduino reference */
void all_outputs_off(int dataPin, int clockPin, int latchPin)
{
    if (gpio_setup_3pins(dataPin, clockPin, latchPin) != ESP_OK)
        return;

    /* follow original order: shift output[2], output[1], output[0] */
    gpio_set_level(latchPin, 0);
    shift_out_msbfist(dataPin, clockPin, 0x00); // nearest chip
    shift_out_msbfist(dataPin, clockPin, 0x00); // middle chip
    shift_out_msbfist(dataPin, clockPin, 0x00); // farthest chip
    latch_pulse(latchPin);
}

/*
 * direction: 1 = open (forward), 0 = close (backward)
 * position_change: 0..24 (1 => 24 cycles as in original)
 * valve_number: 0..11 (values >=6 are remapped as original)
 * dataPin, clockPin, latchPin: GPIO pins for the chain
 */

void valvecontrol(int direction, int position_change, int valve_number, int dataPin, int clockPin, int latchPin)
{
    int output[3][4] = {{0}};
    int i, j, k;
    const int cycles = 24; // cycles per full rotation (same as original)

    /* Validate direction and position_change like original */
    if (!(direction == 1 || direction == 0))
    {
        ESP_LOGE(TAG, "Invalid direction: %d", direction);
        return;
    }
    if (position_change < 0 || position_change > 24)
    {
        ESP_LOGE(TAG, "Error in valvecontrol. Requested position change is: %d. Position change should be between 0 and 24", position_change);
        return;
    }

    /* Take lock_valve_move mutex and set flag like original (if available) */
    if (lock_valve_move_mutex)
    {
        if (xSemaphoreTake(lock_valve_move_mutex, (TickType_t)10) == pdTRUE)
        {
            lock_valve_move = 1;
            xSemaphoreGive(lock_valve_move_mutex);
        }
    }

    /* Normalize valve number same as original */
    if (valve_number >= 6)
        valve_number -= 6;

    /* Build outputs exactly like Arduino switch-case */
    switch (valve_number)
    {
    case 0:
        for (i = 0; i < 4; i++)
        {
            output[0][i] = pattern1[i];
            output[1][i] = 0;
            output[2][i] = 0;
        }
        break;
    case 1:
        for (i = 0; i < 4; i++)
        {
            output[0][i] = pattern2[i];
            output[1][i] = 0;
            output[2][i] = 0;
        }
        break;
    case 2:
        for (i = 0; i < 4; i++)
        {
            output[0][i] = 0;
            output[1][i] = pattern1[i];
            output[2][i] = 0;
        }
        break;
    case 3:
        for (i = 0; i < 4; i++)
        {
            output[0][i] = 0;
            output[1][i] = pattern2[i];
            output[2][i] = 0;
        }
        break;
    case 4:
        for (i = 0; i < 4; i++)
        {
            output[0][i] = 0;
            output[1][i] = 0;
            output[2][i] = pattern1[i];
        }
        break;
    case 5:
        for (i = 0; i < 4; i++)
        {
            output[0][i] = 0;
            output[1][i] = 0;
            output[2][i] = pattern2[i];
        }
        break;
    default:
        /* shouldn't happen */
        break;
    }

    /* Configure GPIO pins */
    if (gpio_setup_3pins(dataPin, clockPin, latchPin) != ESP_OK)
    {
        ESP_LOGE(TAG, "GPIO config failed in valvecontrol");
        if (lock_valve_move_mutex)
        {
            if (xSemaphoreTake(lock_valve_move_mutex, (TickType_t)10) == pdTRUE)
            {
                lock_valve_move = 0;
                xSemaphoreGive(lock_valve_move_mutex);
            }
        }
        return;
    }

    /* Ensure pins start low */
    gpio_set_level(dataPin, 0);
    gpio_set_level(clockPin, 0);
    gpio_set_level(latchPin, 0);

    /* Forward / open */
    if (direction == 1)
    {
        for (j = 0; j < (cycles * position_change); j++)
        {
            for (k = 0; k < 4; k++)
            {
                gpio_set_level(latchPin, 0);
                shift_out_msbfist(dataPin, clockPin, (uint8_t)output[2][k]);
                shift_out_msbfist(dataPin, clockPin, (uint8_t)output[1][k]);
                shift_out_msbfist(dataPin, clockPin, (uint8_t)output[0][k]);
                gpio_set_level(latchPin, 1);
                /* match original delay */
                vTaskDelay(pdMS_TO_TICKS(10));
                gpio_set_level(latchPin, 0);
            }
        }
        all_outputs_off(dataPin, clockPin, latchPin);
    }
    /* Backward / close */
    else
    {
        for (j = 0; j < (cycles * position_change); j++)
        {
            for (k = 3; k >= 0; k--)
            {
                gpio_set_level(latchPin, 0);
                shift_out_msbfist(dataPin, clockPin, (uint8_t)output[2][k]);
                shift_out_msbfist(dataPin, clockPin, (uint8_t)output[1][k]);
                shift_out_msbfist(dataPin, clockPin, (uint8_t)output[0][k]);
                gpio_set_level(latchPin, 1);
                vTaskDelay(pdMS_TO_TICKS(10));
                gpio_set_level(latchPin, 0);
            }
        }
        all_outputs_off(dataPin, clockPin, latchPin);
    }

    /* Release lock flag */
    if (lock_valve_move_mutex)
    {
        if (xSemaphoreTake(lock_valve_move_mutex, (TickType_t)10) == pdTRUE)
        {
            lock_valve_move = 0;
            xSemaphoreGive(lock_valve_move_mutex);
        }
    }
}