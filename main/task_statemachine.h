#ifndef TASK_STATEMACHINE_H
#define TASK_STATEMACHINE_H

#include <driver/i2c_master.h>

#include "globals.h"
#include "general.h"
#include "statemachine.h"
#include "statemachine_helper.h"
#include "rgb_leds.h"
#include "logging.h"

void start_task_statemachine(void);
void task_statemachine_code(void *);

#endif