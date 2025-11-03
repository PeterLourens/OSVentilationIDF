#include "task_i2c_lcd.h"

static const char *TAG = "LCD";

void start_task_i2c_lcd(void)
{
    // Create LCD task
    xTaskCreate(task_i2c_lcd_code, "i2c_lcd", 4096, NULL, 6, &task_i2c_lcd);
}

void task_i2c_lcd_code(void *pvParameters)
{
    char uptime[20] = {};
    char ip_str[16] = {};
    char ping_result[20] = {};
    char formatted_time[32] = {};
    char lcd_contents[100] = {}; // LCD has 20x4=80 chars, so 100 is safe

    ESP_LOGI(TAG, "LCD task started...");

    // Initialize LCD device
    i2c_lcd_init_device();
    i2c_lcd_clear();
    i2c_lcd_backlight_set(false);

    while (1)
    {
        ESP_LOGI(TAG, "Update LCD...");
        i2c_lcd_backlight_set(true);

        formatted_uptime(uptime, sizeof(uptime));
        formatted_datetime(formatted_time, sizeof(formatted_time));
        wifi_manager_get_ip_info(ip_str, sizeof(ip_str));

        if (ping_gateway())
        {
            strcpy(ping_result, "Gateway OK");
        }
        else
        {
            strcpy(ping_result, "Gateway unreachable");
        }

        snprintf(lcd_contents, sizeof(lcd_contents), "%s\nUp: %s\nIP: %s\n%s", formatted_time, uptime, ip_str, ping_result);

        i2c_lcd_write_text(lcd_contents); // The complete display contents is written in one time, not line by line

        // vTaskDelay(pdMS_TO_TICKS(10000));
        // i2c_lcd_clear();
        // i2c_lcd_backlight_set(false);

        vTaskDelay(pdMS_TO_TICKS(20000));
    }
}