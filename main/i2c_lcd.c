#include "i2c_lcd.h"

static const char *TAG = "LCD";

/* PCF8574 / LCD bits */
#define LCD_BACKLIGHT 0x08
#define LCD_ENABLE_BIT 0x04
#define LCD_READ_WRITE_BIT 0x02
#define LCD_REGISTER_SELECT 0x01

/* Timeout for i2c operations */
#define LCD_I2C_TIMEOUT_TICKS pdMS_TO_TICKS(200)

/* LCD geometry */
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
        .device_address = LCD_I2C_ADDR, // 0x27
        .scl_speed_hz = 100000,
    };

    esp_err_t err = i2c_master_bus_add_device(bus_handle, &lcd_cfg, dev_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to add LCD device: %s", esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}

/* Low-level write: single-byte write to PCF8574 via dev_handle */
esp_err_t lcd_dev_write_byte(uint8_t data)
{
    if (lcd_bus1_handle == NULL)
    {
        ESP_LOGE(TAG, "LCD device not bound");
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_transmit(lcd_bus1_handle, &data, 1, LCD_I2C_TIMEOUT_TICKS);
}

/* Pulse enable line (data must include RS/RW/backlight) */
esp_err_t lcd_pulse_enable(uint8_t data)
{
    esp_err_t err = lcd_dev_write_byte(data | LCD_ENABLE_BIT);
    if (err != ESP_OK)
        return err;
    esp_rom_delay_us(1);
    err = lcd_dev_write_byte(data & ~LCD_ENABLE_BIT);
    if (err != ESP_OK)
        return err;
    esp_rom_delay_us(50); // allow command to settle
    return ESP_OK;
}

/* Send nibble (lower 4 bits). Many backpacks map nibble -> P7..P4 */
esp_err_t lcd_write_nibble(uint8_t nibble, uint8_t control_flags)
{
    uint8_t out = (uint8_t)((nibble & 0x0F) << 4);
    out |= control_flags;

    if (s_backlight)
        out |= LCD_BACKLIGHT;

    s_last_output = out; // Remember last stable output used (without E set)

    return lcd_pulse_enable(out);
}

/* Send full byte as two nibbles */
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

/* Bind the device handle returned by your bus API - Function not used and it works ok without
esp_err_t i2c_lcd_bind_device(i2c_master_dev_handle_t dev_handle)
{
    if (dev_handle == NULL)
        return ESP_ERR_INVALID_ARG;
    lcd_bus1_handle = dev_handle;
    return ESP_OK;
}*/

/* Initialize the HD44780 via PCF8574 (4-bit init sequence), call once in task_i2c_lcd */
esp_err_t i2c_lcd_init_device(void)
{
    if (lcd_bus1_handle == NULL)
    {
        ESP_LOGE(TAG, "No LCD device bound");
        return ESP_ERR_INVALID_STATE;
    }

    vTaskDelay(pdMS_TO_TICKS(50)); // power-up delay

    (void)lcd_dev_write_byte(LCD_BACKLIGHT); // optional quick backlight write (ignore error)

    /* Init sequence: 3x 0x03 then 0x02 to set 4-bit mode */
    lcd_write_nibble(0x03, 0);
    vTaskDelay(pdMS_TO_TICKS(5));
    lcd_write_nibble(0x03, 0);
    esp_rom_delay_us(150);
    lcd_write_nibble(0x03, 0);
    esp_rom_delay_us(150);
    lcd_write_nibble(0x02, 0);
    esp_rom_delay_us(150);

    lcd_command(0x28); // Function set: 4-bit, 2 lines, 5x8 dots
    lcd_command(0x0C); // Display on, cursor off, blink off
    lcd_command(0x01); // Clear display
    vTaskDelay(pdMS_TO_TICKS(2));
    lcd_command(0x06); // Entry mode set: increment, no shift

    ESP_LOGI(TAG, "LCD initialized (device bound)");
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

/* Write a C string to display (handles '\n' and wrapping). Pads/truncates to 20 cols per row */
esp_err_t i2c_lcd_write_text(const char *text)
{
    if (!text)
        return ESP_ERR_INVALID_ARG;

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

    for (int r = 0; r < LCD_ROWS; ++r)
    {
        esp_err_t err = i2c_lcd_set_cursor(r, 0);
        if (err != ESP_OK)
            return err;
        for (int c = 0; c < LCD_COLS; ++c)
        {
            err = lcd_data((uint8_t)buf[r][c]);
            if (err != ESP_OK)
                return err;
        }
    }
    return ESP_OK;
}

/* Internal helper: write the current output byte (s_last_output) but ensure backlight bit matches s_backlight */
esp_err_t lcd_write_backlight_state(void)
{
    uint8_t out = s_last_output;
    if (s_backlight)
        out |= LCD_BACKLIGHT;
    else
        out &= (uint8_t)~LCD_BACKLIGHT;

    /* Write as a normal expander byte (no enable pulse) to update backlight immediately.
       We still use lcd_dev_write_byte so behavior/error handling is consistent. */
    return lcd_dev_write_byte(out);
}

/* Public: set backlight on/off. Returns ESP_OK if the I2C write succeeded. */
esp_err_t i2c_lcd_backlight_set(bool on)
{
    s_backlight = on;
    return lcd_write_backlight_state();
}

/* Public: read current cached backlight state */
bool i2c_lcd_backlight_get(void)
{
    return s_backlight;
}
