/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#ifndef TASK_STATEMACHINE_H
#define TASK_STATEMACHINE_H

#include <driver/i2c_master.h>

#include "general.h"
#include "statemachine.h"
#include "statemachine_helper.h"
#include "rgb_leds.h"
#include "logging.h"

void start_task_statemachine(void);
void task_statemachine_code(void *);

#endif