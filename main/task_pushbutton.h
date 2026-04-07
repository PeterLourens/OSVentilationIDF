// task_pushbutton.h
#ifndef TASK_PUSHBUTTON_H
#define TASK_PUSHBUTTON_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#include "pushbutton.h"
#include "i2c_lcd.h"
#include "logging.h"
#include "driver/gpio.h"

void start_task_pushbutton(void);
void stop_task_pushbutton(void);

#endif
