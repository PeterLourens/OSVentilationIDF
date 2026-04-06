/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
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

// Keep track of current backlight state and last static output byte
// last_output holds the last non-toggling control byte we sent to the expander.
// We update it when sending commands/data so toggling backlight doesn't lose RS/E state.
static bool s_backlight = true;
static uint8_t s_last_output = LCD_BACKLIGHT; /* default include backlight */

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

// Initialize the HD44780 via PCF8574 (4-bit init sequence), call once in task_i2c_lcd
esp_err_t i2c_lcd_init_device(void)
{
    if (lcd_bus1_handle == NULL)
    {
        LOGE(TAG, "i2c_lcd_init_device: no LCD device bound");
        return ESP_ERR_INVALID_STATE;
    }

    vTaskDelay(pdMS_TO_TICKS(50)); // power-up delay

    // Best-effort backlight write
    esp_err_t r = lcd_dev_write_byte(LCD_BACKLIGHT);
    if (r != ESP_OK)
    {
        LOGW(TAG, "Initial backlight write failed: %s", esp_err_to_name(r));
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

esp_err_t i2c_lcd_clear(void)
{
    esp_err_t err = lcd_command(0x01);
    vTaskDelay(pdMS_TO_TICKS(2));
    return err;
}

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

// Write a C string to display (handles '\n' and wrapping). Pads/truncates to 20 cols per row
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
esp_err_t i2c_lcd_backlight_set(bool on)
{
    static SemaphoreHandle_t lcd_state_mutex_local = NULL;
    if (lcd_state_mutex_local == NULL)
    {
        lcd_state_mutex_local = xSemaphoreCreateMutex();
        if (lcd_state_mutex_local == NULL)
        {
            LOGW(TAG, "Failed to create lcd_state_mutex_local; proceeding without mutex");
            s_backlight = on;
            return lcd_write_backlight_state();
        }
    }

    if (xSemaphoreTake(lcd_state_mutex_local, pdMS_TO_TICKS(50)) != pdTRUE)
    {
        LOGW(TAG, "Could not take lcd_state_mutex_local; skipping backlight update");
        return ESP_ERR_TIMEOUT;
    }

    s_backlight = on;
    esp_err_t r = lcd_write_backlight_state();
    xSemaphoreGive(lcd_state_mutex_local);

    if (r != ESP_OK)
    {
        LOGW(TAG, "lcd_write_backlight_state failed: %s", esp_err_to_name(r));
    }
    return r;
}

// Public: read current cached backlight state
bool i2c_lcd_backlight_get(void)
{
    return s_backlight;
}

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