#include "task_i2c_lcd.h"

static const char *TAG = "LCD";

void start_task_i2c_lcd(void)
{
    // Create DS3231 sync task
    xTaskCreate(task_i2c_lcd_code, "i2c_lcd", 4096, NULL, 6, &task_i2c_lcd);
}

void task_i2c_lcd_code(void *pvParameters)
{
}