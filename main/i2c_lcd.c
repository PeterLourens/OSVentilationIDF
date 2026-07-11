/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

/**
 * @file i2c_lcd.c
 * @brief LCD display support using a PCF8574 I2C expander and HD44780-compatible commands.
 */

#include "i2c_lcd.h"

static const char *TAG = "i2c_LCD";

// PCF8574 / LCD bits
#define LCD_BACKLIGHT 0x08
#define LCD_ENABLE_BIT 0x04
#define LCD_READ_WRITE_BIT 0x02
#define LCD_REGISTER_SELECT 0x01

// Timeout for i2c operations
#define LCD_I2C_TIMEOUT_TICKS pdMS_TO_TICKS(200)

// LCD geometry
static const int LCD_COLS = 20;
static const int LCD_ROWS = 4;
static const uint8_t row_offsets[] = {0x00, 0x40, 0x14, 0x54};
static bool s_backlight = true;
static uint8_t s_last_output = LCD_BACKLIGHT; // default include backlight

// file-scope mutex for LCD state (backlight)
static SemaphoreHandle_t s_lcd_state_mutex = NULL;

/**
 * @brief Add and bind the LCD expander device to the specified I2C bus.
 *
 * @param bus_handle Handle for the I2C bus to attach the device to.
 * @param dev_handle Pointer to the device handle that will be populated on success.
 * @return ESP_OK on success, or an esp_err_t if device setup fails.
 */
esp_err_t i2c_lcd_init(i2c_master_bus_handle_t bus_handle, i2c_master_dev_handle_t *dev_handle)
{
    i2c_device_config_t lcd_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = LCD_I2C_ADDR,
        .scl_speed_hz = 100000,
    };

    esp_err_t err = i2c_master_bus_add_device(bus_handle, &lcd_cfg, dev_handle);
    if (err != ESP_OK)
    {
        LOGE(TAG, "Failed to add LCD device: %s", esp_err_to_name(err));
        return err;
    }

    // Ensure the global device handle is set so other code can use it
    lcd_bus1_handle = *dev_handle;
    LOGD(TAG, "LCD device bound to global handle");
    return ESP_OK;
}

// Low-level write: single-byte write to PCF8574 via dev_handle
/**
 * @brief Write a single command or data byte to the LCD expander.
 *
 * @param data Raw byte to transmit to PCF8574.
 * @return ESP_OK on success, or an esp_err_t if the I2C transfer fails.
 */
esp_err_t lcd_dev_write_byte(uint8_t data)
{
    if (lcd_bus1_handle == NULL)
    {
        LOGE(TAG, "lcd_dev_write_byte: lcd_bus1_handle == NULL");
        return ESP_ERR_INVALID_STATE;
    }

    const int max_retries = 2;
    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt <= max_retries; ++attempt)
    {
        err = i2c_master_transmit(lcd_bus1_handle, &data, 1, LCD_I2C_TIMEOUT_TICKS);
        if (err == ESP_OK)
        {
            return ESP_OK;
        }
        LOGW(TAG, "lcd_dev_write_byte attempt %d failed: %s", attempt + 1, esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(5 * (attempt + 1)));
    }

    LOGE(TAG, "lcd_dev_write_byte final failure: %s", esp_err_to_name(err));
    return err;
}

// Pulse enable line (data must include RS/RW/backlight) */
/**
 * @brief Pulse the HD44780 enable line through the expander.
 *
 * @param data Data/control byte including RS/RW/backlight bits.
 * @return ESP_OK on success, or an esp_err_t if the enable pulse fails.
 */
esp_err_t lcd_pulse_enable(uint8_t data)
{
    esp_err_t err = lcd_dev_write_byte(data | LCD_ENABLE_BIT);
    if (err != ESP_OK)
        return err;
    esp_rom_delay_us(1);
    err = lcd_dev_write_byte(data & ~LCD_ENABLE_BIT);
    if (err != ESP_OK)
        return err;
    esp_rom_delay_us(50);
    return ESP_OK;
}

