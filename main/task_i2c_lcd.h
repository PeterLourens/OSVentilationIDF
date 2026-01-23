#ifndef TASK_I2C_LCD_H
#define TASK_I2C_LCD_H

#include <driver/i2c_master.h>

#include "globals.h"
#include "general.h"
#include "i2c_lcd.h"
#include "logging.h"

// Function definitions
void start_task_i2c_lcd(void);
void task_i2c_lcd_code(void *);

#endif