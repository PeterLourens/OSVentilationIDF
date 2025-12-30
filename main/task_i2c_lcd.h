#ifndef I2C_TASK_LCD_H
#define I2C_TASK_LCD_H

#include "globals.h"
#include "general.h"
#include "i2c_lcd.h"
// #include "i2c_ds3231.h"

#include "driver/i2c_master.h"

// Function definitions
void start_task_i2c_lcd(void);
void task_i2c_lcd_code(void *);

#endif