/**
 * @brief Write a 4-bit nibble to the LCD controller.
 *
 * @param nibble Nibble value to send in the upper data half-byte.
 * @param control_flags RS/RW/backlight control flags.
 * @return ESP_OK on success, or an esp_err_t if transmission fails.
 */
esp_err_t lcd_write_nibble(uint8_t nibble, uint8_t control_flags)
{
    uint8_t out = (uint8_t)((nibble & 0x0F) << 4);
    out |= control_flags;
    if (s_backlight)
        out |= LCD_BACKLIGHT;
    s_last_output = out;
    return lcd_pulse_enable(out);
}

static esp_err_t lcd_write_byte(uint8_t byte, uint8_t control_flags)
{
    esp_err_t err = lcd_write_nibble((byte >> 4) & 0x0F, control_flags);
    if (err != ESP_OK)
        return err;
    return lcd_write_nibble(byte & 0x0F, control_flags);
}

static inline esp_err_t lcd_command(uint8_t cmd)
{
    return lcd_write_byte(cmd, 0x00);
}

static inline esp_err_t lcd_data(uint8_t d)
{
    return lcd_write_byte(d, LCD_REGISTER_SELECT);
}

/**
 * @brief Initialize the HD44780-compatible LCD through the PCF8574 expander.
 *
 * This performs the required 4-bit initialization sequence and configures the display.
 * @return ESP_OK on success, or an esp_err_t if initialization fails.
 */
esp_err_t i2c_lcd_init_device(void)
{
    if (lcd_bus1_handle == NULL)
    {
        LOGE(TAG, "i2c_lcd_init_device: no LCD device bound");
        return ESP_ERR_INVALID_STATE;
    }

    vTaskDelay(pdMS_TO_TICKS(50)); // power-up delay

    // Ensure state mutex exists if (s_lcd_state_mutex == NULL)
    {
        s_lcd_state_mutex = xSemaphoreCreateMutex();
        if (s_lcd_state_mutex == NULL)
        {
            LOGW(TAG, "i2c_lcd_init_device: failed to create s_lcd_state_mutex");
        }
    }

    // Best-effort backlight write and reflect cached state
    esp_err_t r = lcd_dev_write_byte(LCD_BACKLIGHT);
    if (r != ESP_OK)
    {
        LOGW(TAG, "Initial backlight write failed: %s", esp_err_to_name(r));
    }
    else
    {
        s_last_output = LCD_BACKLIGHT;
        s_backlight = true;
    }

    // Init sequence: 3x 0x03 then 0x02 to set 4-bit mode
    if (lcd_write_nibble(0x03, 0) != ESP_OK)
        return ESP_FAIL;
    vTaskDelay(pdMS_TO_TICKS(5));
    if (lcd_write_nibble(0x03, 0) != ESP_OK)
        return ESP_FAIL;
    esp_rom_delay_us(150);
    if (lcd_write_nibble(0x03, 0) != ESP_OK)
        return ESP_FAIL;
    esp_rom_delay_us(150);
    if (lcd_write_nibble(0x02, 0) != ESP_OK)
        return ESP_FAIL;
    esp_rom_delay_us(150);

    if (lcd_command(0x28) != ESP_OK)
        return ESP_FAIL; // Function set
    if (lcd_command(0x0C) != ESP_OK)
        return ESP_FAIL; // Display on
    if (lcd_command(0x01) != ESP_OK)
        return ESP_FAIL; // Clear
    vTaskDelay(pdMS_TO_TICKS(2));
    if (lcd_command(0x06) != ESP_OK)
        return ESP_FAIL; // Entry mode

    LOGD(TAG, "LCD initialized (device bound)");
    return ESP_OK;
}

/**
 * @brief Clear the LCD display.
 *
 * @return ESP_OK on success, or an esp_err_t if the clear command fails.
 */
esp_err_t i2c_lcd_clear(void)
{
    esp_err_t err = lcd_command(0x01);
    vTaskDelay(pdMS_TO_TICKS(2));
    return err;
}

/**
 * @brief Position the LCD cursor to the requested row and column.
 *
 * @param row Zero-based row index.
 * @param col Zero-based column index.
 * @return ESP_OK on success, or an esp_err_t if the cursor position command fails.
 */
esp_err_t i2c_lcd_set_cursor(int row, int col)
{
    if (row < 0)
        row = 0;
    if (row >= LCD_ROWS)
        row = LCD_ROWS - 1;
    if (col < 0)
        col = 0;
    if (col >= LCD_COLS)
        col = LCD_COLS - 1;
    uint8_t addr = row_offsets[row] + (uint8_t)col;
    return lcd_command(0x80 | addr);
}

/**
 * @brief Write a text string to the LCD display.
 *
 * This function handles line wrapping and newline characters across the configured display geometry.
 * @param text Null-terminated string to display.
 * @return ESP_OK on success, or an esp_err_t if writing fails.
 */
esp_err_t i2c_lcd_write_text(const char *text)
{
    if (!text)
    {
        return ESP_ERR_INVALID_ARG;
    }

    // Prepare a local buffer sized to LCD geometry
    char buf[LCD_ROWS][LCD_COLS];
    for (int r = 0; r < LCD_ROWS; ++r)
        for (int c = 0; c < LCD_COLS; ++c)
            buf[r][c] = ' ';

    int row = 0, col = 0;
    for (const char *p = text; *p && row < LCD_ROWS; ++p)
    {
        if (*p == '\n')
        {
            row++;
            col = 0;
            continue;
        }
        buf[row][col++] = *p;
        if (col >= LCD_COLS)
        {
            row++;
            col = 0;
        }
    }

    // Write rows with error handling
    for (int r = 0; r < LCD_ROWS; ++r)
    {
        esp_err_t err = i2c_lcd_set_cursor(r, 0);
        if (err != ESP_OK)
        {
            LOGW(TAG, "set_cursor failed for row %d: %s", r, esp_err_to_name(err));
            return err;
        }
        for (int c = 0; c < LCD_COLS; ++c)
        {
            err = lcd_data((uint8_t)buf[r][c]);
            if (err != ESP_OK)
            {
                LOGW(TAG, "lcd_data failed at r%d c%d: %s", r, c, esp_err_to_name(err));
                return err;
            }
        }
    }
    return ESP_OK;
}

// Internal helper: write the current output byte (s_last_output) but ensure backlight bit matches s_backlight
/**
 * @brief Update the PCF8574 expander with the current backlight state.
 *
 * @return ESP_OK on success, or an esp_err_t if the I2C write fails.
 */
esp_err_t lcd_write_backlight_state(void)
{
    uint8_t out = s_last_output;
    if (s_backlight)
        out |= LCD_BACKLIGHT;
    else
        out &= (uint8_t)~LCD_BACKLIGHT;

    // Write as a normal expander byte (no enable pulse) to update backlight immediately
    // We still use lcd_dev_write_byte so behavior/error handling is consistent
    return lcd_dev_write_byte(out);
}

// Ensure a mutex is created once at system init if possible.
// This function lazily creates a mutex if none exists.
/**
 * @brief Enable or disable the LCD backlight.
 *
 * @param on true to turn backlight on, false to turn it off.
 * @return ESP_OK on success, or an esp_err_t if the backlight state cannot be written.
 */
esp_err_t i2c_lcd_backlight_set(bool on)
{
    if (s_lcd_state_mutex == NULL)
    {
        // If mutex not created for some reason, try best-effort write but log
        LOGW(TAG, "i2c_lcd_backlight_set: state mutex not created; doing best-effort");
        s_backlight = on;
        return lcd_write_backlight_state();
    }

    if (xSemaphoreTake(s_lcd_state_mutex, pdMS_TO_TICKS(50)) != pdTRUE)
    {
        LOGW(TAG, "Could not take s_lcd_state_mutex; skipping backlight update");
        return ESP_ERR_TIMEOUT;
    }

    s_backlight = on;
    esp_err_t r = lcd_write_backlight_state();
    xSemaphoreGive(s_lcd_state_mutex);

    if (r != ESP_OK)
    {
        LOGW(TAG, "lcd_write_backlight_state failed: %s", esp_err_to_name(r));
    }
    return r;
}

// Public: read current cached backlight state
/**
 * @brief Get the current cached LCD backlight state.
 *
 * @return true if the backlight is currently enabled, false otherwise.
 */
bool i2c_lcd_backlight_get(void)
{
    return s_backlight;
}

/**
 * @brief Format the sensor overview text for the LCD display.
 *
 * @param out Output buffer for the formatted string.
 * @param out_size Maximum number of bytes available in the output buffer.
 * @param bus Sensor bus index.
 * @param ch Sensor channel index.
 * @param avg Average sensor readings array.
 */
void format_sensor_overview(char *out, size_t out_size, int bus, int ch, float avg[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS])
{
    if (!out || out_size == 0)
        return;

    // Validate indices
    if (bus < 0 || bus >= MAX_I2C_BUSES || ch < 0 || ch >= CHANNELS_PER_BUS)
    {
        snprintf(out, out_size, "b%ds%d <invalid index>", bus, ch);
        return;
    }

    bus_sensor_t meta;
    memset(&meta, 0, sizeof(meta));
    strncpy(meta.location, "<unknown>", sizeof(meta.location) - 1);
    strncpy(meta.valve, "valve0", sizeof(meta.valve) - 1);
    meta.rh_enabled = false;
    meta.co2_enabled = false;

    // Read metadata safely
    if (sensor_cfg_mutex && xSemaphoreTake(sensor_cfg_mutex, pdMS_TO_TICKS(20)) == pdTRUE)
    {
        const bus_sensor_t *src = (bus == 0) ? &bus0_sensor_settings[ch] : &bus1_sensor_settings[ch];
        meta = *src;
        xSemaphoreGive(sensor_cfg_mutex);
    }
    else
    {
        LOGW(TAG, "Could not take sensor_cfg_mutex; using defaults for sensor b%ds%d", bus, ch);
    }

    // Extract numeric valve index from meta.valve
    int valve_num = 0;
    const char *p = meta.valve;
    while (*p && !isdigit((unsigned char)*p))
        p++;
    if (*p)
        valve_num = atoi(p);

    // Read sensor values
    float temp_c = avg[bus][ch][IDX_TEMP];
    float humidity = avg[bus][ch][IDX_HUMIDITY];
    float co2_ppm = avg[bus][ch][IDX_CO2];

    int ret;
    if (co2_ppm > 0.0f)
    {
        ret = snprintf(out, out_size,
                       "b%ds%d %s v%d\n"
                       "T:%.2f\xDF"
                       "C H:%.2f%%\n"
                       "RH: %s CO2: %s\n"
                       "CO2:%.2fppm",
                       bus, ch,
                       meta.location,
                       valve_num,
                       temp_c,
                       humidity,
                       meta.rh_enabled ? "On" : "Off",
                       meta.co2_enabled ? "On" : "Off",
                       co2_ppm);
    }
    else
    {
        ret = snprintf(out, out_size,
                       "b%ds%d %s v%d\n"
                       "T:%.2f\xDF"
                       "C H:%.2f%%\n"
                       "RH: %s CO2: %s",
                       bus, ch,
                       meta.location,
                       valve_num,
                       temp_c,
                       humidity,
                       meta.rh_enabled ? "On" : "Off",
                       meta.co2_enabled ? "On" : "Off");
    }

    if (ret < 0)
    {
        LOGW(TAG, "format_sensor_overview snprintf error");
        out[0] = '\0';
    }
    else if ((size_t)ret >= out_size)
    {
        LOGW(TAG, "format_sensor_overview output truncated (needed %d, buf %zu)", ret, out_size);
        out[out_size - 1] = '\0';
    }
}

/**
 * @brief Build a formatted valve position status string for LCD output.
 *
 * @param buf Output buffer for the formatted valve positions.
 * @param buf_size Size of the output buffer.
 */
void build_valve_positions_buffer(char *buf, size_t buf_size)
{
    nvs_handle_t nvs;

    esp_err_t err = nvs_open("valves", NVS_READONLY, &nvs);

    if (err != ESP_OK)
    {
        LOGE(TAG, "Failed to open NVS for valve positions: %s", esp_err_to_name(err));
        buf[0] = '\0';
        return;
    }

    size_t used = 0;

    for (int i = 0; i < 12; i++)
    {
        char key[16];
        snprintf(key, sizeof(key), "valve%d", i);

        int32_t pos = 0;
        if (nvs_get_i32(nvs, key, &pos) != ESP_OK)
            pos = 0;

        // Insert newline every 3 valves
        if (i > 0 && (i % 3) == 0)
        {
            int w = snprintf(buf + used, buf_size - used, "\n");
            if (w < 0 || w >= (int)(buf_size - used))
                break;
            used += w;
        }

        // Write vX:pos
        int w = snprintf(buf + used, buf_size - used, "v%d:%ld", i, pos);
        if (w < 0 || w >= (int)(buf_size - used))
            break;
        used += w;

        // Add trailing space for all except last valve (v11)
        if (i < 11)
        {
            if (used < buf_size - 1)
                buf[used++] = ' ';
            else
                break;
        }
    }

    nvs_close(nvs);

    // Null‑terminate
    if (used < buf_size)
        buf[used] = '\0';
    else
        buf[buf_size - 1] = '\0';
}

/**
 * @brief Build a formatted statemachine actuals string for LCD output.
 *
 * @param buf Output buffer for the formatted status text.
 * @param buf_size Size of the output buffer.
 */
void build_sm_actuals_buffer(char *buf, size_t buf_size)
{
    Statemachine_actuals_t snap;
    size_t used = 0;

    // Take snapshot under mutex
    if (xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(20)) == pdTRUE)
    {
        snap = sm_actuals;
        xSemaphoreGive(statemachine_mutex);
    }
    else
    {
        // Fallback defaults if mutex not available
        memset(&snap, 0, sizeof(snap));
    }

    // Line 1: State
    const char *state_str = state_nr_to_str((state_nr_t)snap.state);
    int w = snprintf(buf + used, buf_size - used, "STATE:%-12s\n", state_str);
    if (w < 0 || w >= (int)(buf_size - used))
        return;
    used += w;

    // Line 2: Fan speed
    const char *fan_str = fan_speed_to_str((fan_speed_t)snap.fanspeed);
    w = snprintf(buf + used, buf_size - used, "FAN:   %-12s\n", fan_str);
    if (w < 0 || w >= (int)(buf_size - used))
        return;
    used += w;

    // Line 3: CO2 sensors high
    w = snprintf(buf + used, buf_size - used, "CO2 HIGH: %2d\n", snap.co2_sensors_high);
    if (w < 0 || w >= (int)(buf_size - used))
        return;
    used += w;

    // Line 4: RH sensors high
    w = snprintf(buf + used, buf_size - used, "RH HIGH:  %2d", snap.rh_sensors_high);
    if (w < 0 || w >= (int)(buf_size - used))
        return;
    used += w;

    // Null‑terminate
    if (used < buf_size)
        buf[used] = '\0';
    else
        buf[buf_size - 1] = '\0';
}